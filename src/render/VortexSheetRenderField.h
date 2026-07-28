#pragma once

#include "engine/ContrailSimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ffatmo::render {

struct VortexSheetRenderPoint {
    std::uint64_t renderId = 0;
    std::uint64_t sourceParcelId = 0;
    std::uint32_t engineIndex = 0;
    engine::WakeSheetLane lane = engine::WakeSheetLane::Core;
    engine::Vec3d worldPositionM {};
    engine::Vec3d worldTangent {0.0, 0.0, -1.0};
    float ageSeconds = 0.0f;
    float physicsRadiusM = 0.0f;
    float opticalDepth = 0.0f;
    float normalizedIceMass = 0.0f;
};

struct VortexSheetRenderFieldMetrics {
    std::size_t inputParcelCount = 0;
    std::size_t initializedSectionCount = 0;
    std::size_t renderPointCount = 0;
    std::array<std::size_t, engine::kWakeSheetLaneCount> pointCountByLane {};
    double maximumLaneGapM = 0.0;
    float maximumLaneCurvatureRadians = 0.0f;
};

struct VortexSheetRenderField {
    std::vector<VortexSheetRenderPoint> points;
    VortexSheetRenderFieldMetrics metrics;
};

inline engine::Vec3d vortexFieldSubtract(
    const engine::Vec3d& a,
    const engine::Vec3d& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline engine::Vec3d vortexFieldMultiply(
    const engine::Vec3d& value,
    double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

inline double vortexFieldDot(
    const engine::Vec3d& a,
    const engine::Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline double vortexFieldMagnitude(const engine::Vec3d& value) {
    return std::sqrt(vortexFieldDot(value, value));
}

inline engine::Vec3d vortexFieldNormalized(
    const engine::Vec3d& value,
    const engine::Vec3d& fallback = {0.0, 0.0, -1.0}) {
    const double length = vortexFieldMagnitude(value);
    if (!std::isfinite(length) || length <= 1.0e-8) return fallback;
    return vortexFieldMultiply(value, 1.0 / length);
}

inline double vortexFieldDistance(
    const engine::Vec3d& a,
    const engine::Vec3d& b) {
    return vortexFieldMagnitude(vortexFieldSubtract(a, b));
}

inline VortexSheetRenderField buildVortexSheetRenderField(
    const std::vector<engine::ContrailParcel>& parcels) {
    VortexSheetRenderField field;
    field.metrics.inputParcelCount = parcels.size();

    for (const auto& parcel : parcels) {
        if (!parcel.vortexSheet.initialized) continue;
        ++field.metrics.initializedSectionCount;

        for (const auto& marker : parcel.vortexSheet.markers) {
            if (!marker.activeForRendering) continue;

            VortexSheetRenderPoint point;
            point.renderId = marker.id;
            point.sourceParcelId = parcel.id;
            point.engineIndex = parcel.engineIndex;
            point.lane = marker.lane;
            point.worldPositionM = marker.worldPositionM;
            point.ageSeconds = parcel.ageSeconds;
            point.physicsRadiusM = parcel.radiusM;
            point.opticalDepth = parcel.opticalDepth;
            point.normalizedIceMass = parcel.normalizedIceMass;
            field.points.push_back(point);

            const std::size_t laneIndex = static_cast<std::size_t>(marker.lane);
            if (laneIndex < field.metrics.pointCountByLane.size()) {
                ++field.metrics.pointCountByLane[laneIndex];
            }
        }
    }

    // Sorting by engine/lane/age turns every material lane into one coherent
    // longitudinal polyline. This is the order used for tangent and continuity
    // calculations; no undisplaced centreline tangent is consulted.
    std::stable_sort(field.points.begin(), field.points.end(),
        [](const VortexSheetRenderPoint& a, const VortexSheetRenderPoint& b) {
            if (a.engineIndex != b.engineIndex) return a.engineIndex < b.engineIndex;
            if (a.lane != b.lane) {
                return static_cast<std::uint8_t>(a.lane) <
                       static_cast<std::uint8_t>(b.lane);
            }
            if (a.ageSeconds != b.ageSeconds) return a.ageSeconds < b.ageSeconds;
            return a.sourceParcelId < b.sourceParcelId;
        });

    std::size_t begin = 0;
    while (begin < field.points.size()) {
        std::size_t end = begin + 1;
        while (end < field.points.size() &&
               field.points[end].engineIndex == field.points[begin].engineIndex &&
               field.points[end].lane == field.points[begin].lane) {
            ++end;
        }

        for (std::size_t index = begin; index < end; ++index) {
            engine::Vec3d tangent {0.0, 0.0, -1.0};
            if (end - begin >= 2) {
                if (index == begin) {
                    tangent = vortexFieldSubtract(
                        field.points[index + 1].worldPositionM,
                        field.points[index].worldPositionM);
                } else if (index + 1 == end) {
                    tangent = vortexFieldSubtract(
                        field.points[index].worldPositionM,
                        field.points[index - 1].worldPositionM);
                } else {
                    tangent = vortexFieldSubtract(
                        field.points[index + 1].worldPositionM,
                        field.points[index - 1].worldPositionM);
                }
            }
            field.points[index].worldTangent = vortexFieldNormalized(tangent);

            if (index > begin) {
                const double gap = vortexFieldDistance(
                    field.points[index - 1].worldPositionM,
                    field.points[index].worldPositionM);
                if (std::isfinite(gap)) {
                    field.metrics.maximumLaneGapM = std::max(
                        field.metrics.maximumLaneGapM, gap);
                }
            }

            if (index > begin && index + 1 < end) {
                const auto previousDirection = vortexFieldNormalized(
                    vortexFieldSubtract(
                        field.points[index].worldPositionM,
                        field.points[index - 1].worldPositionM));
                const auto nextDirection = vortexFieldNormalized(
                    vortexFieldSubtract(
                        field.points[index + 1].worldPositionM,
                        field.points[index].worldPositionM));
                const double cosine = std::clamp(
                    vortexFieldDot(previousDirection, nextDirection), -1.0, 1.0);
                const float curvature = static_cast<float>(std::acos(cosine));
                if (std::isfinite(curvature)) {
                    field.metrics.maximumLaneCurvatureRadians = std::max(
                        field.metrics.maximumLaneCurvatureRadians, curvature);
                }
            }
        }

        begin = end;
    }

    field.metrics.renderPointCount = field.points.size();
    return field;
}

}  // namespace ffatmo::render
