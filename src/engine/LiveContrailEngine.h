#pragma once

#include "engine/ContrailSimulation.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ffatmo::engine {

// Stateful live version of the deterministic baseline model. Formation and
// moisture decay remain compatible with offline replay; Wake Fluid Simulation
// v1 is a live-only analytical flow field attached to each emitted parcel.
class LiveContrailEngine {
public:
    explicit LiveContrailEngine(ContrailSimulationSettings settings = {});

    void reset();
    void setSettings(const ContrailSimulationSettings& settings);
    void setEngineExhaustBodyOffsets(const std::vector<Vec3d>& offsetsBodyM);
    void setWakeAircraftGeometry(float wingspanM, float referenceMassKg);
    void setWakeFluidSettings(const WakeFluidSettings& settings);

    ContrailTimelineSample step(
        const SimulatorSnapshot& snapshot,
        const diagnostics::NormalizedReplaySample& normalized);

    const std::vector<ContrailParcel>& parcels() const { return parcels_; }
    const ContrailSimulationSummary& summary() const { return summary_; }
    const ContrailSimulationSettings& settings() const { return settings_; }
    const WakeFluidSettings& wakeFluidSettings() const { return wakeFluidSettings_; }
    const WakeFluidAircraftGeometry& wakeAircraftGeometry() const { return wakeAircraft_; }
    const ContrailTimelineSample& lastTimelineSample() const { return lastTimeline_; }

private:
    Vec3d emissionPosition(const diagnostics::NormalizedReplaySample& sample,
                           const SimulatorSnapshot& snapshot,
                           std::uint32_t engineIndex,
                           std::uint32_t engineCount) const;

    ContrailSimulationSettings settings_ {};
    WakeFluidSettings wakeFluidSettings_ {};
    WakeFluidAircraftGeometry wakeAircraft_ {};
    std::vector<Vec3d> engineExhaustBodyOffsets_;
    std::vector<ContrailParcel> parcels_;
    std::array<float, kMaximumRecordedEngines> emissionAccumulator_ {};
    ContrailSimulationSummary summary_ {};
    ContrailTimelineSample lastTimeline_ {};
    std::uint64_t nextParcelId_ = 1;
    std::uint64_t deterministicHash_ = 1469598103934665603ull;
    bool encounteredNonFinite_ = false;
};

}  // namespace ffatmo::engine
