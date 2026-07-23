#include "engine/WakeFluidSolver.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ffatmo::engine {
namespace {

constexpr double kPi = 3.14159265358979323846;

bool finite(double value) { return std::isfinite(value); }
bool finite(float value) { return std::isfinite(value); }

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) {
    if (std::abs(edge1 - edge0) <= 1.0e-6f) return value >= edge1 ? 1.0f : 0.0f;
    const float ratio = clamp01((value - edge0) / (edge1 - edge0));
    return ratio * ratio * (3.0f - 2.0f * ratio);
}

double dot(const Vec3d& a, const Vec3d& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double magnitude(const Vec3d& value) {
    return std::sqrt(dot(value, value));
}

Vec3d multiply(const Vec3d& value, double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3d add(const Vec3d& a, const Vec3d& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3d normalized(const Vec3d& value, const Vec3d& fallback) {
    const double length = magnitude(value);
    if (!finite(length) || length <= 1.0e-9) return fallback;
    return multiply(value, 1.0 / length);
}

float deterministicPhase(std::uint64_t parcelId, std::uint64_t salt) {
    std::uint64_t value = parcelId ^ (salt + 0x9e3779b97f4a7c15ull + (parcelId << 6u) +
                                      (parcelId >> 2u));
    value ^= value >> 30u;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27u;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31u;
    const double unit = static_cast<double>(value & 0x00ffffffu) /
                        static_cast<double>(0x01000000u);
    return static_cast<float>(unit * 2.0 * kPi);
}

void addFiniteCoreVortex(float pointLateralM,
                         float pointVerticalM,
                         float vortexLateralM,
                         float vortexVerticalM,
                         float circulationM2ps,
                         float coreRadiusM,
                         float& lateralVelocityMps,
                         float& verticalVelocityMps) {
    const float dx = pointLateralM - vortexLateralM;
    const float dy = pointVerticalM - vortexVerticalM;
    const float denominator = dx * dx + dy * dy + coreRadiusM * coreRadiusM;
    if (!finite(denominator) || denominator <= 1.0e-6f) return;
    const float coefficient = circulationM2ps /
        static_cast<float>(2.0 * kPi * static_cast<double>(denominator));
    lateralVelocityMps += -coefficient * dy;
    verticalVelocityMps += coefficient * dx;
}

void clampVector(float& lateralMps, float& verticalMps, float maximumMps) {
    const float speed = std::sqrt(lateralMps * lateralMps + verticalMps * verticalMps);
    if (!finite(speed) || speed <= maximumMps || speed <= 1.0e-6f) return;
    const float scale = maximumMps / speed;
    lateralMps *= scale;
    verticalMps *= scale;
}

}  // namespace

WakeFluidState initializeWakeFluidState(
    const WakeFluidAircraftGeometry& aircraft,
    const WakeFluidEnvironment& environment,
    const Vec3d& rightWorld,
    const Vec3d& upWorld,
    float sourceLateralM,
    float sourceVerticalM,
    const WakeFluidSettings& settings) {
    WakeFluidState state;
    state.rightWorld = normalized(rightWorld, {1.0, 0.0, 0.0});
    state.upWorld = normalized(upWorld, {0.0, 1.0, 0.0});
    state.initialLateralM = sourceLateralM;
    state.initialVerticalM = sourceVerticalM;
    state.lateralM = sourceLateralM;
    state.verticalM = sourceVerticalM;

    if (!settings.enabled) return state;

    const float wingspan = std::clamp(
        aircraft.wingspanM > 0.0f ? aircraft.wingspanM : settings.defaultWingspanM,
        settings.minimumWingspanM,
        settings.maximumWingspanM);
    const float separation = std::max(
        wingspan * settings.effectiveVortexSeparationFraction, 2.0f);
    const float density = std::clamp(environment.airDensityKgM3, 0.08f, 1.50f);
    const float trueAirspeed = std::clamp(environment.trueAirspeedMps, 45.0f, 360.0f);
    const float gravity = std::clamp(environment.gravityMps2, 7.0f, 12.0f);
    const float loadFactor = std::clamp(
        environment.normalLoadFactorG > 0.05f ? environment.normalLoadFactorG : 1.0f,
        0.45f,
        2.50f);
    const float mass = std::clamp(
        environment.aircraftMassKg > 1000.0f
            ? environment.aircraftMassKg
            : (aircraft.referenceMassKg > 1000.0f ? aircraft.referenceMassKg : 65000.0f),
        1000.0f,
        600000.0f);

    // Engineering lifting-line estimate: Gamma = L / (rho * V * b_eff).
    const float circulation = mass * gravity * loadFactor /
        std::max(density * trueAirspeed * separation, 1.0f);
    const float initialCoreRadius = std::max(
        settings.minimumInitialCoreRadiusM,
        wingspan * settings.initialCoreRadiusFraction);

    state.initialCirculationM2ps = std::clamp(circulation, 5.0f, 1200.0f);
    state.circulationM2ps = state.initialCirculationM2ps;
    state.vortexSeparationM = separation;
    state.initialCoreRadiusM = initialCoreRadius;
    state.coreRadiusM = initialCoreRadius;
    state.initialized = finite(state.initialCirculationM2ps) && finite(separation) &&
                        finite(initialCoreRadius);
    return state;
}

WakeFluidStepResult advanceWakeFluidState(
    WakeFluidState& state,
    std::uint64_t parcelId,
    float parcelAgeSeconds,
    float deltaSeconds,
    const WakeFluidEnvironment& environment,
    const WakeFluidSettings& settings) {
    WakeFluidStepResult result;
    if (!settings.enabled || !state.initialized || deltaSeconds <= 0.0f) {
        result.circulationM2ps = state.circulationM2ps;
        result.coreRadiusM = state.coreRadiusM;
        return result;
    }

    const float boundedDelta = std::clamp(deltaSeconds, 0.0f, 0.25f);
    const float maximumStep = std::clamp(settings.maximumIntegrationStepSeconds, 0.01f, 0.10f);
    const std::size_t substeps = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::ceil(boundedDelta / maximumStep)));
    const float substepSeconds = boundedDelta / static_cast<float>(substeps);
    const float startAge = std::max(0.0f, parcelAgeSeconds - boundedDelta);

    const float turbulence = std::clamp(environment.turbulenceScale, 0.0f, 1.0f);
    const float shearMagnitude = static_cast<float>(magnitude(environment.shearVelocityWorldMps));
    const float decayTime = std::clamp(
        settings.circulationDecaySeconds /
            (1.0f + settings.turbulenceDecayMultiplier * turbulence +
             settings.shearDecayMultiplier * shearMagnitude),
        12.0f,
        180.0f);
    const float eddyViscosity = std::max(
        0.001f,
        settings.baseEddyViscosityM2ps +
            settings.turbulenceEddyViscosityM2ps * turbulence +
            settings.shearEddyViscosityM2psPerMps * shearMagnitude);

    const float phaseA = deterministicPhase(parcelId, 0x52dce729ull);
    const float phaseB = deterministicPhase(parcelId, 0x8f3f73b5ull);
    Vec3d accumulatedWorld {};

    for (std::size_t step = 0; step < substeps; ++step) {
        const float age = startAge + (static_cast<float>(step) + 0.5f) * substepSeconds;
        const float circulation = state.initialCirculationM2ps * std::exp(-age / decayTime);
        const float coreRadius = std::sqrt(
            state.initialCoreRadiusM * state.initialCoreRadiusM +
            4.0f * eddyViscosity * age);
        const float separation = std::max(state.vortexSeparationM, 2.0f);
        const float halfSeparation = 0.5f * separation;

        // The pair's mutual induction translates both vortex cores downward.
        const float wakeDescent = std::clamp(
            circulation / static_cast<float>(2.0 * kPi * separation),
            0.0f,
            settings.maximumWakeDescentMps);
        state.wakeCentreVerticalM -= wakeDescent * substepSeconds;

        float inducedLateral = 0.0f;
        float inducedVertical = 0.0f;
        addFiniteCoreVortex(
            state.lateralM,
            state.verticalM,
            -halfSeparation,
            state.wakeCentreVerticalM,
            -circulation,
            coreRadius,
            inducedLateral,
            inducedVertical);
        addFiniteCoreVortex(
            state.lateralM,
            state.verticalM,
            halfSeparation,
            state.wakeCentreVerticalM,
            circulation,
            coreRadius,
            inducedLateral,
            inducedVertical);
        clampVector(inducedLateral, inducedVertical, settings.maximumInducedSpeedMps);

        // Viscous entrainment draws each exhaust stream toward its nearest wing vortex.
        const float targetLateral = state.initialLateralM < 0.0f
            ? -halfSeparation
            : (state.initialLateralM > 0.0f ? halfSeparation : 0.0f);
        const float captureRamp = smoothstep(0.6f, 14.0f, age) * std::exp(-age / 55.0f);
        const float captureDx = targetLateral - state.lateralM;
        const float captureDy = state.wakeCentreVerticalM - state.verticalM;
        const float captureDistance = std::sqrt(captureDx * captureDx + captureDy * captureDy);
        if (captureDistance > 0.05f) {
            const float captureVelocity = settings.captureVelocityMps * captureRamp;
            inducedLateral += captureVelocity * captureDx / captureDistance;
            inducedVertical += captureVelocity * captureDy / captureDistance;
        }

        const float turbulenceRamp = smoothstep(1.0f, 18.0f, age);
        const float turbulenceAmplitude = settings.turbulenceVelocityMps * turbulence *
                                          (0.20f + 0.80f * turbulenceRamp);
        const float turbulenceLateral = turbulenceAmplitude *
            (0.65f * std::sin(age * 0.37f + phaseA) +
             0.35f * std::sin(age * 0.83f + phaseB));
        const float turbulenceVertical = turbulenceAmplitude *
            (0.70f * std::cos(age * 0.29f + phaseB) +
             0.30f * std::cos(age * 0.71f + phaseA));

        const float relativeVertical = state.verticalM - state.wakeCentreVerticalM;
        const float shearFraction = std::clamp(
            relativeVertical / std::max(0.5f * separation, 1.0f), -1.0f, 1.0f);
        const float shearRamp = smoothstep(2.0f, 30.0f, age);
        const float shearRight = static_cast<float>(dot(
            environment.shearVelocityWorldMps, state.rightWorld));
        const float shearUp = static_cast<float>(dot(
            environment.shearVelocityWorldMps, state.upWorld));
        const float shearLateral = shearRight * shearFraction * shearRamp * settings.shearCoupling;
        const float shearVertical = shearUp * shearFraction * shearRamp * settings.shearCoupling;

        const float lateralVelocity = inducedLateral + turbulenceLateral + shearLateral;
        const float verticalVelocity = inducedVertical - wakeDescent +
                                       turbulenceVertical + shearVertical;
        const float deltaLateral = lateralVelocity * substepSeconds;
        const float deltaVertical = verticalVelocity * substepSeconds;
        state.lateralM += deltaLateral;
        state.verticalM += deltaVertical;

        const Vec3d worldDelta = add(
            multiply(state.rightWorld, deltaLateral),
            multiply(state.upWorld, deltaVertical));
        accumulatedWorld = add(accumulatedWorld, worldDelta);

        state.circulationM2ps = circulation;
        state.coreRadiusM = coreRadius;
        state.inducedSpeedMps = std::sqrt(
            inducedLateral * inducedLateral + inducedVertical * inducedVertical);
        state.wakeDescentMps = wakeDescent;
        state.turbulenceSpeedMps = std::sqrt(
            turbulenceLateral * turbulenceLateral + turbulenceVertical * turbulenceVertical);
        state.shearSpeedMps = std::sqrt(
            shearLateral * shearLateral + shearVertical * shearVertical);

        result.inducedSpeedMps = std::max(result.inducedSpeedMps, state.inducedSpeedMps);
        result.wakeDescentMps = std::max(result.wakeDescentMps, state.wakeDescentMps);
        result.turbulenceSpeedMps = std::max(
            result.turbulenceSpeedMps, state.turbulenceSpeedMps);
        result.shearSpeedMps = std::max(result.shearSpeedMps, state.shearSpeedMps);
    }

    result.worldDisplacementM = accumulatedWorld;
    result.circulationM2ps = state.circulationM2ps;
    result.coreRadiusM = state.coreRadiusM;
    result.finite = finite(accumulatedWorld.x) && finite(accumulatedWorld.y) &&
                    finite(accumulatedWorld.z) && finite(result.circulationM2ps) &&
                    finite(result.coreRadiusM) && finite(result.inducedSpeedMps) &&
                    finite(result.wakeDescentMps) && finite(result.turbulenceSpeedMps) &&
                    finite(result.shearSpeedMps);
    if (!result.finite) {
        result.worldDisplacementM = {};
    }
    return result;
}

}  // namespace ffatmo::engine
