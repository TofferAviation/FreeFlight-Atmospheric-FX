#pragma once

#include "engine/WakeFluidSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ffatmo::engine {

// Renderer Foundation v6.9 physical representation. A wake section is no
// longer represented only by one centreline parcel. These material markers
// persist across frames and are independently advected by WakeFluidSolver.
enum class WakeSheetLane : std::uint8_t {
    Inner = 0,
    Core = 1,
    Outer = 2,
    Secondary = 3
};

constexpr std::size_t kWakeSheetLaneCount = 4;

struct WakeVortexSheetSettings {
    // Initial unresolved ice-plume half width around the engine exhaust source.
    float plumeHalfWidthM = 1.25f;

    // Small cross-sheet thickness prevents all markers from being perfectly
    // co-linear in the wake plane at birth.
    float sheetThicknessM = 0.35f;

    // The secondary lane is physically integrated from birth but hidden until
    // organised roll-up has had time to develop.
    bool secondaryWakeEnabled = true;
    float secondaryWakeVisibleAgeSeconds = 7.0f;
};

struct WakeSheetMarkerState {
    std::uint64_t id = 0;
    WakeSheetLane lane = WakeSheetLane::Core;

    Vec3d worldPositionM {};
    Vec3d previousWorldPositionM {};

    // Each lane owns a full persistent wake-fluid state. This is the key v6.9
    // change: the cross-section deforms because every material marker samples
    // the finite-core vortex pair at its own lateral/vertical location.
    WakeFluidState wakeFluid {};

    float seedLateralOffsetM = 0.0f;
    float seedVerticalOffsetM = 0.0f;
    float ageSeconds = 0.0f;
    bool activeForRendering = true;
};

struct WakeVortexSheetState {
    std::array<WakeSheetMarkerState, kWakeSheetLaneCount> markers {};
    std::size_t activeMarkerCount = 0;
    bool initialized = false;
};

struct WakeVortexSheetStepResult {
    bool finite = true;
    std::size_t activeMarkerCount = 0;
    float maximumLateralSpreadM = 0.0f;
    float maximumVerticalSpreadM = 0.0f;
    float maximumInducedSpeedMps = 0.0f;
};

inline Vec3d wakeSheetAdd(const Vec3d& a, const Vec3d& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3d wakeSheetMultiply(const Vec3d& value, double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

inline bool wakeSheetFinite(const Vec3d& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline WakeVortexSheetState initializeWakeVortexSheet(
    std::uint64_t sectionId,
    const Vec3d& sourceWorldPositionM,
    const WakeFluidAircraftGeometry& aircraft,
    const WakeFluidEnvironment& environment,
    const Vec3d& rightWorld,
    const Vec3d& upWorld,
    float sourceLateralM,
    float sourceVerticalM,
    const WakeFluidSettings& wakeSettings = {},
    const WakeVortexSheetSettings& sheetSettings = {}) {
    WakeVortexSheetState sheet;

    const float halfWidth = std::max(sheetSettings.plumeHalfWidthM, 0.05f);
    const float thickness = std::max(sheetSettings.sheetThicknessM, 0.0f);
    const float side = sourceLateralM < -0.05f ? -1.0f :
                       (sourceLateralM > 0.05f ? 1.0f : 1.0f);

    // "Inner" means toward aircraft centreline; "outer" means away from it.
    // The secondary lane is seeded beyond/below the outer sheet and remains
    // hidden until the roll-up onset gate opens.
    const std::array<float, kWakeSheetLaneCount> lateralOffsets {
        -side * halfWidth * 0.70f,
        0.0f,
        side * halfWidth * 0.95f,
        side * halfWidth * 1.30f
    };
    const std::array<float, kWakeSheetLaneCount> verticalOffsets {
        thickness * 0.35f,
        0.0f,
        -thickness * 0.35f,
        -thickness * 0.90f
    };
    const std::array<WakeSheetLane, kWakeSheetLaneCount> lanes {
        WakeSheetLane::Inner,
        WakeSheetLane::Core,
        WakeSheetLane::Outer,
        WakeSheetLane::Secondary
    };

    for (std::size_t index = 0; index < sheet.markers.size(); ++index) {
        auto& marker = sheet.markers[index];
        marker.id = (sectionId << 3u) | static_cast<std::uint64_t>(index);
        marker.lane = lanes[index];
        marker.seedLateralOffsetM = lateralOffsets[index];
        marker.seedVerticalOffsetM = verticalOffsets[index];

        const Vec3d lateralWorld = wakeSheetMultiply(rightWorld, lateralOffsets[index]);
        const Vec3d verticalWorld = wakeSheetMultiply(upWorld, verticalOffsets[index]);
        marker.worldPositionM = wakeSheetAdd(
            sourceWorldPositionM, wakeSheetAdd(lateralWorld, verticalWorld));
        marker.previousWorldPositionM = marker.worldPositionM;

        marker.wakeFluid = initializeWakeFluidState(
            aircraft,
            environment,
            rightWorld,
            upWorld,
            sourceLateralM + lateralOffsets[index],
            sourceVerticalM + verticalOffsets[index],
            wakeSettings);

        marker.activeForRendering = index != 3 ||
            (sheetSettings.secondaryWakeEnabled &&
             sheetSettings.secondaryWakeVisibleAgeSeconds <= 0.0f);
    }

    sheet.activeMarkerCount = sheetSettings.secondaryWakeEnabled &&
                              sheetSettings.secondaryWakeVisibleAgeSeconds <= 0.0f
        ? kWakeSheetLaneCount
        : kWakeSheetLaneCount - 1;
    sheet.initialized = true;
    return sheet;
}

inline WakeVortexSheetStepResult advanceWakeVortexSheet(
    WakeVortexSheetState& sheet,
    float sectionAgeSeconds,
    float deltaSeconds,
    const Vec3d& ambientWorldVelocityMps,
    const WakeFluidEnvironment& environment,
    const WakeFluidSettings& wakeSettings = {},
    const WakeVortexSheetSettings& sheetSettings = {}) {
    WakeVortexSheetStepResult result;
    if (!sheet.initialized || deltaSeconds <= 0.0f) {
        result.activeMarkerCount = sheet.activeMarkerCount;
        return result;
    }

    const float boundedDelta = std::clamp(deltaSeconds, 0.0f, 0.25f);
    const Vec3d ambientDelta = wakeSheetMultiply(ambientWorldVelocityMps, boundedDelta);

    float minimumLateral = 0.0f;
    float maximumLateral = 0.0f;
    float minimumVertical = 0.0f;
    float maximumVertical = 0.0f;
    bool haveSpreadSample = false;

    for (std::size_t index = 0; index < sheet.markers.size(); ++index) {
        auto& marker = sheet.markers[index];
        marker.previousWorldPositionM = marker.worldPositionM;
        marker.ageSeconds = sectionAgeSeconds;
        marker.worldPositionM = wakeSheetAdd(marker.worldPositionM, ambientDelta);

        const WakeFluidStepResult wakeStep = advanceWakeFluidState(
            marker.wakeFluid,
            marker.id,
            sectionAgeSeconds,
            boundedDelta,
            environment,
            wakeSettings);

        if (!wakeStep.finite) {
            result.finite = false;
            continue;
        }

        marker.worldPositionM = wakeSheetAdd(
            marker.worldPositionM, wakeStep.worldDisplacementM);
        result.maximumInducedSpeedMps = std::max(
            result.maximumInducedSpeedMps, wakeStep.inducedSpeedMps);

        if (marker.lane == WakeSheetLane::Secondary) {
            marker.activeForRendering = sheetSettings.secondaryWakeEnabled &&
                sectionAgeSeconds >= std::max(
                    sheetSettings.secondaryWakeVisibleAgeSeconds, 0.0f);
        }

        if (!marker.activeForRendering) continue;

        const float lateral = marker.wakeFluid.lateralM;
        const float vertical = marker.wakeFluid.verticalM;
        if (!haveSpreadSample) {
            minimumLateral = maximumLateral = lateral;
            minimumVertical = maximumVertical = vertical;
            haveSpreadSample = true;
        } else {
            minimumLateral = std::min(minimumLateral, lateral);
            maximumLateral = std::max(maximumLateral, lateral);
            minimumVertical = std::min(minimumVertical, vertical);
            maximumVertical = std::max(maximumVertical, vertical);
        }

        if (!wakeSheetFinite(marker.worldPositionM)) result.finite = false;
    }

    sheet.activeMarkerCount = 0;
    for (const auto& marker : sheet.markers) {
        if (marker.activeForRendering) ++sheet.activeMarkerCount;
    }

    result.activeMarkerCount = sheet.activeMarkerCount;
    if (haveSpreadSample) {
        result.maximumLateralSpreadM = maximumLateral - minimumLateral;
        result.maximumVerticalSpreadM = maximumVertical - minimumVertical;
    }
    return result;
}

inline const WakeSheetMarkerState* wakeSheetMarker(
    const WakeVortexSheetState& sheet,
    WakeSheetLane lane) {
    const std::size_t index = static_cast<std::size_t>(lane);
    if (index >= sheet.markers.size()) return nullptr;
    return &sheet.markers[index];
}

inline WakeSheetMarkerState* wakeSheetMarker(
    WakeVortexSheetState& sheet,
    WakeSheetLane lane) {
    const std::size_t index = static_cast<std::size_t>(lane);
    if (index >= sheet.markers.size()) return nullptr;
    return &sheet.markers[index];
}

}  // namespace ffatmo::engine
