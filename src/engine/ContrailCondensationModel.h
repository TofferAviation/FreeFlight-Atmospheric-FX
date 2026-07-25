#pragma once

#include <algorithm>

namespace ffatmo::engine {

struct ContrailCondensationHandoff {
    float visibleStartSeconds = 0.12f;
    float fullOpacitySeconds = 0.60f;
};

// Deterministic live visual model for the exhaust cooling and ice-nucleation
// handoff. Colder, more ice-saturated air forms sooner; warmer/drier air and
// hotter exhaust delay the first visible crystals and lengthen the opacity ramp.
inline ContrailCondensationHandoff calculateContrailCondensationHandoff(
    float ambientTemperatureK,
    float relativeHumidityIcePercent,
    float staticPressurePa,
    float exhaustVelocityMps,
    float fuelFlowKgps,
    float trueAirspeedMps) {
    const float coldFactor = std::clamp(
        (241.15f - ambientTemperatureK) / 18.0f, 0.0f, 1.0f);
    const float warmFactor = 1.0f - coldFactor;
    const float dryness = std::clamp(
        (100.0f - relativeHumidityIcePercent) / 80.0f, 0.0f, 1.0f);
    const float exhaustHeat = std::clamp(
        (exhaustVelocityMps - 250.0f) / 300.0f, 0.0f, 1.0f);
    const float fuelHeat = std::clamp(
        (fuelFlowKgps - 0.50f) / 1.50f, 0.0f, 1.0f);
    const float pressureFactor = std::clamp(
        (staticPressurePa - 20000.0f) / 40000.0f, 0.0f, 1.0f);
    const float speedMixing = std::clamp(
        (trueAirspeedMps - 100.0f) / 180.0f, 0.0f, 1.0f);

    ContrailCondensationHandoff result;
    result.visibleStartSeconds = std::clamp(
        0.06f +
        0.18f * warmFactor +
        0.10f * dryness +
        0.035f * exhaustHeat +
        0.020f * fuelHeat +
        0.030f * pressureFactor -
        0.020f * speedMixing,
        0.08f,
        0.38f);

    const float rampDuration = std::clamp(
        0.24f +
        0.28f * dryness +
        0.18f * warmFactor +
        0.055f * exhaustHeat +
        0.035f * fuelHeat,
        0.22f,
        0.78f);
    result.fullOpacitySeconds = std::clamp(
        result.visibleStartSeconds + rampDuration,
        result.visibleStartSeconds + 0.20f,
        1.10f);
    return result;
}

inline float contrailNucleationOpacity(
    float plumeAgeSeconds,
    const ContrailCondensationHandoff& handoff) {
    const float span = std::max(
        handoff.fullOpacitySeconds - handoff.visibleStartSeconds, 0.001f);
    const float ratio = std::clamp(
        (plumeAgeSeconds - handoff.visibleStartSeconds) / span,
        0.0f,
        1.0f);
    return ratio * ratio * (3.0f - 2.0f * ratio);
}

}  // namespace ffatmo::engine
