#include "diagnostics/ReplayRunner.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ffatmo::engine::SimulatorSnapshot makeSnapshot(std::uint64_t sequence,
                                               double simulatorTime,
                                               double latitude,
                                               double longitude,
                                               double altitude,
                                               double localX,
                                               bool paused,
                                               bool replaying,
                                               float tasMps) {
    using namespace ffatmo::engine;
    SimulatorSnapshot snapshot;
    snapshot.sequenceNumber = sequence;
    snapshot.validityMask = ValidTime | ValidGeodeticPosition | ValidLocalTransform |
                            ValidAerodynamics | ValidAtmosphereAtAircraft |
                            ValidAtmosphereProfile | ValidPropulsion | ValidAircraftIdentity;
    snapshot.simulatorUptimeSeconds = simulatorTime;
    snapshot.flightTimeSeconds = simulatorTime;
    snapshot.monotonicTimeSeconds = simulatorTime;
    snapshot.deltaTimeSeconds = 0.05f;
    snapshot.paused = paused ? 1u : 0u;
    snapshot.replaying = replaying ? 1u : 0u;
    snapshot.latitudeDeg = latitude;
    snapshot.longitudeDeg = longitude;
    snapshot.elevationMslM = altitude;
    snapshot.localPositionM = {localX, altitude, -100.0};
    snapshot.trueAirspeedMps = tasMps;
    snapshot.angleOfAttackDeg = 3.0f;
    snapshot.sideslipDeg = 0.2f;
    snapshot.atmosphere.temperatureK = 230.0f;
    snapshot.atmosphere.staticPressurePa = 30000.0f;
    snapshot.atmosphere.profile.dewPointLevelCount = 2;
    snapshot.atmosphere.profile.dewPointAltitudeMslM[0] = 0.0f;
    snapshot.atmosphere.profile.dewPointAltitudeMslM[1] = 12000.0f;
    snapshot.atmosphere.profile.dewPointK[0] = 270.0f;
    snapshot.atmosphere.profile.dewPointK[1] = 225.0f;
    snapshot.engineCount = 2;
    snapshot.engines[0].running = 1;
    snapshot.engines[0].n1Percent = 80.0f;
    snapshot.engines[0].thrustN = 18000.0f;
    snapshot.engines[1] = snapshot.engines[0];
    copyTruncated(snapshot.aircraftName, "Boeing 737-800NG");
    copyTruncated(snapshot.aircraftIcao, "B738");
    copyTruncated(snapshot.aircraftRelativePath, "Aircraft/737NG/737_80NG.acf");
    return snapshot;
}

}  // namespace

int main() {
    using namespace ffatmo::diagnostics;

    ReplayReadResult replay;
    replay.ok = true;
    replay.metadata.aircraftName = "Boeing 737-800NG";
    replay.metadata.aircraftIcao = "B738";
    replay.snapshots.push_back(makeSnapshot(1, 100.00, 60.000000, 10.000000, 9000.0, 0.0, false, false, 220.0f));
    replay.snapshots.push_back(makeSnapshot(2, 100.05, 60.000010, 10.000000, 9001.0, 1.0, false, false, 220.0f));
    replay.snapshots.push_back(makeSnapshot(3, 100.10, 60.000020, 10.000000, 9002.0, 100001.0, false, false, 220.0f));
    replay.snapshots.push_back(makeSnapshot(4, 100.10, 60.000020, 10.000000, 9002.0, 100001.0, true, false, 0.0f));
    replay.snapshots.push_back(makeSnapshot(5, 100.10, 60.000020, 10.000000, 9002.0, 100001.0, false, true, 220.0f));
    replay.snapshots.push_back(makeSnapshot(6, 100.15, 60.000030, 10.000000, 9003.0, 100002.0, false, false, 220.0f));
    replay.snapshots[2].lifecycleFlags = ffatmo::engine::LifecycleManualMarker;

    const ReplayRunnerResult first = runReplayAnalysis(replay);
    const ReplayRunnerResult second = runReplayAnalysis(replay);

    require(first.summary.ok, "synthetic replay normalizes successfully: " + first.summary.error);
    require(first.summary.inputSnapshotCount == 6, "all snapshots are consumed");
    require(first.summary.normalizedSampleCount == 6, "all normalized samples are produced");
    require(first.summary.localOriginRebaseCount == 1, "one local-origin rebase is detected");
    require(first.summary.pausedSampleCount == 1, "pause sample is counted");
    require(first.summary.replaySampleCount == 1, "X-Plane replay sample is counted");
    require(first.summary.manualMarkerCount == 1, "manual marker is counted");
    require(first.summary.lowSpeedAerodynamicRejectionCount == 1,
            "low-speed aerodynamic angles are rejected");
    require(std::abs(first.samples[3].physicsDeltaSeconds) < 1e-9,
            "paused sample does not advance physics");
    require(std::abs(first.samples[4].physicsDeltaSeconds) < 1e-9,
            "replay sample does not advance physics");
    require(first.samples[2].localOriginRebased,
            "the local coordinate discontinuity is marked on the sample");
    require(first.samples[2].worldNorthM < 10.0,
            "engine-owned geodetic world position remains continuous across local rebase");
    require(first.summary.deterministicHash == second.summary.deterministicHash,
            "same replay produces identical deterministic hash");
    require(first.summary.deterministicHash != 0,
            "deterministic hash is populated");

    std::cout << "FFAtmo offline replay runner tests passed\n";
    return 0;
}
