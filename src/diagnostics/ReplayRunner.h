#pragma once

#include "diagnostics/ReplayFile.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ffatmo::diagnostics {

struct NormalizedReplaySample {
    std::uint64_t sequenceNumber = 0;
    double simulationTimeSeconds = 0.0;
    double physicsDeltaSeconds = 0.0;
    double worldEastM = 0.0;
    double worldUpM = 0.0;
    double worldNorthM = 0.0;
    double altitudeMslM = 0.0;
    float trueAirspeedMps = 0.0f;
    float verticalSpeedMps = 0.0f;
    float temperatureK = 0.0f;
    float dewPointK = 0.0f;
    float relativeHumidityIcePercent = 0.0f;
    float meanEngineN1Percent = 0.0f;
    float meanEngineThrustN = 0.0f;
    float flapRatio = 0.0f;
    float slatRatio = 0.0f;
    bool paused = false;
    bool replaying = false;
    bool localOriginRebased = false;
    bool aerodynamicAnglesValid = false;
};

struct ReplayRunnerSummary {
    bool ok = false;
    std::string error;
    std::uint64_t inputSnapshotCount = 0;
    std::uint64_t normalizedSampleCount = 0;
    std::uint64_t pausedSampleCount = 0;
    std::uint64_t replaySampleCount = 0;
    std::uint64_t manualMarkerCount = 0;
    std::uint64_t localOriginRebaseCount = 0;
    std::uint64_t lowSpeedAerodynamicRejectionCount = 0;
    std::uint64_t nonFiniteInputCount = 0;
    double firstSimulatorTimeSeconds = 0.0;
    double lastSimulatorTimeSeconds = 0.0;
    double integratedPhysicsTimeSeconds = 0.0;
    double minimumAltitudeMslM = 0.0;
    double maximumAltitudeMslM = 0.0;
    float minimumTemperatureK = 0.0f;
    float maximumTemperatureK = 0.0f;
    float minimumRelativeHumidityIcePercent = 0.0f;
    float maximumRelativeHumidityIcePercent = 0.0f;
    std::uint64_t deterministicHash = 0;
};

struct ReplayRunnerResult {
    ReplayRunnerSummary summary;
    std::vector<NormalizedReplaySample> samples;
};

ReplayRunnerResult runReplayAnalysis(const ReplayReadResult& replay);
ReplayRunnerResult runReplayAnalysis(const std::filesystem::path& replayPath,
                                     std::size_t maximumSnapshots = 2'000'000);

bool writeReplayRunnerSummary(const ReplayRunnerResult& result,
                              const ReplayMetadata& metadata,
                              const std::filesystem::path& outputPath,
                              std::string* error = nullptr);

bool writeNormalizedReplayCsv(const ReplayRunnerResult& result,
                              const std::filesystem::path& outputPath,
                              std::string* error = nullptr);

}  // namespace ffatmo::diagnostics
