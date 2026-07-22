#pragma once

#include "diagnostics/ReplayRunner.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ffatmo::engine {

// First deterministic contrail-physics baseline. The ice-mass value is an
// engine-normalized visual mass, not a claim of calibrated physical kilograms.
struct ContrailSimulationSettings {
    double minimumFormationAltitudeM = 4500.0;
    double fullFormationAltitudeM = 7000.0;
    float minimumEngineN1Percent = 35.0f;
    float fullEngineN1Percent = 68.0f;
    float emissionIntervalSeconds = 0.40f;
    float minimumVisibleOpticalDepth = 0.015f;
    float minimumNormalizedIceMass = 0.008f;
    float minimumDryLifetimeSeconds = 7.0f;
    float maximumDryLifetimeSeconds = 55.0f;
    float maximumParcelAgeSeconds = 600.0f;
    float initialParcelRadiusM = 1.25f;
    float baseSpreadRateMps = 0.22f;
    float initialWakeDescentMps = 0.85f;
    float persistentGrowthRatePerSecond = 0.0025f;
    std::size_t maximumActiveParcels = 100'000;
};

struct ContrailParcel {
    std::uint64_t id = 0;
    std::uint32_t engineIndex = 0;
    Vec3d worldPositionM {};
    float ageSeconds = 0.0f;
    float radiusM = 0.0f;
    float normalizedIceMass = 0.0f;
    float opticalDepth = 0.0f;
    float sourceTemperatureK = 0.0f;
    float sourceRelativeHumidityIcePercent = 0.0f;
};

struct ContrailTimelineSample {
    std::uint64_t sequenceNumber = 0;
    double simulationTimeSeconds = 0.0;
    double physicsDeltaSeconds = 0.0;
    float formationPotential = 0.0f;
    float relativeHumidityIcePercent = 0.0f;
    float temperatureK = 0.0f;
    std::uint64_t emittedParcelCount = 0;
    std::uint64_t expiredParcelCount = 0;
    std::uint64_t activeParcelCount = 0;
    float totalNormalizedIceMass = 0.0f;
    float meanOpticalDepth = 0.0f;
    float meanRadiusM = 0.0f;
    float maximumParcelAgeSeconds = 0.0f;
    double visibleTrailLengthM = 0.0;
    bool persistentEnvironment = false;
    bool physicsFrozen = false;
};

struct ContrailSimulationSummary {
    bool ok = false;
    std::string error;
    std::uint64_t inputSampleCount = 0;
    std::uint64_t emittedParcelCount = 0;
    std::uint64_t expiredParcelCount = 0;
    std::uint64_t peakActiveParcelCount = 0;
    std::uint64_t formationSampleCount = 0;
    std::uint64_t persistentEnvironmentSampleCount = 0;
    std::uint64_t frozenPhysicsSampleCount = 0;
    std::uint64_t capacityDropCount = 0;
    double firstFormationTimeSeconds = -1.0;
    double lastFormationTimeSeconds = -1.0;
    double maximumVisibleTrailLengthM = 0.0;
    float maximumTotalNormalizedIceMass = 0.0f;
    float maximumParcelAgeSeconds = 0.0f;
    float maximumFormationPotential = 0.0f;
    std::uint64_t deterministicHash = 0;
};

struct ContrailSimulationResult {
    ContrailSimulationSummary summary;
    std::vector<ContrailTimelineSample> timeline;
    std::vector<ContrailParcel> finalParcels;
};

ContrailSimulationResult simulateContrails(
    const diagnostics::ReplayReadResult& replay,
    const diagnostics::ReplayRunnerResult& normalizedReplay,
    const ContrailSimulationSettings& settings = {});

bool writeContrailSimulationSummary(const ContrailSimulationResult& result,
                                    const diagnostics::ReplayMetadata& metadata,
                                    const std::filesystem::path& outputPath,
                                    std::string* error = nullptr);

bool writeContrailTimelineCsv(const ContrailSimulationResult& result,
                              const std::filesystem::path& outputPath,
                              std::string* error = nullptr);

}  // namespace ffatmo::engine
