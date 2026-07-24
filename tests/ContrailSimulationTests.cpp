#include "engine/ContrailSimulation.h"

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

Fixture makeFixture(std::size_t sampleCount,
                    float temperatureK,
                    float relativeHumidityIcePercent,
                    bool includeFrozenSamples) {
    using namespace ffatmo;
    Fixture fixture;
    fixture.replay.ok = true;
    fixture.replay.metadata.aircraftName = "Boeing 737-800NG";
    fixture.replay.metadata.aircraftIcao = "B738";
    fixture.normalized.summary.ok = true;
    fixture.replay.snapshots.reserve(sampleCount);
    fixture.normalized.samples.reserve(sampleCount);

    double simulationTime = 0.0;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        engine::SimulatorSnapshot snapshot;
        snapshot.sequenceNumber = static_cast<std::uint64_t>(index + 1);
        snapshot.validityMask = engine::ValidTime |
                                engine::ValidGeodeticPosition |
                                engine::ValidLocalTransform |
                                engine::ValidAtmosphereAtAircraft |
                                engine::ValidAtmosphereProfile |
                                engine::ValidPropulsion |
                                engine::ValidAircraftIdentity;
        snapshot.simulatorUptimeSeconds = 100.0 + simulationTime;
        snapshot.elevationMslM = 9000.0;
        snapshot.headingDegTrue = 90.0f;
        snapshot.trueAirspeedMps = 220.0f;
        snapshot.atmosphere.temperatureK = temperatureK;
        snapshot.atmosphere.staticPressurePa = 30000.0f;
        snapshot.atmosphere.densityKgM3 = 0.45f;
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
        engine::copyTruncated(snapshot.aircraftName, "Boeing 737-800NG");
        engine::copyTruncated(snapshot.aircraftIcao, "B738");
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
        normalized.temperatureK = temperatureK;
        normalized.dewPointK = temperatureK - 5.0f;
        normalized.relativeHumidityIcePercent = relativeHumidityIcePercent;
        normalized.meanEngineN1Percent = 90.0f;
        normalized.meanEngineThrustN = 26000.0f;
        normalized.aerodynamicAnglesValid = true;

        if (includeFrozenSamples && index == 350) {
            normalized.physicsDeltaSeconds = 0.0;
            normalized.paused = true;
        }
        if (includeFrozenSamples && index == 700) {
            normalized.physicsDeltaSeconds = 0.0;
            normalized.replaying = true;
        }
        fixture.normalized.samples.push_back(normalized);
        simulationTime += normalized.physicsDeltaSeconds;
    }
    fixture.normalized.summary.normalizedSampleCount = sampleCount;
    fixture.normalized.summary.inputSnapshotCount = sampleCount;
    return fixture;
}

}  // namespace

int main() {
    using namespace ffatmo;

    const Fixture dryFixture = makeFixture(1800, 234.0f, 72.0f, true);
    const auto dryFirst = engine::simulateContrails(dryFixture.replay, dryFixture.normalized);
    const auto drySecond = engine::simulateContrails(dryFixture.replay, dryFixture.normalized);

    require(dryFirst.summary.ok, "cold dry contrail simulation succeeds: " + dryFirst.summary.error);
    require(dryFirst.summary.emittedParcelCount > 0,
            "cold high-altitude engines produce initial contrail parcels");
    require(dryFirst.summary.expiredParcelCount > 0,
            "dry-air sublimation expires parcels");
    require(dryFirst.summary.peakActiveParcelCount > 0,
            "active trail population is measured");
    require(dryFirst.summary.maximumVisibleTrailLengthM > 0.0,
            "visible trail length is measured");
    require(dryFirst.summary.persistentEnvironmentSampleCount == 0,
            "dry fixture is not classified as ice supersaturated");
    require(dryFirst.summary.frozenPhysicsSampleCount >= 3,
            "initial, pause, and replay zero-advance samples are counted");
    require(dryFirst.timeline[350].physicsFrozen,
            "pause sample freezes contrail physics");
    require(dryFirst.timeline[350].activeParcelCount == dryFirst.timeline[349].activeParcelCount,
            "pause does not age, emit, or remove parcels");
    require(dryFirst.timeline[700].physicsFrozen,
            "X-Plane replay sample freezes contrail physics");
    require(dryFirst.timeline[700].activeParcelCount == dryFirst.timeline[699].activeParcelCount,
            "X-Plane replay does not age, emit, or remove parcels");
    require(dryFirst.summary.deterministicHash == drySecond.summary.deterministicHash,
            "same replay and settings produce identical contrail hash");

    const Fixture warmFixture = makeFixture(400, 270.0f, 80.0f, false);
    const auto warmResult = engine::simulateContrails(warmFixture.replay, warmFixture.normalized);
    require(warmResult.summary.ok, "warm fixture simulation succeeds");
    require(warmResult.summary.emittedParcelCount == 0,
            "warm air blocks contrail formation");

    const Fixture persistentFixture = makeFixture(900, 232.0f, 112.0f, false);
    const auto persistentResult = engine::simulateContrails(
        persistentFixture.replay, persistentFixture.normalized);
    require(persistentResult.summary.ok, "supersaturated fixture simulation succeeds");
    require(persistentResult.summary.persistentEnvironmentSampleCount > 0,
            "ice-supersaturated samples are counted");
    require(persistentResult.summary.maximumTotalNormalizedIceMass >
            dryFirst.summary.maximumTotalNormalizedIceMass * 0.5f,
            "supersaturated air preserves or grows the young trail");

    engine::ContrailSimulationSettings tinyCapacity;
    tinyCapacity.maximumActiveParcels = 2;
    const auto capacityResult = engine::simulateContrails(
        persistentFixture.replay, persistentFixture.normalized, tinyCapacity);
    require(capacityResult.summary.ok, "capacity-limited simulation remains valid");
    require(capacityResult.summary.capacityDropCount > 0,
            "bounded parcel capacity drops excess emission deterministically");
    require(capacityResult.summary.peakActiveParcelCount <= 2,
            "active parcel count respects configured capacity");

    std::cout << "FFAtmo contrail formation and decay tests passed\n";
    return 0;
}
