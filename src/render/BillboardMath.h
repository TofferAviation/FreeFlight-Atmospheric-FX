#pragma once

#include "engine/SimulatorSnapshot.h"

#include <algorithm>
#include <cmath>

namespace ffatmo::render {

struct BillboardAngles {
    float headingDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
};

inline engine::Vec3d normalizedVector(const engine::Vec3d& value,
                                      const engine::Vec3d& fallback) {
    const double magnitude = std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z);
    if (!std::isfinite(magnitude) || magnitude <= 1.0e-9) return fallback;
    return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

inline double vectorLength(const engine::Vec3d& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

inline double dotVector(const engine::Vec3d& a, const engine::Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline engine::Vec3d crossVector(const engine::Vec3d& a, const engine::Vec3d& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline BillboardAngles calculateBillboardAngles(const engine::Vec3d& objectPosition,
                                                 const engine::Vec3d& cameraPosition) {
    constexpr double radiansToDegrees = 57.2957795130823208768;
    const double deltaX = cameraPosition.x - objectPosition.x;
    const double deltaY = cameraPosition.y - objectPosition.y;
    const double deltaZ = cameraPosition.z - objectPosition.z;
    const double horizontal = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);

    BillboardAngles result;
    result.headingDeg = static_cast<float>(std::atan2(deltaX, -deltaZ) * radiansToDegrees);
    result.pitchDeg = static_cast<float>(
        std::atan2(deltaY, std::max(horizontal, 1.0e-9)) * radiansToDegrees);
    return result;
}

inline double trailProjectionFactor(const engine::Vec3d& objectPosition,
                                    const engine::Vec3d& cameraPosition,
                                    const engine::Vec3d& trailTangentLocal) {
    const engine::Vec3d viewDirection = normalizedVector({
        cameraPosition.x - objectPosition.x,
        cameraPosition.y - objectPosition.y,
        cameraPosition.z - objectPosition.z
    }, {0.0, 0.0, -1.0});
    const engine::Vec3d tangent = normalizedVector(trailTangentLocal, {0.0, 0.0, -1.0});
    const double parallel = std::clamp(std::abs(dotVector(viewDirection, tangent)), 0.0, 1.0);
    return std::sqrt(std::max(0.0, 1.0 - parallel * parallel));
}

inline BillboardAngles calculateTrailBillboardAngles(
    const engine::Vec3d& objectPosition,
    const engine::Vec3d& cameraPosition,
    const engine::Vec3d& trailTangentLocal) {
    constexpr double degreesToRadians = 0.017453292519943295769;
    constexpr double radiansToDegrees = 57.2957795130823208768;

    BillboardAngles result = calculateBillboardAngles(objectPosition, cameraPosition);
    const double heading = static_cast<double>(result.headingDeg) * degreesToRadians;
    const double pitch = static_cast<double>(result.pitchDeg) * degreesToRadians;

    const engine::Vec3d normal {
        std::sin(heading) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(heading) * std::cos(pitch)
    };
    const engine::Vec3d referenceUp {
        -std::sin(heading) * std::sin(pitch),
        std::cos(pitch),
        std::cos(heading) * std::sin(pitch)
    };
    const engine::Vec3d referenceRight = normalizedVector(
        crossVector(normal, referenceUp), {1.0, 0.0, 0.0});

    const engine::Vec3d tangent = normalizedVector(trailTangentLocal, referenceUp);
    const double tangentNormalComponent = dotVector(tangent, normal);
    const engine::Vec3d projectedTangent {
        tangent.x - normal.x * tangentNormalComponent,
        tangent.y - normal.y * tangentNormalComponent,
        tangent.z - normal.z * tangentNormalComponent
    };
    const double projectedLength = vectorLength(projectedTangent);

    // When looking almost directly along a trail segment, its projected direction
    // is undefined. Holding roll at zero is stable and the renderer shortens the
    // segment using trailProjectionFactor(), avoiding the v4.1 vertical spike.
    if (!std::isfinite(projectedLength) || projectedLength < 0.06) {
        result.rollDeg = 0.0f;
        return result;
    }

    const engine::Vec3d alignedTangent = normalizedVector(projectedTangent, referenceUp);
    const double rightComponent = dotVector(alignedTangent, referenceRight);
    const double upComponent = dotVector(alignedTangent, referenceUp);
    result.rollDeg = static_cast<float>(std::atan2(rightComponent, upComponent) * radiansToDegrees);
    return result;
}

inline double billboardAlignmentErrorDegrees(const engine::Vec3d& objectPosition,
                                             const engine::Vec3d& cameraPosition,
                                             const BillboardAngles& angles) {
    constexpr double degreesToRadians = 0.017453292519943295769;
    constexpr double radiansToDegrees = 57.2957795130823208768;

    const double heading = static_cast<double>(angles.headingDeg) * degreesToRadians;
    const double pitch = static_cast<double>(angles.pitchDeg) * degreesToRadians;
    const engine::Vec3d normal {
        std::sin(heading) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(heading) * std::cos(pitch)
    };

    const engine::Vec3d target = normalizedVector({
        cameraPosition.x - objectPosition.x,
        cameraPosition.y - objectPosition.y,
        cameraPosition.z - objectPosition.z
    }, normal);
    const double alignment = std::clamp(dotVector(normal, target), -1.0, 1.0);
    return std::acos(alignment) * radiansToDegrees;
}

inline double trailAlignmentErrorDegrees(const engine::Vec3d& objectPosition,
                                         const engine::Vec3d& cameraPosition,
                                         const engine::Vec3d& trailTangentLocal,
                                         const BillboardAngles& angles) {
    constexpr double degreesToRadians = 0.017453292519943295769;
    constexpr double radiansToDegrees = 57.2957795130823208768;

    if (trailProjectionFactor(objectPosition, cameraPosition, trailTangentLocal) < 0.06) {
        return 0.0;
    }

    const double heading = static_cast<double>(angles.headingDeg) * degreesToRadians;
    const double pitch = static_cast<double>(angles.pitchDeg) * degreesToRadians;
    const double roll = static_cast<double>(angles.rollDeg) * degreesToRadians;
    const engine::Vec3d normal {
        std::sin(heading) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(heading) * std::cos(pitch)
    };
    const engine::Vec3d referenceUp {
        -std::sin(heading) * std::sin(pitch),
        std::cos(pitch),
        std::cos(heading) * std::sin(pitch)
    };
    const engine::Vec3d referenceRight = normalizedVector(
        crossVector(normal, referenceUp), {1.0, 0.0, 0.0});
    const engine::Vec3d renderedLongAxis {
        referenceUp.x * std::cos(roll) + referenceRight.x * std::sin(roll),
        referenceUp.y * std::cos(roll) + referenceRight.y * std::sin(roll),
        referenceUp.z * std::cos(roll) + referenceRight.z * std::sin(roll)
    };

    const double tangentNormalComponent = dotVector(trailTangentLocal, normal);
    const engine::Vec3d projectedTangent = normalizedVector({
        trailTangentLocal.x - normal.x * tangentNormalComponent,
        trailTangentLocal.y - normal.y * tangentNormalComponent,
        trailTangentLocal.z - normal.z * tangentNormalComponent
    }, renderedLongAxis);
    const double alignment = std::clamp(
        std::abs(dotVector(renderedLongAxis, projectedTangent)), -1.0, 1.0);
    return std::acos(alignment) * radiansToDegrees;
}

}  // namespace ffatmo::render
