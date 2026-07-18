#include "AtmosphereModel.h"

#include <algorithm>
#include <cmath>

namespace ffatmo {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float response(float current, float target, float dt, float riseSeconds, float fallSeconds) {
    const float timeConstant = target > current ? riseSeconds : fallSeconds;
    const float alpha = 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(timeConstant, 0.01f));
    return current + (target - current) * alpha;
}

float qualityBudget(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::Low: return 0.42f;
        case QualityPreset::Medium: return 0.68f;
        case QualityPreset::High: return 1.0f;
        case QualityPreset::Ultra: return 1.28f;
    }
    return 1.0f;
}

float qualityPersistenceCap(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::Low: return 90.0f;
        case QualityPreset::Medium: return 180.0f;
        case QualityPreset::High: return 300.0f;
        case QualityPreset::Ultra: return 420.0f;
    }
    return 300.0f;
}

float engineAvailability(float n1Percent, bool burning) {
    if (!burning) {
        return 0.0f;
    }
    return smoothstep(18.0f, 68.0f, n1Percent);
}

}  // namespace

float AtmosphereModel::saturationVapourPressureWaterHpa(float temperatureC) {
    // Buck (1981) equation; stable over the atmospheric range used here.
    return 6.1121f * std::exp((18.678f - temperatureC / 234.5f) *
                             (temperatureC / (257.14f + temperatureC)));
}

float AtmosphereModel::saturationVapourPressureIceHpa(float temperatureC) {
    return 6.1115f * std::exp((23.036f - temperatureC / 333.7f) *
                             (temperatureC / (279.82f + temperatureC)));
}

EffectState AtmosphereModel::update(const AtmosphereInput& input,
                                    const EffectSettings& settings,
                                    float deltaSeconds) {
    EffectState target {};
    vortexPhase_ = std::fmod(vortexPhase_ + std::max(deltaSeconds, 0.0f) * 0.38f, 1.0f);
    target.vortexPhase = vortexPhase_;

    const float esWater = std::max(saturationVapourPressureWaterHpa(input.ambientTemperatureC), 0.001f);
    const float esIce = std::max(saturationVapourPressureIceHpa(input.ambientTemperatureC), 0.001f);

    float vapourPressureHpa = 0.5f * esWater;
    if (input.hasDewPoint) {
        vapourPressureHpa = saturationVapourPressureWaterHpa(
            std::min(input.dewPointC, input.ambientTemperatureC));
    } else if (input.hasRelativeHumidity) {
        vapourPressureHpa = clamp01(input.relativeHumidityWaterPercent / 100.0f) * esWater;
    }

    target.relativeHumidityWaterPercent = std::clamp(100.0f * vapourPressureHpa / esWater, 0.0f, 150.0f);
    target.relativeHumidityIcePercent = std::clamp(100.0f * vapourPressureHpa / esIce, 0.0f, 200.0f);

    const float pressurePosition = clamp01((101325.0f - input.pressurePa) / (101325.0f - 20000.0f));
    target.criticalContrailTemperatureC = -36.0f - 13.0f * pressurePosition;

    const float coldGate = smoothstep(target.criticalContrailTemperatureC + 4.0f,
                                      target.criticalContrailTemperatureC - 5.0f,
                                      input.ambientTemperatureC);
    const float formationHumidity = smoothstep(55.0f, 92.0f, target.relativeHumidityIcePercent);
    const float altitudeGate = smoothstep(settings.minimumContrailAltitudeFt - 5000.0f,
                                          settings.minimumContrailAltitudeFt + 1500.0f,
                                          input.altitudeFt);
    target.formationProbability = clamp01(coldGate * formationHumidity * altitudeGate);
    target.persistenceProbability = smoothstep(96.0f, 114.0f, target.relativeHumidityIcePercent);

    float persistenceSeconds = 8.0f;
    if (target.relativeHumidityIcePercent >= 96.0f) {
        persistenceSeconds = 30.0f + 70.0f * smoothstep(96.0f, 101.0f, target.relativeHumidityIcePercent);
    }
    if (target.relativeHumidityIcePercent >= 101.0f) {
        persistenceSeconds = 100.0f + 200.0f * smoothstep(101.0f, 114.0f, target.relativeHumidityIcePercent);
    }
    persistenceSeconds = std::min(persistenceSeconds * std::clamp(settings.persistenceScale, 0.25f, 2.0f),
                                  qualityPersistenceCap(settings.quality));

    float adaptiveBudget = qualityBudget(settings.quality);
    if (settings.adaptiveQuality && input.frameRateFps > 1.0f &&
        input.frameRateFps < settings.adaptiveTargetFps) {
        adaptiveBudget *= std::clamp(input.frameRateFps / std::max(settings.adaptiveTargetFps, 10.0f),
                                     0.45f, 1.0f);
    }
    target.particleBudgetRatio = adaptiveBudget;

    for (int engine = 0; engine < 2; ++engine) {
        const float available = engineAvailability(input.n1Percent[engine], input.engineBurning[engine]);
        target.engineContrailRatio[engine] = target.formationProbability * available *
            std::clamp(settings.contrailIntensity, 0.0f, 2.0f);
    }

    const float bothEngines = 0.5f * (target.engineContrailRatio[0] + target.engineContrailRatio[1]);
    target.contrailCoreRatio = clamp01(bothEngines * (1.0f - 0.32f * target.persistenceProbability));
    target.contrailSpreadRatio = clamp01(bothEngines * (0.30f + 0.70f * target.persistenceProbability));
    target.contrailPersistenceSeconds = bothEngines > 0.01f ? persistenceSeconds : 0.0f;

    // Young contrails are first captured by the aircraft wake. The primary
    // counter-rotating pair descends while persistent ice detrains into a
    // secondary curtain; only ice-supersaturated air grows into contrail cirrus.
    const float liftWake = smoothstep(0.45f, 1.05f, std::abs(input.normalG)) *
                           smoothstep(65.0f, 150.0f, input.trueAirspeedMps);
    target.wakeCaptureRatio = clamp01(bothEngines * (0.62f + 0.38f * liftWake));
    target.primaryWakeRatio = clamp01(target.wakeCaptureRatio *
        (0.82f + 0.18f * (1.0f - target.persistenceProbability)));
    target.secondaryCurtainRatio = clamp01(bothEngines * target.persistenceProbability *
        (0.58f + 0.42f * liftWake));
    target.contrailCirrusRatio = clamp01(bothEngines * target.persistenceProbability);
    target.wakeVortexLifetimeSeconds = bothEngines > 0.01f
        ? 35.0f + 95.0f * smoothstep(70.0f, 112.0f, target.relativeHumidityIcePercent)
        : 0.0f;

    const float wingHumidity = smoothstep(72.0f, 100.0f, target.relativeHumidityWaterPercent);
    const float liftDemand = clamp01((std::abs(input.angleOfAttackDeg) - 1.5f) / 7.0f +
                                     std::max(input.normalG - 1.0f, 0.0f) * 0.48f +
                                     input.flapDeployRatio * 0.22f);
    const float speedGate = smoothstep(55.0f, 125.0f, input.trueAirspeedMps);
    const float temperatureGate = smoothstep(28.0f, 8.0f, input.ambientTemperatureC);
    const float precipAssist = 0.80f + 0.20f * clamp01(input.precipitationRatio);

    target.wingCondensationRatio = clamp01(wingHumidity * liftDemand * speedGate * temperatureGate * precipAssist *
        std::clamp(settings.wingCondensationIntensity, 0.0f, 2.0f));
    target.wingVortexRatio = clamp01(wingHumidity * (0.35f + 0.65f * liftDemand) * speedGate *
        (0.55f + 0.45f * input.flapDeployRatio) *
        std::clamp(settings.wingVortexIntensity, 0.0f, 2.0f));

    if (settings.previewMode && settings.enabled) {
        target.formationProbability = 1.0f;
        target.persistenceProbability = 0.85f;
        target.engineContrailRatio = {0.90f, 0.90f};
        target.contrailCoreRatio = 0.78f;
        target.contrailSpreadRatio = 0.88f;
        target.contrailPersistenceSeconds = std::min(240.0f * settings.persistenceScale,
                                                     qualityPersistenceCap(settings.quality));
        target.wakeCaptureRatio = 0.92f;
        target.primaryWakeRatio = 0.88f;
        target.secondaryCurtainRatio = 0.78f;
        target.contrailCirrusRatio = 0.84f;
        target.wakeVortexLifetimeSeconds = 120.0f;
        target.wingCondensationRatio = 0.82f;
        target.wingVortexRatio = 0.72f;
    }

    if (!settings.enabled) {
        target.formationProbability = 0.0f;
        target.persistenceProbability = 0.0f;
        target.engineContrailRatio = {0.0f, 0.0f};
        target.contrailCoreRatio = 0.0f;
        target.contrailSpreadRatio = 0.0f;
        target.contrailPersistenceSeconds = 0.0f;
        target.wakeCaptureRatio = 0.0f;
        target.primaryWakeRatio = 0.0f;
        target.secondaryCurtainRatio = 0.0f;
        target.contrailCirrusRatio = 0.0f;
        target.wakeVortexLifetimeSeconds = 0.0f;
        target.wingCondensationRatio = 0.0f;
        target.wingVortexRatio = 0.0f;
    }

    if (!initialized_) {
        smoothed_ = target;
        initialized_ = true;
    } else {
        smoothed_.relativeHumidityWaterPercent = response(smoothed_.relativeHumidityWaterPercent,
            target.relativeHumidityWaterPercent, deltaSeconds, 2.0f, 2.0f);
        smoothed_.relativeHumidityIcePercent = response(smoothed_.relativeHumidityIcePercent,
            target.relativeHumidityIcePercent, deltaSeconds, 2.0f, 2.0f);
        smoothed_.criticalContrailTemperatureC = target.criticalContrailTemperatureC;
        smoothed_.formationProbability = response(smoothed_.formationProbability,
            target.formationProbability, deltaSeconds, 2.2f, 7.5f);
        smoothed_.persistenceProbability = response(smoothed_.persistenceProbability,
            target.persistenceProbability, deltaSeconds, 3.0f, 12.0f);
        for (int engine = 0; engine < 2; ++engine) {
            smoothed_.engineContrailRatio[engine] = response(smoothed_.engineContrailRatio[engine],
                target.engineContrailRatio[engine], deltaSeconds, 1.8f, 6.0f);
        }
        smoothed_.contrailCoreRatio = response(smoothed_.contrailCoreRatio,
            target.contrailCoreRatio, deltaSeconds, 1.4f, 5.0f);
        smoothed_.contrailSpreadRatio = response(smoothed_.contrailSpreadRatio,
            target.contrailSpreadRatio, deltaSeconds, 3.0f, 14.0f);
        smoothed_.contrailPersistenceSeconds = response(smoothed_.contrailPersistenceSeconds,
            target.contrailPersistenceSeconds, deltaSeconds, 5.0f, 20.0f);
        smoothed_.wakeCaptureRatio = response(smoothed_.wakeCaptureRatio,
            target.wakeCaptureRatio, deltaSeconds, 1.8f, 7.0f);
        smoothed_.primaryWakeRatio = response(smoothed_.primaryWakeRatio,
            target.primaryWakeRatio, deltaSeconds, 2.5f, 10.0f);
        smoothed_.secondaryCurtainRatio = response(smoothed_.secondaryCurtainRatio,
            target.secondaryCurtainRatio, deltaSeconds, 4.0f, 16.0f);
        smoothed_.contrailCirrusRatio = response(smoothed_.contrailCirrusRatio,
            target.contrailCirrusRatio, deltaSeconds, 6.0f, 24.0f);
        smoothed_.wakeVortexLifetimeSeconds = response(smoothed_.wakeVortexLifetimeSeconds,
            target.wakeVortexLifetimeSeconds, deltaSeconds, 5.0f, 15.0f);
        smoothed_.vortexPhase = target.vortexPhase;
        smoothed_.wingCondensationRatio = response(smoothed_.wingCondensationRatio,
            target.wingCondensationRatio, deltaSeconds, 0.35f, 1.8f);
        smoothed_.wingVortexRatio = response(smoothed_.wingVortexRatio,
            target.wingVortexRatio, deltaSeconds, 0.45f, 2.6f);
        smoothed_.particleBudgetRatio = response(smoothed_.particleBudgetRatio,
            target.particleBudgetRatio, deltaSeconds, 3.0f, 1.5f);
    }

    smoothed_.estimatedTrailLengthKm = input.trueAirspeedMps *
        smoothed_.contrailPersistenceSeconds / 1000.0f;
    return smoothed_;
}

void AtmosphereModel::reset() {
    smoothed_ = {};
    initialized_ = false;
    vortexPhase_ = 0.0f;
}

}  // namespace ffatmo
