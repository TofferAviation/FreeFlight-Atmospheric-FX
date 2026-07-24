#include "diagnostics/LiveSnapshotNormalizer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ffatmo::diagnostics {
namespace {

constexpr double kEarthRadiusM = 6378137.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr double kLocalJumpThresholdM = 1000.0;
constexpr double kGeodeticContinuityThresholdM = 500.0;
constexpr double kMaximumPhysicsStepSeconds = 0.25;
constexpr float kMinimumAerodynamicAirspeedMps = 15.0f;

bool finite(double value) { return std::isfinite(value); }
bool finite(float value) { return std::isfinite(value); }

double squared(double value) { return value * value; }

double localDistanceM(const engine::Vec3d& first, const engine::Vec3d& second) {
    return std::sqrt(squared(first.x - second.x) +
                     squared(first.y - second.y) +
                     squared(first.z - second.z));
}

double geodeticDistanceM(double latitudeA,
                         double longitudeA,
                         double latitudeB,
                         double longitudeB) {
    const double latA = latitudeA * kDegreesToRadians;
    const double latB = latitudeB * kDegreesToRadians;
    const double deltaLatitude = (latitudeB - latitudeA) * kDegreesToRadians;
    const double deltaLongitude = (longitudeB - longitudeA) * kDegreesToRadians;
    const double sinLatitude = std::sin(deltaLatitude * 0.5);
    const double sinLongitude = std::sin(deltaLongitude * 0.5);
    const double haversine = sinLatitude * sinLatitude +
        std::cos(latA) * std::cos(latB) * sinLongitude * sinLongitude;
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(haversine)));
}

std::array<double, 3> geodeticToEcef(double latitudeDeg,
                                      double longitudeDeg,
                                      double altitudeM) {
    constexpr double semiMajor = 6378137.0;
    constexpr double eccentricitySquared = 6.69437999014e-3;
    const double latitude = latitudeDeg * kDegreesToRadians;
    const double longitude = longitudeDeg * kDegreesToRadians;
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double radius = semiMajor /
        std::sqrt(1.0 - eccentricitySquared * sinLatitude * sinLatitude);
    return {
        (radius + altitudeM) * cosLatitude * std::cos(longitude),
        (radius + altitudeM) * cosLatitude * std::sin(longitude),
        (radius * (1.0 - eccentricitySquared) + altitudeM) * sinLatitude
    };
}

void ecefDeltaToEnu(const std::array<double, 3>& anchor,
                    double anchorLatitudeDeg,
                    double anchorLongitudeDeg,
                    const std::array<double, 3>& value,
                    double& east,
                    double& up,
                    double& north) {
    const double latitude = anchorLatitudeDeg * kDegreesToRadians;
    const double longitude = anchorLongitudeDeg * kDegreesToRadians;
    const double deltaX = value[0] - anchor[0];
    const double deltaY = value[1] - anchor[1];
    const double deltaZ = value[2] - anchor[2];
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double sinLongitude = std::sin(longitude);
    const double cosLongitude = std::cos(longitude);

    east = -sinLongitude * deltaX + cosLongitude * deltaY;
    north = -sinLatitude * cosLongitude * deltaX -
            sinLatitude * sinLongitude * deltaY + cosLatitude * deltaZ;
    up = cosLatitude * cosLongitude * deltaX +
         cosLatitude * sinLongitude * deltaY + sinLatitude * deltaZ;
}

float interpolateProfile(
    const std::array<float, engine::kMaximumAtmosphereLevels>& altitude,
    const std::array<float, engine::kMaximumAtmosphereLevels>& values,
    std::uint32_t count,
    float targetAltitude,
    float fallback) {
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(altitude.size()));
    if (count == 0) return fallback;
    if (count == 1 || targetAltitude <= altitude[0]) return values[0];
    for (std::uint32_t index = 1; index < count; ++index) {
        if (targetAltitude <= altitude[index]) {
            const float span = altitude[index] - altitude[index - 1];
            if (std::abs(span) < 0.001f) return values[index];
            const float ratio = std::clamp(
                (targetAltitude - altitude[index - 1]) / span, 0.0f, 1.0f);
            return values[index - 1] + (values[index] - values[index - 1]) * ratio;
        }
    }
    return values[count - 1];
}

float saturationVaporPressureWaterPa(float temperatureK) {
    const double temperatureC = static_cast<double>(temperatureK) - 273.15;
    return static_cast<float>(611.21 * std::exp(
        (18.678 - temperatureC / 234.5) *
        (temperatureC / (257.14 + temperatureC))));
}

float saturationVaporPressureIcePa(float temperatureK) {
    const double temperatureC = static_cast<double>(temperatureK) - 273.15;
    return static_cast<float>(611.15 * std::exp(
        (23.036 - temperatureC / 333.7) *
        (temperatureC / (279.82 + temperatureC))));
}

float relativeHumidityIcePercent(float temperatureK, float dewPointK) {
    if (!finite(temperatureK) || !finite(dewPointK) ||
        temperatureK <= 0.0f || dewPointK <= 0.0f) {
        return 0.0f;
    }
    const float vaporPressure = saturationVaporPressureWaterPa(dewPointK);
    const float saturationIce = saturationVaporPressureIcePa(temperatureK);
    if (!finite(vaporPressure) || !finite(saturationIce) || saturationIce <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(100.0f * vaporPressure / saturationIce, 0.0f, 500.0f);
}

}  // namespace

void LiveSnapshotNormalizer::reset() {
    anchorEcef_ = {};
    anchorLatitudeDeg_ = 0.0;
    anchorLongitudeDeg_ = 0.0;
    previous_ = {};
    initialized_ = false;
    localOriginRebaseCount_ = 0;
    integratedPhysicsTimeSeconds_ = 0.0;
}

NormalizedReplaySample LiveSnapshotNormalizer::normalize(
    const engine::SimulatorSnapshot& snapshot) {
    NormalizedReplaySample sample;
    sample.sequenceNumber = snapshot.sequenceNumber;
    sample.altitudeMslM = snapshot.elevationMslM;
    sample.trueAirspeedMps = snapshot.trueAirspeedMps;
    sample.temperatureK = snapshot.atmosphere.temperatureK;
    sample.flapRatio = snapshot.flapRatio;
    sample.slatRatio = snapshot.slatRatio;
    sample.paused = snapshot.paused != 0;
    sample.replaying = snapshot.replaying != 0;
    sample.aerodynamicAnglesValid = finite(snapshot.trueAirspeedMps) &&
        snapshot.trueAirspeedMps >= kMinimumAerodynamicAirspeedMps;

    const auto currentEcef = geodeticToEcef(
        snapshot.latitudeDeg, snapshot.longitudeDeg, snapshot.elevationMslM);

    if (!initialized_) {
        anchorEcef_.x = currentEcef[0];
        anchorEcef_.y = currentEcef[1];
        anchorEcef_.z = currentEcef[2];
        anchorLatitudeDeg_ = snapshot.latitudeDeg;
        anchorLongitudeDeg_ = snapshot.longitudeDeg;
        initialized_ = true;
    }

    const std::array<double, 3> anchor {
        anchorEcef_.x, anchorEcef_.y, anchorEcef_.z
    };
    ecefDeltaToEnu(anchor,
                   anchorLatitudeDeg_,
                   anchorLongitudeDeg_,
                   currentEcef,
                   sample.worldEastM,
                   sample.worldUpM,
                   sample.worldNorthM);

    if (previous_.sequenceNumber != 0) {
        double recordedDelta = snapshot.simulatorUptimeSeconds -
                               previous_.simulatorUptimeSeconds;
        if (!finite(recordedDelta) || recordedDelta < 0.0) recordedDelta = 0.0;
        if (!sample.paused && !sample.replaying) {
            sample.physicsDeltaSeconds = std::clamp(
                recordedDelta, 0.0, kMaximumPhysicsStepSeconds);
            integratedPhysicsTimeSeconds_ += sample.physicsDeltaSeconds;
        }

        if (recordedDelta > 0.0001 && finite(recordedDelta)) {
            sample.verticalSpeedMps = static_cast<float>(
                (snapshot.elevationMslM - previous_.elevationMslM) / recordedDelta);
        }

        const double localJump = localDistanceM(
            snapshot.localPositionM, previous_.localPositionM);
        const double geodeticJump = geodeticDistanceM(
            previous_.latitudeDeg,
            previous_.longitudeDeg,
            snapshot.latitudeDeg,
            snapshot.longitudeDeg);
        if (finite(localJump) && finite(geodeticJump) &&
            localJump > kLocalJumpThresholdM &&
            geodeticJump < kGeodeticContinuityThresholdM) {
            sample.localOriginRebased = true;
            ++localOriginRebaseCount_;
        }
    }
    sample.simulationTimeSeconds = integratedPhysicsTimeSeconds_;

    const auto& profile = snapshot.atmosphere.profile;
    sample.dewPointK = interpolateProfile(
        profile.dewPointAltitudeMslM,
        profile.dewPointK,
        profile.dewPointLevelCount,
        static_cast<float>(snapshot.elevationMslM),
        snapshot.atmosphere.temperatureK);
    sample.relativeHumidityIcePercent = relativeHumidityIcePercent(
        sample.temperatureK, sample.dewPointK);

    const std::uint32_t engineCount = std::min<std::uint32_t>(
        snapshot.engineCount, static_cast<std::uint32_t>(snapshot.engines.size()));
    if (engineCount > 0) {
        double n1Total = 0.0;
        double thrustTotal = 0.0;
        for (std::uint32_t index = 0; index < engineCount; ++index) {
            n1Total += snapshot.engines[index].n1Percent;
            thrustTotal += snapshot.engines[index].thrustN;
        }
        sample.meanEngineN1Percent = static_cast<float>(n1Total / engineCount);
        sample.meanEngineThrustN = static_cast<float>(thrustTotal / engineCount);
    }

    previous_ = snapshot;
    return sample;
}

}  // namespace ffatmo::diagnostics
