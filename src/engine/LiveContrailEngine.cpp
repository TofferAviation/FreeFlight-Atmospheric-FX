#include "engine/LiveContrailEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ffatmo::engine {
namespace {

constexpr double kPi = 3.14159265358979323846;
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

double distance(const Vec3d& first, const Vec3d& second) {
    const double deltaX = first.x - second.x;
    const double deltaY = first.y - second.y;
    const double deltaZ = first.z - second.z;
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
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
    const float pressurePosition = clamp01(
        (101325.0f - pressure) / (101325.0f - 20000.0f));
    const float criticalTemperatureCeilingK =
        241.15f - 7.0f * pressurePosition + 3.0f * engineLoad;
    const float coldGate = smoothstep(criticalTemperatureCeilingK + 1.5f,
                                      criticalTemperatureCeilingK - 2.0f,
                                      sample.temperatureK);
    const float humidityGate = smoothstep(
        20.0f, 70.0f, sample.relativeHumidityIcePercent);
    return clamp01(engineLoad * altitudeGate * pressureGate * coldGate * humidityGate);
}

float dryLifetimeSeconds(float relativeHumidityIcePercent,
                         const ContrailSimulationSettings& settings) {
    const float humidityRatio = clamp01(relativeHumidityIcePercent / 100.0f);
    const float shaped = std::pow(humidityRatio, 2.2f);
    return settings.minimumDryLifetimeSeconds +
           (settings.maximumDryLifetimeSeconds - settings.minimumDryLifetimeSeconds) * shaped;
}

float fallbackEngineLateralOffsetM(std::uint32_t engineIndex,
                                   std::uint32_t engineCount) {
    if (engineCount <= 1) return 0.0f;
    const float ratio = static_cast<float>(engineIndex) /
                        static_cast<float>(engineCount - 1);
    return (ratio - 0.5f) * 10.0f;
}

Vec3d rotateBodyOffsetToEnu(const Vec3d& body,
                            float headingDeg,
                            float pitchDeg,
                            float rollDeg) {
    const double heading = static_cast<double>(headingDeg) * kPi / 180.0;
    const double pitch = static_cast<double>(pitchDeg) * kPi / 180.0;
    const double roll = static_cast<double>(rollDeg) * kPi / 180.0;
    const double cosRoll = std::cos(roll);
    const double sinRoll = std::sin(roll);
    const double cosPitch = std::cos(pitch);
    const double sinPitch = std::sin(pitch);
    const double cosHeading = std::cos(heading);
    const double sinHeading = std::sin(heading);

    // OBJ/ACF body axes are X right, Y up, Z aft. Apply positive X-Plane
    // roll (right wing down), pitch (nose up), and clockwise true heading.
    const double rollX = cosRoll * body.x + sinRoll * body.y;
    const double rollY = -sinRoll * body.x + cosRoll * body.y;
    const double rollZ = body.z;

    const double pitchX = rollX;
    const double pitchY = cosPitch * rollY - sinPitch * rollZ;
    const double pitchZ = sinPitch * rollY + cosPitch * rollZ;

    const double localX = cosHeading * pitchX - sinHeading * pitchZ;
    const double localY = pitchY;
    const double localZ = sinHeading * pitchX + cosHeading * pitchZ;

    // X-Plane local coordinates are east/up/south. Engine-world coordinates
    // are east/up/north, hence the sign change on local Z.
    return {localX, localY, -localZ};
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
            const double segmentLength = distance(
                previousVisiblePosition[engine], parcel.worldPositionM);
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

LiveContrailEngine::LiveContrailEngine(ContrailSimulationSettings settings)
    : settings_(settings) {
    reset();
}

void LiveContrailEngine::reset() {
    parcels_.clear();
    emissionAccumulator_.fill(0.0f);
    summary_ = {};
    lastTimeline_ = {};
    nextParcelId_ = 1;
    deterministicHash_ = 1469598103934665603ull;
    encounteredNonFinite_ = false;
    summary_.ok = true;
}

void LiveContrailEngine::setSettings(const ContrailSimulationSettings& settings) {
    settings_ = settings;
}

void LiveContrailEngine::setEngineExhaustBodyOffsets(
    const std::vector<Vec3d>& offsetsBodyM) {
    engineExhaustBodyOffsets_ = offsetsBodyM;
}

Vec3d LiveContrailEngine::emissionPosition(
    const diagnostics::NormalizedReplaySample& sample,
    const SimulatorSnapshot& snapshot,
    std::uint32_t engineIndex,
    std::uint32_t engineCount) const {
    Vec3d bodyOffset;
    if (engineIndex < engineExhaustBodyOffsets_.size()) {
        bodyOffset = engineExhaustBodyOffsets_[engineIndex];
    } else {
        bodyOffset.x = fallbackEngineLateralOffsetM(engineIndex, engineCount);
    }
    const Vec3d worldOffset = rotateBodyOffsetToEnu(
        bodyOffset, snapshot.headingDegTrue, snapshot.pitchDeg, snapshot.rollDeg);
    return {
        sample.worldEastM + worldOffset.x,
        sample.worldUpM + worldOffset.y,
        sample.worldNorthM + worldOffset.z
    };
}

ContrailTimelineSample LiveContrailEngine::step(
    const SimulatorSnapshot& snapshot,
    const diagnostics::NormalizedReplaySample& normalized) {
    ContrailTimelineSample timeline;
    timeline.sequenceNumber = normalized.sequenceNumber;
    timeline.simulationTimeSeconds = normalized.simulationTimeSeconds;
    timeline.physicsDeltaSeconds = normalized.physicsDeltaSeconds;
    timeline.relativeHumidityIcePercent = normalized.relativeHumidityIcePercent;
    timeline.temperatureK = normalized.temperatureK;
    timeline.persistentEnvironment = normalized.relativeHumidityIcePercent >= 100.0f;
    timeline.physicsFrozen = normalized.physicsDeltaSeconds <= 0.0 ||
                             normalized.paused || normalized.replaying;
    timeline.formationPotential = formationPotential(snapshot, normalized, settings_);

    ++summary_.inputSampleCount;
    summary_.maximumFormationPotential = std::max(
        summary_.maximumFormationPotential, timeline.formationPotential);
    if (timeline.persistentEnvironment) ++summary_.persistentEnvironmentSampleCount;
    if (timeline.physicsFrozen) ++summary_.frozenPhysicsSampleCount;

    const float deltaSeconds = static_cast<float>(
        std::clamp(normalized.physicsDeltaSeconds, 0.0, 0.25));
    if (deltaSeconds > 0.0f) {
        if (timeline.formationPotential > 0.01f) ++summary_.formationSampleCount;

        const std::uint32_t engineCount = std::min<std::uint32_t>(
            snapshot.engineCount, static_cast<std::uint32_t>(snapshot.engines.size()));
        for (std::uint32_t engineIndex = 0; engineIndex < engineCount; ++engineIndex) {
            const float activity = engineActivity(snapshot.engines[engineIndex], settings_);
            emissionAccumulator_[engineIndex] +=
                deltaSeconds * timeline.formationPotential * activity;

            while (emissionAccumulator_[engineIndex] >= settings_.emissionIntervalSeconds) {
                emissionAccumulator_[engineIndex] -= settings_.emissionIntervalSeconds;
                if (parcels_.size() >= settings_.maximumActiveParcels) {
                    ++summary_.capacityDropCount;
                    continue;
                }

                ContrailParcel parcel;
                parcel.id = nextParcelId_++;
                parcel.engineIndex = engineIndex;
                parcel.worldPositionM = emissionPosition(
                    normalized, snapshot, engineIndex, engineCount);
                parcel.radiusM = settings_.initialParcelRadiusM;
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
                parcels_.push_back(parcel);
                ++timeline.emittedParcelCount;
                ++summary_.emittedParcelCount;
            }
        }

        const auto& profile = snapshot.atmosphere.profile;
        const float turbulence = std::clamp(interpolateProfile(
            profile.windAltitudeMslM,
            profile.turbulenceScale,
            profile.windLevelCount,
            static_cast<float>(normalized.altitudeMslM),
            0.0f), 0.0f, 1.0f);
        const float relativeHumidityIce = normalized.relativeHumidityIcePercent;
        const float lifetime = dryLifetimeSeconds(relativeHumidityIce, settings_);
        const float supersaturation = std::max(
            (relativeHumidityIce - 100.0f) / 100.0f, 0.0f);
        const double windEastMps = snapshot.atmosphere.windLocalMps.x;
        const double windUpMps = snapshot.atmosphere.windLocalMps.y;
        const double windNorthMps = -snapshot.atmosphere.windLocalMps.z;

        for (auto& parcel : parcels_) {
            parcel.ageSeconds += deltaSeconds;
            parcel.worldPositionM.x += windEastMps * deltaSeconds;
            parcel.worldPositionM.y += windUpMps * deltaSeconds;
            parcel.worldPositionM.z += windNorthMps * deltaSeconds;

            const float wakeDescent = settings_.initialWakeDescentMps *
                std::exp(-parcel.ageSeconds / 28.0f);
            parcel.worldPositionM.y -= wakeDescent * deltaSeconds;

            const float ageSpread = clamp01(parcel.ageSeconds / 90.0f);
            parcel.radiusM += (settings_.baseSpreadRateMps +
                               0.42f * turbulence +
                               0.18f * ageSpread) * deltaSeconds;

            if (relativeHumidityIce < 100.0f) {
                parcel.normalizedIceMass *= std::exp(
                    -deltaSeconds / std::max(lifetime, 0.1f));
            } else {
                parcel.normalizedIceMass *= std::exp(
                    settings_.persistentGrowthRatePerSecond *
                    supersaturation * deltaSeconds);
                parcel.normalizedIceMass = std::min(parcel.normalizedIceMass, 4.0f);
            }

            parcel.opticalDepth = clamp01(parcel.normalizedIceMass /
                (0.45f + 0.28f * parcel.radiusM +
                 0.015f * parcel.radiusM * parcel.radiusM));
        }

        const std::size_t beforeRemoval = parcels_.size();
        parcels_.erase(std::remove_if(parcels_.begin(), parcels_.end(),
            [&](const ContrailParcel& parcel) {
                return parcel.ageSeconds > settings_.maximumParcelAgeSeconds ||
                       parcel.normalizedIceMass < settings_.minimumNormalizedIceMass ||
                       parcel.opticalDepth < settings_.minimumVisibleOpticalDepth;
            }), parcels_.end());
        timeline.expiredParcelCount = static_cast<std::uint64_t>(
            beforeRemoval - parcels_.size());
        summary_.expiredParcelCount += timeline.expiredParcelCount;
    }

    timeline.activeParcelCount = static_cast<std::uint64_t>(parcels_.size());
    measureParcels(parcels_, settings_, timeline);

    if (timeline.emittedParcelCount > 0) {
        if (summary_.firstFormationTimeSeconds < 0.0) {
            summary_.firstFormationTimeSeconds = timeline.simulationTimeSeconds;
        }
        summary_.lastFormationTimeSeconds = timeline.simulationTimeSeconds;
    }
    summary_.peakActiveParcelCount = std::max(
        summary_.peakActiveParcelCount, timeline.activeParcelCount);
    summary_.maximumVisibleTrailLengthM = std::max(
        summary_.maximumVisibleTrailLengthM, timeline.visibleTrailLengthM);
    summary_.maximumTotalNormalizedIceMass = std::max(
        summary_.maximumTotalNormalizedIceMass, timeline.totalNormalizedIceMass);
    summary_.maximumParcelAgeSeconds = std::max(
        summary_.maximumParcelAgeSeconds, timeline.maximumParcelAgeSeconds);

    const bool timelineFinite = finite(timeline.simulationTimeSeconds) &&
                                finite(timeline.physicsDeltaSeconds) &&
                                finite(timeline.formationPotential) &&
                                finite(timeline.totalNormalizedIceMass) &&
                                finite(timeline.meanOpticalDepth) &&
                                finite(timeline.meanRadiusM) &&
                                finite(timeline.maximumParcelAgeSeconds) &&
                                finite(timeline.visibleTrailLengthM);
    if (!timelineFinite) encounteredNonFinite_ = true;

    hashTimelineSample(deterministicHash_, timeline);
    summary_.deterministicHash = deterministicHash_;
    summary_.ok = !encounteredNonFinite_ &&
                  summary_.emittedParcelCount >= summary_.expiredParcelCount;
    if (!summary_.ok) {
        summary_.error = encounteredNonFinite_
            ? "Live contrail simulation produced a non-finite value"
            : "Live contrail parcel accounting failed";
    }

    lastTimeline_ = timeline;
    return timeline;
}

}  // namespace ffatmo::engine
