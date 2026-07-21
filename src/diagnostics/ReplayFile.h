#pragma once

#include "engine/SimulatorSnapshot.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ffatmo::diagnostics {

constexpr std::uint32_t kReplayContainerVersion = 1;

struct ReplayMetadata {
    std::string engineBuild;
    std::string gitRevision;
    std::string xplaneVersion;
    std::string platform;
    std::string aircraftName;
    std::string aircraftIcao;
    std::string aircraftRelativePath;
    std::string coordinateConvention = "XPLANE_LOCAL_METRES_BODY_X_RIGHT_Y_UP_Z_AFT";
    std::uint64_t scenarioSeed = 0;
};

struct ReplaySummary {
    std::uint64_t snapshotCount = 0;
    std::uint64_t droppedSnapshotCount = 0;
    std::uint64_t firstSequence = 0;
    std::uint64_t lastSequence = 0;
    double firstSimulatorTimeSeconds = 0.0;
    double lastSimulatorTimeSeconds = 0.0;
    bool cleanEndChunk = false;
};

struct ReplayReadResult {
    bool ok = false;
    std::string error;
    ReplayMetadata metadata;
    ReplaySummary summary;
    std::vector<engine::SimulatorSnapshot> snapshots;
};

class ReplayRecorder {
public:
    ReplayRecorder();
    ~ReplayRecorder();

    ReplayRecorder(const ReplayRecorder&) = delete;
    ReplayRecorder& operator=(const ReplayRecorder&) = delete;

    bool start(const std::filesystem::path& outputPath,
               ReplayMetadata metadata,
               std::string* error = nullptr);
    bool tryPush(const engine::SimulatorSnapshot& snapshot);
    bool stop(std::string* error = nullptr);

    bool recording() const;
    std::uint64_t acceptedSnapshotCount() const;
    std::uint64_t droppedSnapshotCount() const;
    std::filesystem::path outputPath() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

ReplayReadResult readReplayFile(const std::filesystem::path& path,
                                std::size_t maximumSnapshots = 2'000'000);

bool writeReplaySummary(const ReplayReadResult& replay,
                        const std::filesystem::path& outputPath,
                        std::string* error = nullptr);

}  // namespace ffatmo::diagnostics
