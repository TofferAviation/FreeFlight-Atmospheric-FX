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

float interpolateDirectionProfile(
    const std::array<float, kMaximumAtmosphereLevels>& altitude,
    const std::array<float, kMaximumAtmosphereLevels>& directionDeg,
    std::uint32_t count,
    float targetAltitude,
    float fallbackDeg) {
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(altitude.size()));
    if (count == 0) return fallbackDeg;
    if (count == 1 || targetAltitude <= altitude[0]) return directionDeg[0];
    for (std::uint32_t index = 1; index < count; ++index) {
        if (targetAltitude <= altitude[index]) {
            const float span = altitude[index] - altitude[index - 1];
            if (std::abs(span) < 0.001f) return directionDeg[index];
            const float ratio = clamp01((targetAltitude - altitude[index - 1]) / span);
            const double first = static_cast<double>(directionDeg[index - 1]) * kPi / 180.0;
            const double second = static_cast<double>(directionDeg[index]) * kPi / 180.0;
            const double x = (1.0 - ratio) * std::sin(first) + ratio * std::sin(second);
            const double z = (1.0 - ratio) * std::cos(first) + ratio * std::cos(second);
            double angle = std::atan2(x, z) * 180.0 / kPi;
            if (angle < 0.0) angle += 360.0;
            return static_cast<float>(angle);
        }
    }
    return directionDeg[count - 1];
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

    const double rollX = cosRoll * body.x + sinRoll * body.y;
    const double rollY = -sinRoll * body.x + cosRoll * body.y;
    const double rollZ = body.z;
    const double pitchX = rollX;
    const double pitchY = cosPitch * rollY - sinPitch * rollZ;
    const double pitchZ = sinPitch * rollY + cosPitch * rollZ;
    const double localX = cosHeading * pitchX - sinHeading * pitchZ;
    const double localY = pitchY;
    const double localZ = sinHeading * pitchX + cosHeading * pitchZ;
    return {localX, localY, -localZ};
}

Vec3d shearVelocityWorld(float speedMps, float directionDegTrue) {
    if (!finite(speedMps) || speedMps <= 0.0f || !finite(directionDegTrue)) return {};
    const double radians = static_cast<double>(directionDegTrue) * kPi / 180.0;
    return {
        static_cast<double>(speedMps) * std::sin(radians),
        0.0,
        static_cast<double>(speedMps) * std::cos(radians)
    };
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
    hashValue(hash, sample.meanWakeCirculationM2ps);
    hashValue(hash, sample.meanWakeCoreRadiusM);
    hashValue(hash, sample.meanWakeInducedSpeedMps);
    hashValue(hash, sample.meanWakeDescentMps);
    hashValue(hash, sample.maximumWakeLateralDisplacementM);
    hashValue(hash, sample.maximumWakeVerticalDisplacementM);
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
    double circulationTotal = 0.0;
    double coreRadiusTotal = 0.0;
    double inducedSpeedTotal = 0.0;
    double descentTotal = 0.0;
    std::size_t wakeCount = 0;

    for (const auto& parcel : parcels) {
        timeline.totalNormalizedIceMass += parcel.normalizedIceMass;
        opticalDepthTotal += parcel.opticalDepth;
        radiusTotal += parcel.radiusM;
        timeline.maximumParcelAgeSeconds = std::max(
            timeline.maximumParcelAgeSeconds, parcel.ageSeconds);

        if (parcel.wakeFluid.initialized) {
            circulationTotal += parcel.wakeFluid.circulationM2ps;
            coreRadiusTotal += parcel.wakeFluid.coreRadiusM;
            inducedSpeedTotal += parcel.wakeFluid.inducedSpeedMps;
            descentTotal += parcel.wakeFluid.wakeDescentMps;
            ++wakeCount;
            timeline.maximumWakeLateralDisplacementM = std::max(
                timeline.maximumWakeLateralDisplacementM,
                std::abs(parcel.wakeFluid.lateralM - parcel.wakeFluid.initialLateralM));
            timeline.maximumWakeVerticalDisplacementM = std::max(
                timeline.maximumWakeVerticalDisplacementM,
                std::abs(parcel.wakeFluid.verticalM - parcel.wakeFluid.initialVerticalM));
        }

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
    if (wakeCount > 0) {
        const double wakeCountDouble = static_cast<double>(wakeCount);
        timeline.meanWakeCirculationM2ps = static_cast<float>(
            circulationTotal / wakeCountDouble);
        timeline.meanWakeCoreRadiusM = static_cast<float>(
            coreRadiusTotal / wakeCountDouble);
        timeline.meanWakeInducedSpeedMps = static_cast<float>(
            inducedSpeedTotal / wakeCountDouble);
        timeline.meanWakeDescentMps = static_cast<float>(
            descentTotal / wakeCountDouble);
    }
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

void LiveContrailEngine::setWakeAircraftGeometry(float wingspanM, float referenceMassKg) {
    wakeAircraft_.wingspanM = std::max(wingspanM, 0.0f);
    wakeAircraft_.referenceMassKg = std::max(referenceMassKg, 0.0f);
}

void LiveContrailEngine::setWakeFluidSettings(const WakeFluidSettings& settings) {
    wakeFluidSettings_ = settings;
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

        const auto& profile = snapshot.atmosphere.profile;
        const float targetAltitude = static_cast<float>(normalized.altitudeMslM);
        const float turbulence = std::clamp(interpolateProfile(
            profile.windAltitudeMslM,
            profile.turbulenceScale,
            profile.windLevelCount,
            targetAltitude,
            0.0f), 0.0f, 1.0f);
        const float shearSpeed = std::max(0.0f, interpolateProfile(
            profile.windAltitudeMslM,
            profile.shearSpeedMps,
            profile.windLevelCount,
            targetAltitude,
            0.0f));
        const float shearDirection = interpolateDirectionProfile(
            profile.windAltitudeMslM,
            profile.shearDirectionDegTrue,
            profile.windLevelCount,
            targetAltitude,
            snapshot.headingDegTrue);

        WakeFluidEnvironment wakeEnvironment;
        wakeEnvironment.aircraftMassKg = snapshot.totalMassKg;
        wakeEnvironment.trueAirspeedMps = snapshot.trueAirspeedMps > 1.0f
            ? snapshot.trueAirspeedMps
            : normalized.trueAirspeedMps;
        wakeEnvironment.airDensityKgM3 = snapshot.atmosphere.densityKgM3;
        wakeEnvironment.gravityMps2 = snapshot.atmosphere.gravityMps2 > 1.0f
            ? snapshot.atmosphere.gravityMps2
            : 9.80665f;
        wakeEnvironment.normalLoadFactorG = snapshot.normalLoadFactorG > 0.05f
            ? snapshot.normalLoadFactorG
            : 1.0f;
        wakeEnvironment.turbulenceScale = turbulence;
        wakeEnvironment.shearVelocityWorldMps = shearVelocityWorld(
            shearSpeed, shearDirection);

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

                Vec3d bodyOffset;
                if (engineIndex < engineExhaustBodyOffsets_.size()) {
                    bodyOffset = engineExhaustBodyOffsets_[engineIndex];
                } else {
                    bodyOffset.x = fallbackEngineLateralOffsetM(engineIndex, engineCount);
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

                const Vec3d wakeRight = rotateBodyOffsetToEnu(
                    {1.0, 0.0, 0.0},
                    snapshot.headingDegTrue,
                    snapshot.pitchDeg,
                    snapshot.rollDeg);
                const Vec3d wakeUp = rotateBodyOffsetToEnu(
                    {0.0, 1.0, 0.0},
                    snapshot.headingDegTrue,
                    snapshot.pitchDeg,
                    snapshot.rollDeg);
                parcel.wakeFluid = initializeWakeFluidState(
                    wakeAircraft_,
                    wakeEnvironment,
                    wakeRight,
                    wakeUp,
                    static_cast<float>(bodyOffset.x),
                    static_cast<float>(bodyOffset.y),
                    wakeFluidSettings_);
                parcel.vortexRightWorld = parcel.wakeFluid.rightWorld;
                parcel.vortexSide = bodyOffset.x < -0.05 ? -1.0f :
                                    (bodyOffset.x > 0.05 ? 1.0f : 0.0f);
                summary_.maximumWakeInitialCirculationM2ps = std::max(
                    summary_.maximumWakeInitialCirculationM2ps,
                    parcel.wakeFluid.initialCirculationM2ps);

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

            const WakeFluidStepResult wakeStep = advanceWakeFluidState(
                parcel.wakeFluid,
                parcel.id,
                parcel.ageSeconds,
                deltaSeconds,
                wakeEnvironment,
                wakeFluidSettings_);
            if (!wakeStep.finite) {
                encounteredNonFinite_ = true;
            } else if (parcel.wakeFluid.initialized) {
                parcel.worldPositionM.x += wakeStep.worldDisplacementM.x;
                parcel.worldPositionM.y += wakeStep.worldDisplacementM.y;
                parcel.worldPositionM.z += wakeStep.worldDisplacementM.z;
                ++summary_.wakeFluidStepCount;
                summary_.maximumWakeInducedSpeedMps = std::max(
                    summary_.maximumWakeInducedSpeedMps, wakeStep.inducedSpeedMps);
                summary_.maximumWakeDescentMps = std::max(
                    summary_.maximumWakeDescentMps, wakeStep.wakeDescentMps);
                summary_.maximumWakeCoreRadiusM = std::max(
                    summary_.maximumWakeCoreRadiusM, wakeStep.coreRadiusM);
            }

            parcel.appliedVortexLateralM =
                parcel.wakeFluid.lateralM - parcel.wakeFluid.initialLateralM;
            parcel.appliedVortexVerticalM =
                parcel.wakeFluid.verticalM - parcel.wakeFluid.initialVerticalM;
            summary_.maximumWakeLateralDisplacementM = std::max(
                summary_.maximumWakeLateralDisplacementM,
                std::abs(parcel.appliedVortexLateralM));
            summary_.maximumWakeVerticalDisplacementM = std::max(
                summary_.maximumWakeVerticalDisplacementM,
                std::abs(parcel.appliedVortexVerticalM));

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
                                finite(timeline.visibleTrailLengthM) &&
                                finite(timeline.meanWakeCirculationM2ps) &&
                                finite(timeline.meanWakeCoreRadiusM) &&
                                finite(timeline.meanWakeInducedSpeedMps) &&
                                finite(timeline.meanWakeDescentMps) &&
                                finite(timeline.maximumWakeLateralDisplacementM) &&
                                finite(timeline.maximumWakeVerticalDisplacementM);
    if (!timelineFinite) encounteredNonFinite_ = true;

    hashTimelineSample(deterministicHash_, timeline);
    summary_.deterministicHash = deterministicHash_;
    summary_.ok = !encounteredNonFinite_ &&
                  summary_.emittedParcelCount >= summary_.expiredParcelCount;
    if (!summary_.ok) {
        summary_.error = encounteredNonFinite_
            ? "Wake Fluid v1 produced a non-finite value"
            : "Live contrail parcel accounting failed";
    }

    lastTimeline_ = timeline;
    return timeline;
}

}  // namespace ffatmo::engine
