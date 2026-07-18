#pragma once

#include <array>

namespace ffatmo {

enum class QualityPreset : int {
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3,
};

struct EffectSettings {
    bool enabled = true;
    bool automaticWeather = true;
    bool previewMode = false;
    QualityPreset quality = QualityPreset::High;

    float contrailIntensity = 1.0f;
    float wingCondensationIntensity = 1.0f;
    float wingVortexIntensity = 1.0f;
    float persistenceScale = 1.0f;
    float minimumContrailAltitudeFt = 18000.0f;
    float adaptiveTargetFps = 30.0f;
    bool adaptiveQuality = true;
};

struct AtmosphereInput {
    float ambientTemperatureC = 15.0f;
    float dewPointC = 5.0f;
    float relativeHumidityWaterPercent = 50.0f;
    float pressurePa = 101325.0f;
    float altitudeFt = 0.0f;
    float trueAirspeedMps = 0.0f;
    float mach = 0.0f;
    float angleOfAttackDeg = 0.0f;
    float normalG = 1.0f;
    float flapDeployRatio = 0.0f;
    float precipitationRatio = 0.0f;
    float turbulenceRatio = 0.0f;
    float frameRateFps = 60.0f;
    std::array<float, 2> n1Percent {0.0f, 0.0f};
    std::array<bool, 2> engineBurning {false, false};
    bool hasDewPoint = false;
    bool hasRelativeHumidity = false;
};

struct EffectState {
    float relativeHumidityWaterPercent = 0.0f;
    float relativeHumidityIcePercent = 0.0f;
    float criticalContrailTemperatureC = -40.0f;
    float formationProbability = 0.0f;
    float persistenceProbability = 0.0f;

    std::array<float, 2> engineContrailRatio {0.0f, 0.0f};
    float contrailCoreRatio = 0.0f;
    float contrailSpreadRatio = 0.0f;
    float contrailPersistenceSeconds = 0.0f;
    float estimatedTrailLengthKm = 0.0f;
    float wakeCaptureRatio = 0.0f;
    float primaryWakeRatio = 0.0f;
    float secondaryCurtainRatio = 0.0f;
    float contrailCirrusRatio = 0.0f;
    float wakeVortexLifetimeSeconds = 0.0f;
    float vortexPhase = 0.0f;
    float wingCondensationRatio = 0.0f;
    float wingVortexRatio = 0.0f;
    float particleBudgetRatio = 1.0f;
};

class AtmosphereModel {
public:
    EffectState update(const AtmosphereInput& input,
                       const EffectSettings& settings,
                       float deltaSeconds);

    void reset();

    static float saturationVapourPressureWaterHpa(float temperatureC);
    static float saturationVapourPressureIceHpa(float temperatureC);

private:
    EffectState smoothed_ {};
    bool initialized_ = false;
    float vortexPhase_ = 0.0f;
};

}  // namespace ffatmo
