#pragma once

#include "engine/SimulatorSnapshot.h"

#include <algorithm>
#include <cmath>

namespace ffatmo::render {

struct BillboardAngles {
    float headingDeg = 0.0f;
    float pitchDeg = 0.0f;
};

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

    engine::Vec3d target {
        cameraPosition.x - objectPosition.x,
        cameraPosition.y - objectPosition.y,
        cameraPosition.z - objectPosition.z
    };
    const double targetLength = std::sqrt(
        target.x * target.x + target.y * target.y + target.z * target.z);
    if (targetLength <= 1.0e-9) return 0.0;
    target.x /= targetLength;
    target.y /= targetLength;
    target.z /= targetLength;

    const double dot = std::clamp(
        normal.x * target.x + normal.y * target.y + normal.z * target.z,
        -1.0,
        1.0);
    return std::acos(dot) * radiansToDegrees;
}

}  // namespace ffatmo::render
