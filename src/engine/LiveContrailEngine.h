#pragma once

#include "engine/ContrailSimulation.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ffatmo::engine {

// Stateful live version of the deterministic baseline model. It intentionally
// shares the same settings and parcel representation as offline replay so the
// visual debug plugin exercises the same formation/decay contract.
class LiveContrailEngine {
public:
    explicit LiveContrailEngine(ContrailSimulationSettings settings = {});

    void reset();
    void setSettings(const ContrailSimulationSettings& settings);
    void setEngineExhaustBodyOffsets(const std::vector<Vec3d>& offsetsBodyM);

    ContrailTimelineSample step(
        const SimulatorSnapshot& snapshot,
        const diagnostics::NormalizedReplaySample& normalized);

    const std::vector<ContrailParcel>& parcels() const { return parcels_; }
    const ContrailSimulationSummary& summary() const { return summary_; }
    const ContrailSimulationSettings& settings() const { return settings_; }
    const ContrailTimelineSample& lastTimelineSample() const { return lastTimeline_; }

private:
    Vec3d emissionPosition(const diagnostics::NormalizedReplaySample& sample,
                          const SimulatorSnapshot& snapshot,
                          std::uint32_t engineIndex,
                          std::uint32_t engineCount) const;

    ContrailSimulationSettings settings_ {};
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
