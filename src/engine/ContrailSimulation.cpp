#include "engine/ContrailSimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>

namespace ffatmo::engine {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) {
    if (std::abs(edge1 - edge0) < 1.0e-6f) return value >= edge1 ? 1.0f : 0.0f;
    const float ratio = clamp01((value - edge0) / (edge1 - edge0));
    return ratio * ratio * (3.0f - 2.0f * ratio);
}

bool finite(double value) { return std::isfinite(value); }
bool finite(float value) { return std::isfinite(value); }

double distance(const Vec3d& a, const Vec3d& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float interpolateProfile(const std::array<float, kMaximumAtmosphereLevels>& altitude,
                         const std::array<float, kMaximumAtmosphereLevels>& values,
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
            const float ratio = clamp01((targetAltitude - altitude[index - 1]) / span);
            return values[index - 1] + (values[index] - values[index - 1]) * ratio;
        }
    }
    return values[count - 1];
}

float engineActivity(const EngineSnapshot& engine,
                     const ContrailSimulationSettings& settings) {
    if (engine.running == 0u) return 0.0f;
    return smoothstep(settings.minimumEngineN1Percent,
                      settings.fullEngineN1Percent,
                      engine.n1Percent);
}

float meanEngineActivity(const SimulatorSnapshot& snapshot,
                         const ContrailSimulationSettings& settings) {
    const std::uint32_t count = std::min<std::uint32_t>(
        snapshot.engineCount, static_cast<std::uint32_t>(snapshot.engines.size()));
    if (count == 0) return 0.0f;
    float total = 0.0f;
    for (std::uint32_t index = 0; index < count; ++index) {
        total += engineActivity(snapshot.engines[index], settings);
    }
    return total / static_cast<float>(count);
}

float formationPotential(const SimulatorSnapshot& snapshot,
                         const diagnostics::NormalizedReplaySample& sample,
                         const ContrailSimulationSettings& settings) {
    const float engineLoad = meanEngineActivity(snapshot, settings);
    if (engineLoad <= 0.0f) return 0.0f;

    const float altitudeGate = smoothstep(
        static_cast<float>(settings.minimumFormationAltitudeM),
        static_cast<float>(settings.fullFormationAltitudeM),
        static_cast<float>(sample.altitudeMslM));

    const float pressure = snapshot.atmosphere.staticPressurePa;
    const float pressureGate = smoothstep(60000.0f, 45000.0f, pressure);
    const float pressurePosition = clamp01((101325.0f - pressure) / (101325.0f - 20000.0f));

    // This is the first engine-tunable mixing-line approximation. It deliberately
    // produces a formation potential rather than claiming a fully calibrated
    // Schmidt-Appleman threshold. The pressure and engine-load terms will be
    // replaced by aircraft-specific efficiency once that contract is available.
    const float criticalTemperatureCeilingK =
        241.15f - 7.0f * pressurePosition + 3.0f * engineLoad;
    const float coldGate = smoothstep(criticalTemperatureCeilingK + 1.5f,
                                      criticalTemperatureCeilingK - 2.0f,
                                      sample.temperatureK);
    const float humidityGate = smoothstep(20.0f, 70.0f,
                                          sample.relativeHumidityIcePercent);

    return clamp01(engineLoad * altitudeGate * pressureGate * coldGate * humidityGate);
}

float dryLifetimeSeconds(float relativeHumidityIcePercent,
                         const ContrailSimulationSettings& settings) {
    const float humidityRatio = clamp01(relativeHumidityIcePercent / 100.0f);
    const float shaped = std::pow(humidityRatio, 2.2f);
    return settings.minimumDryLifetimeSeconds +
           (settings.maximumDryLifetimeSeconds - settings.minimumDryLifetimeSeconds) * shaped;
}

float engineLateralOffsetM(std::uint32_t engineIndex, std::uint32_t engineCount) {
    if (engineCount <= 1) return 0.0f;
    const float ratio = static_cast<float>(engineIndex) /
                        static_cast<float>(engineCount - 1);
    return (ratio - 0.5f) * 10.0f;
}

Vec3d emissionPosition(const diagnostics::NormalizedReplaySample& sample,
                       const SimulatorSnapshot& snapshot,
                       std::uint32_t engineIndex,
                       std::uint32_t engineCount) {
    const double headingRad = static_cast<double>(snapshot.headingDegTrue) * kPi / 180.0;
    const double lateral = static_cast<double>(engineLateralOffsetM(engineIndex, engineCount));
    Vec3d position;
    position.x = sample.worldEastM + std::cos(headingRad) * lateral;
    position.y = sample.worldUpM;
    position.z = sample.worldNorthM - std::sin(headingRad) * lateral;
    return position;
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

void hashTimelineSample(std::uint64_t& hash, const ContrailTimelineSample& sample) {
    hashValue(hash, sample.sequenceNumber);
    hashValue(hash, sample.simulationTimeSeconds);
    hashValue(hash, sample.physicsDeltaSeconds);
    hashValue(hash, sample.formationPotential);
    hashValue(hash, sample.relativeHumidityIcePercent);
    hashValue(hash, sample.temperatureK);
    hashValue(hash, sample.emittedParcelCount);
    hashValue(hash, sample.expiredParcelCount);
    hashValue(hash, sample.activeParcelCount);
    hashValue(hash, sample.totalNormalizedIceMass);
    hashValue(hash, sample.meanOpticalDepth);
    hashValue(hash, sample.meanRadiusM);
    hashValue(hash, sample.maximumParcelAgeSeconds);
    hashValue(hash, sample.visibleTrailLengthM);
    const std::uint8_t flags[] {
        static_cast<std::uint8_t>(sample.persistentEnvironment),
        static_cast<std::uint8_t>(sample.physicsFrozen)
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

void measureParcels(const std::vector<ContrailParcel>& parcels,
                    const ContrailSimulationSettings& settings,
                    ContrailTimelineSample& timeline) {
    if (parcels.empty()) return;

    std::array<Vec3d, kMaximumRecordedEngines> previousVisiblePosition {};
    std::array<bool, kMaximumRecordedEngines> hasPreviousVisible {};
    double opticalDepthTotal = 0.0;
    double radiusTotal = 0.0;

    for (const auto& parcel : parcels) {
        timeline.totalNormalizedIceMass += parcel.normalizedIceMass;
        opticalDepthTotal += parcel.opticalDepth;
        radiusTotal += parcel.radiusM;
        timeline.maximumParcelAgeSeconds = std::max(
            timeline.maximumParcelAgeSeconds, parcel.ageSeconds);

        if (parcel.opticalDepth < settings.minimumVisibleOpticalDepth ||
            parcel.engineIndex >= kMaximumRecordedEngines) {
            continue;
        }

        const std::size_t engine = static_cast<std::size_t>(parcel.engineIndex);
        if (hasPreviousVisible[engine]) {
            const double segmentLength = distance(previousVisiblePosition[engine], parcel.worldPositionM);
            if (finite(segmentLength) && segmentLength < 2000.0) {
                timeline.visibleTrailLengthM += segmentLength;
            }
        }
        previousVisiblePosition[engine] = parcel.worldPositionM;
        hasPreviousVisible[engine] = true;
    }

    const double count = static_cast<double>(parcels.size());
    timeline.meanOpticalDepth = static_cast<float>(opticalDepthTotal / count);
    timeline.meanRadiusM = static_cast<float>(radiusTotal / count);
}

}  // namespace

ContrailSimulationResult simulateContrails(
    const diagnostics::ReplayReadResult& replay,
    const diagnostics::ReplayRunnerResult& normalizedReplay,
    const ContrailSimulationSettings& settings) {
    ContrailSimulationResult result;
    auto& summary = result.summary;

    if (!replay.ok) {
        summary.error = replay.error.empty() ? "Replay input is invalid" : replay.error;
        return result;
    }
    if (!normalizedReplay.summary.ok) {
        summary.error = normalizedReplay.summary.error.empty()
            ? "Normalized replay input is invalid"
            : normalizedReplay.summary.error;
        return result;
    }
    if (replay.snapshots.size() != normalizedReplay.samples.size()) {
        summary.error = "Raw and normalized replay sample counts do not match";
        return result;
    }
    if (settings.emissionIntervalSeconds <= 0.0f ||
        settings.maximumActiveParcels == 0 ||
        settings.maximumParcelAgeSeconds <= 0.0f) {
        summary.error = "Contrail simulation settings are invalid";
        return result;
    }

    summary.inputSampleCount = replay.snapshots.size();
    result.timeline.reserve(replay.snapshots.size());

    std::vector<ContrailParcel> parcels;
    parcels.reserve(std::min<std::size_t>(settings.maximumActiveParcels, 20000));
    std::array<float, kMaximumRecordedEngines> emissionAccumulator {};
    std::uint64_t nextParcelId = 1;
    std::uint64_t hash = kFnvOffset;
    bool encounteredNonFinite = false;

    for (std::size_t sampleIndex = 0; sampleIndex < replay.snapshots.size(); ++sampleIndex) {
        const auto& snapshot = replay.snapshots[sampleIndex];
        const auto& normalized = normalizedReplay.samples[sampleIndex];

        ContrailTimelineSample timeline;
        timeline.sequenceNumber = normalized.sequenceNumber;
        timeline.simulationTimeSeconds = normalized.simulationTimeSeconds;
        timeline.physicsDeltaSeconds = normalized.physicsDeltaSeconds;
        timeline.relativeHumidityIcePercent = normalized.relativeHumidityIcePercent;
        timeline.temperatureK = normalized.temperatureK;
        timeline.persistentEnvironment = normalized.relativeHumidityIcePercent >= 100.0f;
        timeline.physicsFrozen = normalized.physicsDeltaSeconds <= 0.0 ||
                                 normalized.paused || normalized.replaying;
        timeline.formationPotential = formationPotential(snapshot, normalized, settings);

        summary.maximumFormationPotential = std::max(
            summary.maximumFormationPotential, timeline.formationPotential);
        if (timeline.persistentEnvironment) ++summary.persistentEnvironmentSampleCount;
        if (timeline.physicsFrozen) ++summary.frozenPhysicsSampleCount;

        const float deltaSeconds = static_cast<float>(
            std::clamp(normalized.physicsDeltaSeconds, 0.0, 0.25));

        if (deltaSeconds > 0.0f) {
            if (timeline.formationPotential > 0.01f) ++summary.formationSampleCount;

            const std::uint32_t engineCount = std::min<std::uint32_t>(
                snapshot.engineCount, static_cast<std::uint32_t>(snapshot.engines.size()));
            for (std::uint32_t engineIndex = 0; engineIndex < engineCount; ++engineIndex) {
                const float activity = engineActivity(snapshot.engines[engineIndex], settings);
                emissionAccumulator[engineIndex] +=
                    deltaSeconds * timeline.formationPotential * activity;

                while (emissionAccumulator[engineIndex] >= settings.emissionIntervalSeconds) {
                    emissionAccumulator[engineIndex] -= settings.emissionIntervalSeconds;
                    if (parcels.size() >= settings.maximumActiveParcels) {
                        ++summary.capacityDropCount;
                        continue;
                    }

                    ContrailParcel parcel;
                    parcel.id = nextParcelId++;
                    parcel.engineIndex = engineIndex;
                    parcel.worldPositionM = emissionPosition(
                        normalized, snapshot, engineIndex, engineCount);
                    parcel.radiusM = settings.initialParcelRadiusM;
                    parcel.sourceTemperatureK = normalized.temperatureK;
                    parcel.sourceRelativeHumidityIcePercent =
                        normalized.relativeHumidityIcePercent;

                    const float fuelFlowAssist = clamp01(
                        snapshot.engines[engineIndex].fuelFlowKgps / 1.5f);
                    const float exhaustAssist = clamp01(
                        snapshot.engines[engineIndex].exhaustVelocityMps / 450.0f);
                    parcel.normalizedIceMass = timeline.formationPotential *
                        (0.65f + 0.20f * fuelFlowAssist + 0.15f * exhaustAssist);
                    parcel.opticalDepth = clamp01(parcel.normalizedIceMass /
                                                   (0.5f + 0.35f * parcel.radiusM));
                    parcels.push_back(parcel);
                    ++timeline.emittedParcelCount;
                    ++summary.emittedParcelCount;
                }
            }

            const auto& profile = snapshot.atmosphere.profile;
            const float turbulence = std::clamp(interpolateProfile(
                profile.windAltitudeMslM,
                profile.turbulenceScale,
                profile.windLevelCount,
                static_cast<float>(normalized.altitudeMslM),
                0.0f), 0.0f, 1.0f);

            const float rhi = normalized.relativeHumidityIcePercent;
            const float lifetime = dryLifetimeSeconds(rhi, settings);
            const float supersaturation = std::max((rhi - 100.0f) / 100.0f, 0.0f);
            const double windEastMps = static_cast<double>(snapshot.atmosphere.windLocalMps.x);
            const double windUpMps = static_cast<double>(snapshot.atmosphere.windLocalMps.y);
            const double windNorthMps = -static_cast<double>(snapshot.atmosphere.windLocalMps.z);

            for (auto& parcel : parcels) {
                parcel.ageSeconds += deltaSeconds;
                parcel.worldPositionM.x += windEastMps * deltaSeconds;
                parcel.worldPositionM.y += windUpMps * deltaSeconds;
                parcel.worldPositionM.z += windNorthMps * deltaSeconds;

                const float wakeDescent = settings.initialWakeDescentMps *
                    std::exp(-parcel.ageSeconds / 28.0f);
                parcel.worldPositionM.y -= static_cast<double>(wakeDescent * deltaSeconds);

                const float ageSpread = clamp01(parcel.ageSeconds / 90.0f);
                parcel.radiusM += (settings.baseSpreadRateMps +
                                   0.42f * turbulence +
                                   0.18f * ageSpread) * deltaSeconds;

                if (rhi < 100.0f) {
                    parcel.normalizedIceMass *= std::exp(-deltaSeconds /
                                                        std::max(lifetime, 0.1f));
                } else {
                    parcel.normalizedIceMass *= std::exp(
                        settings.persistentGrowthRatePerSecond * supersaturation * deltaSeconds);
                    parcel.normalizedIceMass = std::min(parcel.normalizedIceMass, 4.0f);
                }

                parcel.opticalDepth = clamp01(parcel.normalizedIceMass /
                    (0.45f + 0.28f * parcel.radiusM + 0.015f * parcel.radiusM * parcel.radiusM));
            }

            const std::size_t beforeRemoval = parcels.size();
            parcels.erase(std::remove_if(parcels.begin(), parcels.end(),
                [&](const ContrailParcel& parcel) {
                    return parcel.ageSeconds > settings.maximumParcelAgeSeconds ||
                           parcel.normalizedIceMass < settings.minimumNormalizedIceMass ||
                           parcel.opticalDepth < settings.minimumVisibleOpticalDepth;
                }), parcels.end());
            timeline.expiredParcelCount = static_cast<std::uint64_t>(beforeRemoval - parcels.size());
            summary.expiredParcelCount += timeline.expiredParcelCount;
        }

        timeline.activeParcelCount = static_cast<std::uint64_t>(parcels.size());
        measureParcels(parcels, settings, timeline);

        if (timeline.emittedParcelCount > 0) {
            if (summary.firstFormationTimeSeconds < 0.0) {
                summary.firstFormationTimeSeconds = timeline.simulationTimeSeconds;
            }
            summary.lastFormationTimeSeconds = timeline.simulationTimeSeconds;
        }
        summary.peakActiveParcelCount = std::max(
            summary.peakActiveParcelCount, timeline.activeParcelCount);
        summary.maximumVisibleTrailLengthM = std::max(
            summary.maximumVisibleTrailLengthM, timeline.visibleTrailLengthM);
        summary.maximumTotalNormalizedIceMass = std::max(
            summary.maximumTotalNormalizedIceMass, timeline.totalNormalizedIceMass);
        summary.maximumParcelAgeSeconds = std::max(
            summary.maximumParcelAgeSeconds, timeline.maximumParcelAgeSeconds);

        const bool timelineFinite = finite(timeline.simulationTimeSeconds) &&
                                    finite(timeline.physicsDeltaSeconds) &&
                                    finite(timeline.formationPotential) &&
                                    finite(timeline.totalNormalizedIceMass) &&
                                    finite(timeline.meanOpticalDepth) &&
                                    finite(timeline.meanRadiusM) &&
                                    finite(timeline.maximumParcelAgeSeconds) &&
                                    finite(timeline.visibleTrailLengthM);
        if (!timelineFinite) encounteredNonFinite = true;

        hashTimelineSample(hash, timeline);
        result.timeline.push_back(timeline);
    }

    result.finalParcels = std::move(parcels);
    summary.deterministicHash = hash;
    summary.ok = !encounteredNonFinite &&
                 summary.emittedParcelCount >= summary.expiredParcelCount &&
                 result.timeline.size() == replay.snapshots.size();
    if (!summary.ok) {
        summary.error = encounteredNonFinite
            ? "Contrail simulation produced a non-finite timeline value"
            : "Contrail simulation parcel accounting failed";
    }
    return result;
}

bool writeContrailSimulationSummary(const ContrailSimulationResult& result,
                                    const diagnostics::ReplayMetadata& metadata,
                                    const std::filesystem::path& outputPath,
                                    std::string* error) {
    if (!writeParentDirectories(outputPath, error)) return false;
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream) {
        if (error) *error = "Could not open contrail simulation summary output";
        return false;
    }

    const auto& summary = result.summary;
    stream << "FFAtmo Contrail Formation and Non-Persistent Decay Summary\n"
           << "model=engine-v1-contrail-baseline-v1\n"
           << "status=" << (summary.ok ? "OK" : "ERROR") << '\n'
           << "error=" << summary.error << '\n'
           << "aircraft_name=" << metadata.aircraftName << '\n'
           << "aircraft_icao=" << metadata.aircraftIcao << '\n'
           << "aircraft_relative_path=" << metadata.aircraftRelativePath << '\n'
           << "input_sample_count=" << summary.inputSampleCount << '\n'
           << "emitted_parcel_count=" << summary.emittedParcelCount << '\n'
           << "expired_parcel_count=" << summary.expiredParcelCount << '\n'
           << "final_parcel_count=" << result.finalParcels.size() << '\n'
           << "peak_active_parcel_count=" << summary.peakActiveParcelCount << '\n'
           << "formation_sample_count=" << summary.formationSampleCount << '\n'
           << "persistent_environment_sample_count="
           << summary.persistentEnvironmentSampleCount << '\n'
           << "frozen_physics_sample_count=" << summary.frozenPhysicsSampleCount << '\n'
           << "capacity_drop_count=" << summary.capacityDropCount << '\n'
           << std::fixed << std::setprecision(6)
           << "first_formation_time_seconds=" << summary.firstFormationTimeSeconds << '\n'
           << "last_formation_time_seconds=" << summary.lastFormationTimeSeconds << '\n'
           << "maximum_visible_trail_length_m=" << summary.maximumVisibleTrailLengthM << '\n'
           << "maximum_total_normalized_ice_mass="
           << summary.maximumTotalNormalizedIceMass << '\n'
           << "maximum_parcel_age_seconds=" << summary.maximumParcelAgeSeconds << '\n'
           << "maximum_formation_potential=" << summary.maximumFormationPotential << '\n'
           << std::hex << std::setfill('0')
           << "deterministic_hash=0x" << std::setw(16)
           << summary.deterministicHash << '\n'
           << std::dec << std::setfill(' ')
           << "mass_units=normalized_visual_ice_mass_not_calibrated_kg\n";

    if (!stream) {
        if (error) *error = "Could not write contrail simulation summary";
        return false;
    }
    return true;
}

bool writeContrailTimelineCsv(const ContrailSimulationResult& result,
                              const std::filesystem::path& outputPath,
                              std::string* error) {
    if (!writeParentDirectories(outputPath, error)) return false;
    std::ofstream stream(outputPath, std::ios::trunc);
    if (!stream) {
        if (error) *error = "Could not open contrail timeline CSV output";
        return false;
    }

    stream << "sequence,simulation_time_s,physics_dt_s,formation_potential,rhi_percent,"
              "temperature_k,emitted,expired,active,total_normalized_ice_mass,"
              "mean_optical_depth,mean_radius_m,max_parcel_age_s,visible_trail_length_m,"
              "persistent_environment,physics_frozen\n";
    stream << std::fixed << std::setprecision(6);
    for (const auto& sample : result.timeline) {
        stream << sample.sequenceNumber << ','
               << sample.simulationTimeSeconds << ','
               << sample.physicsDeltaSeconds << ','
               << sample.formationPotential << ','
               << sample.relativeHumidityIcePercent << ','
               << sample.temperatureK << ','
               << sample.emittedParcelCount << ','
               << sample.expiredParcelCount << ','
               << sample.activeParcelCount << ','
               << sample.totalNormalizedIceMass << ','
               << sample.meanOpticalDepth << ','
               << sample.meanRadiusM << ','
               << sample.maximumParcelAgeSeconds << ','
               << sample.visibleTrailLengthM << ','
               << (sample.persistentEnvironment ? 1 : 0) << ','
               << (sample.physicsFrozen ? 1 : 0) << '\n';
    }

    if (!stream) {
        if (error) *error = "Could not write contrail timeline CSV";
        return false;
    }
    return true;
}

}  // namespace ffatmo::engine
