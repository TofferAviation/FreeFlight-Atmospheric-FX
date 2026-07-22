#pragma once

#include "diagnostics/ReplayRunner.h"
#include "engine/SimulatorSnapshot.h"

#include <cstdint>

namespace ffatmo::diagnostics {

// Stateful live counterpart to the offline replay normalizer. It keeps an
// engine-owned geodetic ENU frame so persistent effects survive X-Plane local
// coordinate rebases, and it freezes the physics clock during pause/replay.
class LiveSnapshotNormalizer {
public:
    void reset();
    NormalizedReplaySample normalize(const engine::SimulatorSnapshot& snapshot);

    bool initialized() const { return initialized_; }
    std::uint64_t localOriginRebaseCount() const { return localOriginRebaseCount_; }
    double integratedPhysicsTimeSeconds() const { return integratedPhysicsTimeSeconds_; }

private:
    struct Ecef {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    Ecef anchorEcef_ {};
    double anchorLatitudeDeg_ = 0.0;
    double anchorLongitudeDeg_ = 0.0;
    engine::SimulatorSnapshot previous_ {};
    bool initialized_ = false;
    std::uint64_t localOriginRebaseCount_ = 0;
    double integratedPhysicsTimeSeconds_ = 0.0;
};

}  // namespace ffatmo::diagnostics
