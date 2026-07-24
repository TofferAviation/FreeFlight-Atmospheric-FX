#include "diagnostics/ReplayRunner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ffatmo::diagnostics {
namespace {

constexpr double kEarthRadiusM = 6378137.0;
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr float kMinimumAerodynamicAirspeedMps = 15.0f;
constexpr double kLocalJumpThresholdM = 1000.0;
constexpr double kGeodeticContinuityThresholdM = 500.0;
constexpr double kMaximumPhysicsStepSeconds = 0.25;
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

bool finite(double value) { return std::isfinite(value); }
bool finite(float value) { return std::isfinite(value); }

double squared(double value) { return value * value; }

double distance3(const engine::Vec3d& a, const engine::Vec3d& b) {
    return std::sqrt(squared(a.x - b.x) + squared(a.y - b.y) + squared(a.z - b.z));
}

double geodeticDistanceM(double latitudeA,
                         double longitudeA,
                         double latitudeB,
                         double longitudeB) {
    const double latA = latitudeA * kDegreesToRadians;
    const double latB = latitudeB * kDegreesToRadians;
    const double deltaLat = (latitudeB - latitudeA) * kDegreesToRadians;
    const double deltaLon = (longitudeB - longitudeA) * kDegreesToRadians;
    const double sinLat = std::sin(deltaLat * 0.5);
    const double sinLon = std::sin(deltaLon * 0.5);
    const double h = sinLat * sinLat + std::cos(latA) * std::cos(latB) * sinLon * sinLon;
    return 2.0 * kEarthRadiusM * std::asin(std::min(1.0, std::sqrt(h)));
}

struct Ecef {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Ecef geodeticToEcef(double latitudeDeg, double longitudeDeg, double altitudeM) {
    constexpr double semiMajor = 6378137.0;
    constexpr double eccentricitySquared = 6.69437999014e-3;
    const double latitude = latitudeDeg * kDegreesToRadians;
    const double longitude = longitudeDeg * kDegreesToRadians;
    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double radius = semiMajor / std::sqrt(1.0 - eccentricitySquared * sinLatitude * sinLatitude);
    return {
        (radius + altitudeM) * cosLatitude * std::cos(longitude),
        (radius + altitudeM) * cosLatitude * std::sin(longitude),
        (radius * (1.0 - eccentricitySquared) + altitudeM) * sinLatitude
    };
}

void ecefDeltaToEnu(const Ecef& origin,
                    double originLatitudeDeg,
                    double originLongitudeDeg,
                    const Ecef& value,
                    double& east,
                    double& up,
                    double& north) {
    const double latitude = originLatitudeDeg * kDegreesToRadians;
    const double longitude = originLongitudeDeg * kDegreesToRadians;
    const double dx = value.x - origin.x;
    const double dy = value.y - origin.y;
    const double dz = value.z - origin.z;
    const double sinLat = std::sin(latitude);
    const double cosLat = std::cos(latitude);
    const double sinLon = std::sin(longitude);
    const double cosLon = std::cos(longitude);

    east = -sinLon * dx + cosLon * dy;
    north = -sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz;
    up = cosLat * cosLon * dx + cosLat * sinLon * dy + sinLat * dz;
}

float interpolateProfile(const std::array<float, engine::kMaximumAtmosphereLevels>& altitude,
                         const std::array<float, engine::kMaximumAtmosphereLevels>& values,
                         std::uint32_t count,
                         float targetAltitude,
                         float fallback) {
    if (count == 0) return fallback;
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(altitude.size()));
    if (count == 1 || targetAltitude <= altitude[0]) return values[0];
    for (std::uint32_t index = 1; index < count; ++index) {
        if (targetAltitude <= altitude[index]) {
            const float lowerAltitude = altitude[index - 1];
            const float upperAltitude = altitude[index];
            const float span = upperAltitude - lowerAltitude;
            if (std::abs(span) < 0.001f) return values[index];
            const float ratio = std::clamp((targetAltitude - lowerAltitude) / span, 0.0f, 1.0f);
            return values[index - 1] + (values[index] - values[index - 1]) * ratio;
        }
    }
    return values[count - 1];
}

float saturationVaporPressureWaterPa(float temperatureK) {
    const double temperatureC = static_cast<double>(temperatureK) - 273.15;
    return static_cast<float>(611.21 * std::exp((18.678 - temperatureC / 234.5) *
                                                (temperatureC / (257.14 + temperatureC))));
}

float saturationVaporPressureIcePa(float temperatureK) {
    const double temperatureC = static_cast<double>(temperatureK) - 273.15;
    return static_cast<float>(611.15 * std::exp((23.036 - temperatureC / 333.7) *
                                                (temperatureC / (279.82 + temperatureC))));
}

float relativeHumidityIcePercent(float temperatureK, float dewPointK) {
    if (!finite(temperatureK) || !finite(dewPointK) || temperatureK <= 0.0f || dewPointK <= 0.0f) {
        return 0.0f;
    }
    const float vaporPressure = saturationVaporPressureWaterPa(dewPointK);
    const float saturationIce = saturationVaporPressureIcePa(temperatureK);
    if (!finite(vaporPressure) || !finite(saturationIce) || saturationIce <= 0.0f) return 0.0f;
    return std::clamp(100.0f * vaporPressure / saturationIce, 0.0f, 500.0f);
}

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

template <typename T>
void hashValue(std::uint64_t& hash, const T& value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashSample(std::uint64_t& hash, const NormalizedReplaySample& sample) {
    hashValue(hash, sample.sequenceNumber);
    hashValue(hash, sample.simulationTimeSeconds);
    hashValue(hash, sample.physicsDeltaSeconds);
    hashValue(hash, sample.worldEastM);
    hashValue(hash, sample.worldUpM);
    hashValue(hash, sample.worldNorthM);
    hashValue(hash, sample.altitudeMslM);
    hashValue(hash, sample.trueAirspeedMps);
    hashValue(hash, sample.verticalSpeedMps);
    hashValue(hash, sample.temperatureK);
    hashValue(hash, sample.dewPointK);
    hashValue(hash, sample.relativeHumidityIcePercent);
    hashValue(hash, sample.meanEngineN1Percent);
    hashValue(hash, sample.meanEngineThrustN);
    hashValue(hash, sample.flapRatio);
    hashValue(hash, sample.slatRatio);
    const std::uint8_t flags[] {
        static_cast<std::uint8_t>(sample.paused),
        static_cast<std::uint8_t>(sample.replaying),
        static_cast<std::uint8_t>(sample.localOriginRebased),
        static_cast<std::uint8_t>(sample.aerodynamicAnglesValid)
    };
    hashBytes(hash, flags, sizeof(flags));
}

bool writeParentDirectories(const std::filesystem::path& outputPath, std::string* error) {
    if (outputPath.parent_path().empty()) return true;
    std::error_code directoryError;
    std::filesystem::create_directories(outputPath.parent_path(), directoryError);
    if (directoryError) {
        if (error) *error = directoryError.message();
        return false;
    }
    return true;
}

}  // namespace

ReplayRunnerResult runReplayAnalysis(const ReplayReadResult& replay) {
    ReplayRunnerResult result;
    auto& summary = result.summary;
    if (!replay.ok) {
        summary.error = replay.error.empty() ? "Replay reader rejected the file" : replay.error;
        return result;
    }
    if (replay.snapshots.empty()) {
        summary.error = "Replay contains no snapshots";
        return result;
    }

    summary.inputSnapshotCount = replay.snapshots.size();
    result.samples.reserve(replay.snapshots.size());

    const auto& first = replay.snapshots.front();
    if (!finite(first.latitudeDeg) || !finite(first.longitudeDeg) || !finite(first.elevationMslM)) {
        summary.error = "First snapshot has no finite geodetic anchor";
        return result;
    }

    const Ecef anchorEcef = geodeticToEcef(first.latitudeDeg, first.longitudeDeg, first.elevationMslM);
    const double anchorLatitude = first.latitudeDeg;
    const double anchorLongitude = first.longitudeDeg;
    summary.firstSimulatorTimeSeconds = first.simulatorUptimeSeconds;
    summary.lastSimulatorTimeSeconds = first.simulatorUptimeSeconds;
    summary.minimumAltitudeMslM = first.elevationMslM;
    summary.maximumAltitudeMslM = first.elevationMslM;
    summary.minimumTemperatureK = first.atmosphere.temperatureK;
    summary.maximumTemperatureK = first.atmosphere.temperatureK;
    summary.minimumRelativeHumidityIcePercent = std::numeric_limits<float>::max();
    summary.maximumRelativeHumidityIcePercent = std::numeric_limits<float>::lowest();

    std::uint64_t hash = kFnvOffset;
    const engine::SimulatorSnapshot* previous = nullptr;
    double normalizedSimulationTime = 0.0;

    for (const auto& snapshot : replay.snapshots) {
        NormalizedReplaySample sample;
        sample.sequenceNumber = snapshot.sequenceNumber;
        sample.paused = snapshot.paused != 0;
        sample.replaying = snapshot.replaying != 0;
        sample.altitudeMslM = snapshot.elevationMslM;
        sample.trueAirspeedMps = snapshot.trueAirspeedMps;
        sample.temperatureK = snapshot.atmosphere.temperatureK;
        sample.flapRatio = snapshot.flapRatio;
        sample.slatRatio = snapshot.slatRatio;
        sample.aerodynamicAnglesValid = finite(snapshot.trueAirspeedMps) &&
                                        snapshot.trueAirspeedMps >= kMinimumAerodynamicAirspeedMps;
        if (!sample.aerodynamicAnglesValid) ++summary.lowSpeedAerodynamicRejectionCount;
        if (sample.paused) ++summary.pausedSampleCount;
        if (sample.replaying) ++summary.replaySampleCount;
        if ((snapshot.lifecycleFlags & engine::LifecycleManualMarker) != 0u) ++summary.manualMarkerCount;

        const Ecef ecef = geodeticToEcef(snapshot.latitudeDeg,
                                         snapshot.longitudeDeg,
                                         snapshot.elevationMslM);
        ecefDeltaToEnu(anchorEcef,
                       anchorLatitude,
                       anchorLongitude,
                       ecef,
                       sample.worldEastM,
                       sample.worldUpM,
                       sample.worldNorthM);

        if (previous) {
            double recordedDelta = snapshot.simulatorUptimeSeconds - previous->simulatorUptimeSeconds;
            if (!finite(recordedDelta) || recordedDelta < 0.0) {
                ++summary.nonFiniteInputCount;
                recordedDelta = 0.0;
            }
            if (!sample.paused && !sample.replaying) {
                sample.physicsDeltaSeconds = std::clamp(recordedDelta, 0.0, kMaximumPhysicsStepSeconds);
                normalizedSimulationTime += sample.physicsDeltaSeconds;
            }

            const double localJump = distance3(snapshot.localPositionM, previous->localPositionM);
            const double geodeticJump = geodeticDistanceM(previous->latitudeDeg,
                                                          previous->longitudeDeg,
                                                          snapshot.latitudeDeg,
                                                          snapshot.longitudeDeg);
            if (finite(localJump) && finite(geodeticJump) &&
                localJump > kLocalJumpThresholdM && geodeticJump < kGeodeticContinuityThresholdM) {
                sample.localOriginRebased = true;
                ++summary.localOriginRebaseCount;
            }

            const double timeDelta = snapshot.simulatorUptimeSeconds - previous->simulatorUptimeSeconds;
            if (finite(timeDelta) && timeDelta > 0.0001) {
                sample.verticalSpeedMps = static_cast<float>(
                    (snapshot.elevationMslM - previous->elevationMslM) / timeDelta);
            }
        }
        sample.simulationTimeSeconds = normalizedSimulationTime;

        const auto& profile = snapshot.atmosphere.profile;
        sample.dewPointK = interpolateProfile(profile.dewPointAltitudeMslM,
                                              profile.dewPointK,
                                              profile.dewPointLevelCount,
                                              static_cast<float>(snapshot.elevationMslM),
                                              snapshot.atmosphere.temperatureK);
        sample.relativeHumidityIcePercent = relativeHumidityIcePercent(sample.temperatureK,
                                                                       sample.dewPointK);

        if (snapshot.engineCount > 0) {
            const std::uint32_t count = std::min<std::uint32_t>(
                snapshot.engineCount, static_cast<std::uint32_t>(snapshot.engines.size()));
            double n1 = 0.0;
            double thrust = 0.0;
            for (std::uint32_t index = 0; index < count; ++index) {
                n1 += snapshot.engines[index].n1Percent;
                thrust += snapshot.engines[index].thrustN;
            }
            sample.meanEngineN1Percent = static_cast<float>(n1 / count);
            sample.meanEngineThrustN = static_cast<float>(thrust / count);
        }

        const bool sampleFinite = finite(sample.worldEastM) && finite(sample.worldUpM) &&
                                  finite(sample.worldNorthM) && finite(sample.altitudeMslM) &&
                                  finite(sample.temperatureK) && finite(sample.dewPointK) &&
                                  finite(sample.relativeHumidityIcePercent) &&
                                  finite(sample.meanEngineN1Percent) && finite(sample.meanEngineThrustN);
        if (!sampleFinite) ++summary.nonFiniteInputCount;

        summary.minimumAltitudeMslM = std::min(summary.minimumAltitudeMslM, sample.altitudeMslM);
        summary.maximumAltitudeMslM = std::max(summary.maximumAltitudeMslM, sample.altitudeMslM);
        summary.minimumTemperatureK = std::min(summary.minimumTemperatureK, sample.temperatureK);
        summary.maximumTemperatureK = std::max(summary.maximumTemperatureK, sample.temperatureK);
        summary.minimumRelativeHumidityIcePercent = std::min(
            summary.minimumRelativeHumidityIcePercent, sample.relativeHumidityIcePercent);
        summary.maximumRelativeHumidityIcePercent = std::max(
            summary.maximumRelativeHumidityIcePercent, sample.relativeHumidityIcePercent);
        summary.integratedPhysicsTimeSeconds += sample.physicsDeltaSeconds;
        summary.lastSimulatorTimeSeconds = snapshot.simulatorUptimeSeconds;

        hashSample(hash, sample);
        result.samples.push_back(sample);
        previous = &snapshot;
    }

    summary.normalizedSampleCount = result.samples.size();
    summary.deterministicHash = hash;
    summary.ok = summary.nonFiniteInputCount == 0;
    if (!summary.ok) summary.error = "Replay normalization encountered non-finite or invalid timing input";
    return result;
}

ReplayRunnerResult runReplayAnalysis(const std::filesystem::path& replayPath,
                                     std::size_t maximumSnapshots) {
    return runReplayAnalysis(readReplayFile(replayPath, maximumSnapshots));
}

bool writeReplayRunnerSummary(const ReplayRunnerResult& result,
                              const ReplayMetadata& metadata,
                              const std::filesystem::path& outputPath,
                              std::string* error) {
    if (!writeParentDirectories(outputPath, error)) return false;
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream) {
        if (error) *error = "Could not open replay-runner summary output";
        return false;
    }
    const auto& summary = result.summary;
    stream << "FFAtmo Offline Replay Runner Summary\n"
           << "status=" << (summary.ok ? "OK" : "ERROR") << '\n'
           << "error=" << summary.error << '\n'
           << "aircraft_name=" << metadata.aircraftName << '\n'
           << "aircraft_icao=" << metadata.aircraftIcao << '\n'
           << "aircraft_relative_path=" << metadata.aircraftRelativePath << '\n'
           << "input_snapshot_count=" << summary.inputSnapshotCount << '\n'
           << "normalized_sample_count=" << summary.normalizedSampleCount << '\n'
           << "paused_sample_count=" << summary.pausedSampleCount << '\n'
           << "replay_sample_count=" << summary.replaySampleCount << '\n'
           << "manual_marker_count=" << summary.manualMarkerCount << '\n'
           << "local_origin_rebase_count=" << summary.localOriginRebaseCount << '\n'
           << "low_speed_aerodynamic_rejection_count="
           << summary.lowSpeedAerodynamicRejectionCount << '\n'
           << "non_finite_input_count=" << summary.nonFiniteInputCount << '\n'
           << std::fixed << std::setprecision(6)
           << "first_simulator_time_seconds=" << summary.firstSimulatorTimeSeconds << '\n'
           << "last_simulator_time_seconds=" << summary.lastSimulatorTimeSeconds << '\n'
           << "integrated_physics_time_seconds=" << summary.integratedPhysicsTimeSeconds << '\n'
           << "minimum_altitude_msl_m=" << summary.minimumAltitudeMslM << '\n'
           << "maximum_altitude_msl_m=" << summary.maximumAltitudeMslM << '\n'
           << "minimum_temperature_k=" << summary.minimumTemperatureK << '\n'
           << "maximum_temperature_k=" << summary.maximumTemperatureK << '\n'
           << "minimum_relative_humidity_ice_percent="
           << summary.minimumRelativeHumidityIcePercent << '\n'
           << "maximum_relative_humidity_ice_percent="
           << summary.maximumRelativeHumidityIcePercent << '\n'
           << std::hex << std::setfill('0')
           << "deterministic_hash=0x" << std::setw(16) << summary.deterministicHash << '\n';
    if (!stream) {
        if (error) *error = "Could not write replay-runner summary";
        return false;
    }
    return true;
}

bool writeNormalizedReplayCsv(const ReplayRunnerResult& result,
                              const std::filesystem::path& outputPath,
                              std::string* error) {
    if (!writeParentDirectories(outputPath, error)) return false;
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream) {
        if (error) *error = "Could not open normalized replay CSV output";
        return false;
    }
    stream << "sequence,simulation_time_s,physics_dt_s,east_m,up_m,north_m,altitude_msl_m,"
              "tas_mps,vertical_speed_mps,temperature_k,dew_point_k,rhi_percent,mean_n1_percent,"
              "mean_thrust_n,flap_ratio,slat_ratio,paused,replaying,local_origin_rebased,"
              "aerodynamic_angles_valid\n";
    stream << std::fixed << std::setprecision(6);
    for (const auto& sample : result.samples) {
        stream << sample.sequenceNumber << ','
               << sample.simulationTimeSeconds << ','
               << sample.physicsDeltaSeconds << ','
               << sample.worldEastM << ','
               << sample.worldUpM << ','
               << sample.worldNorthM << ','
               << sample.altitudeMslM << ','
               << sample.trueAirspeedMps << ','
               << sample.verticalSpeedMps << ','
               << sample.temperatureK << ','
               << sample.dewPointK << ','
               << sample.relativeHumidityIcePercent << ','
               << sample.meanEngineN1Percent << ','
               << sample.meanEngineThrustN << ','
               << sample.flapRatio << ','
               << sample.slatRatio << ','
               << (sample.paused ? 1 : 0) << ','
               << (sample.replaying ? 1 : 0) << ','
               << (sample.localOriginRebased ? 1 : 0) << ','
               << (sample.aerodynamicAnglesValid ? 1 : 0) << '\n';
    }
    if (!stream) {
        if (error) *error = "Could not write normalized replay CSV";
        return false;
    }
    return true;
}

}  // namespace ffatmo::diagnostics
