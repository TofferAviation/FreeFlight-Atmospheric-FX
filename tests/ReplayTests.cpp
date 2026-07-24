#include "diagnostics/ReplayFile.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ffatmo::engine::SimulatorSnapshot makeSnapshot(std::uint64_t sequence) {
    using namespace ffatmo::engine;
    SimulatorSnapshot snapshot;
    snapshot.sequenceNumber = sequence;
    snapshot.validityMask = ValidTime | ValidLocalTransform | ValidAtmosphereAtAircraft |
                            ValidAtmosphereProfile | ValidPropulsion | ValidAircraftIdentity;
    snapshot.simulatorUptimeSeconds = 1000.0 + static_cast<double>(sequence) * 0.05;
    snapshot.flightTimeSeconds = 200.0 + static_cast<double>(sequence) * 0.05;
    snapshot.monotonicTimeSeconds = static_cast<double>(sequence) * 0.05;
    snapshot.deltaTimeSeconds = 0.05f;
    snapshot.timeAcceleration = 1.0f;
    snapshot.localPositionM = {100.0 + sequence, 2000.0, -300.0};
    snapshot.pitchDeg = 2.5f;
    snapshot.rollDeg = -1.0f;
    snapshot.headingDegTrue = 275.0f;
    snapshot.linearVelocityLocalMps = {12.0f, 0.5f, -230.0f};
    snapshot.trueAirspeedMps = 231.0f;
    snapshot.atmosphere.temperatureK = 218.15f;
    snapshot.atmosphere.staticPressurePa = 23800.0f;
    snapshot.atmosphere.densityKgM3 = 0.38f;
    snapshot.atmosphere.windLocalMps = {8.0f, 0.0f, -3.0f};
    snapshot.atmosphere.profile.temperatureLevelCount = 2;
    snapshot.atmosphere.profile.temperatureAltitudeMslM[0] = 9000.0f;
    snapshot.atmosphere.profile.temperatureAltitudeMslM[1] = 11000.0f;
    snapshot.atmosphere.profile.temperatureK[0] = 228.15f;
    snapshot.atmosphere.profile.temperatureK[1] = 218.15f;
    snapshot.atmosphere.profile.dewPointLevelCount = 2;
    snapshot.atmosphere.profile.dewPointAltitudeMslM[0] = 9000.0f;
    snapshot.atmosphere.profile.dewPointAltitudeMslM[1] = 11000.0f;
    snapshot.atmosphere.profile.dewPointK[0] = 222.15f;
    snapshot.atmosphere.profile.dewPointK[1] = 216.15f;
    snapshot.atmosphere.profile.windLevelCount = 2;
    snapshot.atmosphere.profile.windAltitudeMslM[0] = 9000.0f;
    snapshot.atmosphere.profile.windAltitudeMslM[1] = 11000.0f;
    snapshot.atmosphere.profile.windSpeedMps[0] = 12.0f;
    snapshot.atmosphere.profile.windSpeedMps[1] = 18.0f;
    snapshot.engineCount = 2;
    snapshot.engines[0].running = 1;
    snapshot.engines[0].n1Percent = 86.0f;
    snapshot.engines[0].fuelFlowKgps = 0.82f;
    snapshot.engines[1] = snapshot.engines[0];
    copyTruncated(snapshot.aircraftName, "Boeing 737-800NG");
    copyTruncated(snapshot.aircraftIcao, "B738");
    copyTruncated(snapshot.aircraftRelativePath, "Aircraft/737NG/737_80NG.acf");
    return snapshot;
}

}  // namespace

int main() {
    using namespace ffatmo::diagnostics;

    const std::filesystem::path path = "ffatmo_replay_roundtrip.ffar";
    const std::filesystem::path truncatedPath = "ffatmo_replay_truncated.ffar";
    const std::filesystem::path summaryPath = "ffatmo_replay_summary.txt";

    ReplayMetadata metadata;
    metadata.engineBuild = "unit-test";
    metadata.gitRevision = "deadbeef";
    metadata.xplaneVersion = "120400";
    metadata.platform = "test";
    metadata.aircraftName = "Boeing 737-800NG";
    metadata.aircraftIcao = "B738";
    metadata.aircraftRelativePath = "Aircraft/737NG/737_80NG.acf";
    metadata.scenarioSeed = 42;

    ReplayRecorder recorder;
    std::string error;
    require(recorder.start(path, metadata, &error), "recorder starts: " + error);
    for (std::uint64_t sequence = 1; sequence <= 500; ++sequence) {
        require(recorder.tryPush(makeSnapshot(sequence)), "snapshot accepted by bounded queue");
    }
    require(recorder.stop(&error), "recorder finalizes: " + error);

    const ReplayReadResult replay = readReplayFile(path);
    require(replay.ok, "recording reads back: " + replay.error);
    require(replay.summary.cleanEndChunk, "clean end chunk exists");
    require(replay.summary.snapshotCount == 500, "all snapshots round-trip");
    require(replay.summary.droppedSnapshotCount == 0, "no snapshots dropped in test");
    require(replay.metadata.aircraftIcao == "B738", "metadata round-trip");
    require(replay.snapshots.front().sequenceNumber == 1, "first sequence preserved");
    require(replay.snapshots.back().sequenceNumber == 500, "last sequence preserved");
    require(std::abs(replay.snapshots[99].localPositionM.x - 200.0) < 0.0001,
            "double position round-trip");
    require(std::abs(replay.snapshots[99].atmosphere.profile.dewPointK[1] - 216.15f) < 0.001f,
            "dew-point profile round-trip");
    require(replay.snapshots[99].engines[1].running == 1,
            "engine state round-trip");
    require(writeReplaySummary(replay, summaryPath, &error), "summary writes: " + error);

    {
        std::ifstream input(path, std::ios::binary);
        std::ofstream output(truncatedPath, std::ios::binary | std::ios::trunc);
        input.seekg(0, std::ios::end);
        const auto length = input.tellg();
        input.seekg(0, std::ios::beg);
        const std::streamoff keep = length > 20 ? static_cast<std::streamoff>(length) - 20 : 0;
        for (std::streamoff index = 0; index < keep; ++index) output.put(static_cast<char>(input.get()));
    }
    const ReplayReadResult truncated = readReplayFile(truncatedPath);
    require(!truncated.ok, "truncated replay is rejected");

    std::filesystem::remove(path);
    std::filesystem::remove(truncatedPath);
    std::filesystem::remove(summaryPath);
    std::cout << "FFAtmo replay tests passed\n";
    return 0;
}
