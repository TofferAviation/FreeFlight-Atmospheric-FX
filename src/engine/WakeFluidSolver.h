#pragma once

#include "engine/SimulatorSnapshot.h"

#include <cstdint>

namespace ffatmo::engine {

// Real-time engineering wake model. It uses a finite-core counter-rotating
// vortex pair and deterministic sub-grid turbulence rather than a full-domain
// Navier-Stokes solve, so it remains affordable for thousands of trail parcels.
struct WakeFluidSettings {
    bool enabled = true;
    float defaultWingspanM = 35.8f;
    float minimumWingspanM = 8.0f;
    float maximumWingspanM = 90.0f;
    float effectiveVortexSeparationFraction = 0.785398163f;  // pi / 4
    float initialCoreRadiusFraction = 0.015f;
    float minimumInitialCoreRadiusM = 0.30f;
    float circulationDecaySeconds = 72.0f;
    float turbulenceDecayMultiplier = 2.25f;
    float shearDecayMultiplier = 0.045f;
    float baseEddyViscosityM2ps = 0.018f;
    float turbulenceEddyViscosityM2ps = 0.085f;
    float shearEddyViscosityM2psPerMps = 0.0025f;
    float maximumInducedSpeedMps = 8.0f;
    float maximumWakeDescentMps = 3.5f;
    float maximumIntegrationStepSeconds = 0.05f;
    float captureVelocityMps = 0.22f;
    float turbulenceVelocityMps = 0.65f;
    float shearCoupling = 0.28f;
};

struct WakeFluidAircraftGeometry {
    float wingspanM = 0.0f;
    float referenceMassKg = 0.0f;
};

struct WakeFluidEnvironment {
    float aircraftMassKg = 0.0f;
    float trueAirspeedMps = 0.0f;
    float airDensityKgM3 = 0.0f;
    float gravityMps2 = 9.80665f;
    float normalLoadFactorG = 1.0f;
    float turbulenceScale = 0.0f;
    Vec3d shearVelocityWorldMps {};
};

struct WakeFluidState {
    Vec3d rightWorld {1.0, 0.0, 0.0};
    Vec3d upWorld {0.0, 1.0, 0.0};
    float initialLateralM = 0.0f;
    float initialVerticalM = 0.0f;
    float lateralM = 0.0f;
    float verticalM = 0.0f;
    float initialCirculationM2ps = 0.0f;
    float circulationM2ps = 0.0f;
    float vortexSeparationM = 0.0f;
    float initialCoreRadiusM = 0.0f;
    float coreRadiusM = 0.0f;
    float wakeCentreVerticalM = 0.0f;
    float inducedSpeedMps = 0.0f;
    float wakeDescentMps = 0.0f;
    float turbulenceSpeedMps = 0.0f;
    float shearSpeedMps = 0.0f;
    bool initialized = false;
};

struct WakeFluidStepResult {
    Vec3d worldDisplacementM {};
    float circulationM2ps = 0.0f;
    float coreRadiusM = 0.0f;
    float inducedSpeedMps = 0.0f;
    float wakeDescentMps = 0.0f;
    float turbulenceSpeedMps = 0.0f;
    float shearSpeedMps = 0.0f;
    bool finite = true;
};

WakeFluidState initializeWakeFluidState(
    const WakeFluidAircraftGeometry& aircraft,
    const WakeFluidEnvironment& environment,
    const Vec3d& rightWorld,
    const Vec3d& upWorld,
    float sourceLateralM,
    float sourceVerticalM,
    const WakeFluidSettings& settings = {});

WakeFluidStepResult advanceWakeFluidState(
    WakeFluidState& state,
    std::uint64_t parcelId,
    float parcelAgeSeconds,
    float deltaSeconds,
    const WakeFluidEnvironment& environment,
    const WakeFluidSettings& settings = {});

}  // namespace ffatmo::engine
