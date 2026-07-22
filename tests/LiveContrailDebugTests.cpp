#include "diagnostics/LiveSnapshotNormalizer.h"
#include "engine/ContrailSimulation.h"
#include "engine/LiveContrailEngine.h"

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

struct Fixture {
    ffatmo::diagnostics::ReplayReadResult replay;
    ffatmo::diagnostics::ReplayRunnerResult normalized;
};

Fixture makeDryFixture(std::size_t sampleCount) {
    using namespace ffatmo;
    Fixture fixture;
    fixture.replay.ok = true;
    fixture.normalized.summary.ok = true;
    fixture.replay.snapshots.reserve(sampleCount);
    fixture.normalized.samples.reserve(sampleCount);

    double simulationTime = 0.0;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        engine::SimulatorSnapshot snapshot;
        snapshot.sequenceNumber = static_cast<std::uint64_t>(index + 1);
        snapshot.simulatorUptimeSeconds = 100.0 + simulationTime;
        snapshot.latitudeDeg = 60.0;
        snapshot.longitudeDeg = 10.0 + static_cast<double>(index) * 0.00001;
        snapshot.elevationMslM = 9000.0;
        snapshot.localPositionM = {
            static_cast<double>(index) * 22.0,
            9000.0,
            0.0
        };
        snapshot.headingDegTrue = 90.0f;
        snapshot.trueAirspeedMps = 220.0f;
        snapshot.atmosphere.temperatureK = 234.0f;
        snapshot.atmosphere.staticPressurePa = 30000.0f;
        snapshot.atmosphere.windLocalMps = {8.0f, 0.0f, -2.0f};
        snapshot.atmosphere.profile.windLevelCount = 2;
        snapshot.atmosphere.profile.windAltitudeMslM[0] = 0.0f;
        snapshot.atmosphere.profile.windAltitudeMslM[1] = 12000.0f;
        snapshot.atmosphere.profile.turbulenceScale[0] = 0.02f;
        snapshot.atmosphere.profile.turbulenceScale[1] = 0.08f;
        snapshot.engineCount = 2;
        for (std::uint32_t engineIndex = 0; engineIndex < 2; ++engineIndex) {
            snapshot.engines[engineIndex].running = 1;
            snapshot.engines[engineIndex].n1Percent = 90.0f;
            snapshot.engines[engineIndex].fuelFlowKgps = 1.10f;
            snapshot.engines[engineIndex].thrustN = 26000.0f;
            snapshot.engines[engineIndex].exhaustVelocityMps = 390.0f;
        }
        fixture.replay.snapshots.push_back(snapshot);

        diagnostics::NormalizedReplaySample normalized;
        normalized.sequenceNumber = snapshot.sequenceNumber;
        normalized.simulationTimeSeconds = simulationTime;
        normalized.physicsDeltaSeconds = index == 0 ? 0.0 : 0.1;
        normalized.worldEastM = static_cast<double>(index) * 22.0;
        normalized.worldUpM = 9000.0;
        normalized.worldNorthM = 0.0;
        normalized.altitudeMslM = 9000.0;
        normalized.trueAirspeedMps = 220.0f;
        normalized.temperatureK = 234.0f;
        normalized.dewPointK = 226.0f;
        normalized.relativeHumidityIcePercent = 72.0f;
        normalized.meanEngineN1Percent = 90.0f;
        normalized.meanEngineThrustN = 26000.0f;
        normalized.aerodynamicAnglesValid = true;
        if (index == 350) {
            normalized.physicsDeltaSeconds = 0.0;
            normalized.paused = true;
        }
        if (index == 700) {
            normalized.physicsDeltaSeconds = 0.0;
            normalized.replaying = true;
        }
        fixture.normalized.samples.push_back(normalized);
        simulationTime += normalized.physicsDeltaSeconds;
    }
    fixture.normalized.summary.inputSnapshotCount = sampleCount;
    fixture.normalized.summary.normalizedSampleCount = sampleCount;
    return fixture;
}

}  // namespace

int main() {
    using namespace ffatmo;

    diagnostics::LiveSnapshotNormalizer normalizer;
    engine::SimulatorSnapshot first;
    first.sequenceNumber = 1;
    first.simulatorUptimeSeconds = 10.0;
    first.latitudeDeg = 60.0;
    first.longitudeDeg = 10.0;
    first.elevationMslM = 9000.0;
    first.localPositionM = {100.0, 9000.0, -200.0};
    first.trueAirspeedMps = 220.0f;
    first.atmosphere.temperatureK = 234.0f;
    first.atmosphere.profile.dewPointLevelCount = 1;
    first.atmosphere.profile.dewPointAltitudeMslM[0] = 9000.0f;
    first.atmosphere.profile.dewPointK[0] = 226.0f;

    const auto normalizedFirst = normalizer.normalize(first);
    require(normalizedFirst.physicsDeltaSeconds == 0.0,
            "first live sample does not advance physics");

    engine::SimulatorSnapshot rebased = first;
    rebased.sequenceNumber = 2;
    rebased.simulatorUptimeSeconds = 10.05;
    rebased.latitudeDeg += 0.00001;
    rebased.localPositionM.x += 100000.0;
    const auto normalizedRebased = normalizer.normalize(rebased);
    require(normalizedRebased.localOriginRebased,
            "live normalizer detects local-origin rebase");
    require(normalizer.localOriginRebaseCount() == 1,
            "live rebase counter increments");
    require(normalizedRebased.physicsDeltaSeconds > 0.0,
            "normal flight advances live physics");

    engine::SimulatorSnapshot paused = rebased;
    paused.sequenceNumber = 3;
    paused.paused = 1;
    paused.simulatorUptimeSeconds = 10.10;
    const auto normalizedPaused = normalizer.normalize(paused);
    require(normalizedPaused.physicsDeltaSeconds == 0.0,
            "pause freezes live physics clock");

    const Fixture fixture = makeDryFixture(1800);
    const auto offline = engine::simulateContrails(fixture.replay, fixture.normalized);
    engine::LiveContrailEngine live;
    engine::LiveContrailEngine liveRepeat;
    const std::vector<engine::Vec3d> exhaustOffsets = {
        {-4.95, -1.50, -0.75},
        {4.95, -1.50, -0.75}
    };
    live.setEngineExhaustBodyOffsets(exhaustOffsets);
    liveRepeat.setEngineExhaustBodyOffsets(exhaustOffsets);
    for (std::size_t index = 0; index < fixture.replay.snapshots.size(); ++index) {
        live.step(fixture.replay.snapshots[index], fixture.normalized.samples[index]);
        liveRepeat.step(fixture.replay.snapshots[index], fixture.normalized.samples[index]);
    }

    require(live.summary().ok, "live contrail engine remains valid");
    require(live.summary().emittedParcelCount == offline.summary.emittedParcelCount,
            "live and offline engines emit the same parcel count");
    require(live.summary().expiredParcelCount == offline.summary.expiredParcelCount,
            "live and offline engines expire the same parcel count");
    require(live.summary().peakActiveParcelCount == offline.summary.peakActiveParcelCount,
            "wake displacement does not change parcel population");
    require(live.summary().deterministicHash == liveRepeat.summary().deterministicHash,
            "repeated live wake simulation produces the same deterministic hash");
    require(live.parcels().size() == liveRepeat.parcels().size(),
            "repeated live simulations finish with the same parcel count");

    engine::LiveContrailEngine geometryEngine;
    geometryEngine.setEngineExhaustBodyOffsets(exhaustOffsets);
    for (std::size_t index = 0; index < 240; ++index) {
        geometryEngine.step(fixture.replay.snapshots[index], fixture.normalized.samples[index]);
    }
    require(geometryEngine.summary().ok,
            "parsed exhaust offsets are accepted by the live engine");
    bool observedRollup = false;
    for (const auto& parcel : geometryEngine.parcels()) {
        if (std::abs(parcel.vortexSide) > 0.5f &&
            (std::abs(parcel.appliedVortexLateralM) > 0.05f ||
             std::abs(parcel.appliedVortexVerticalM) > 0.05f)) {
            observedRollup = true;
            break;
        }
    }
    require(observedRollup,
            "live parcels receive deterministic inward wake-roll-up displacement");

    std::cout << "FFAtmo live contrail world-renderer tests passed\n";
    return 0;
}
