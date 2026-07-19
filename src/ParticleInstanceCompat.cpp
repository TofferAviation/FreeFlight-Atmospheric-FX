#include "ParticleInstance.h"

#include <algorithm>
#include <chrono>

namespace ffatmo {

bool ParticleInstance::load(const std::string& objectPath, std::string* error) {
    return load(objectPath, lineage1000Profile(), error);
}

void ParticleInstance::update(
    const AircraftPose& pose,
    const std::array<float, PublishedDatarefs::InstanceValueCount>& values) {
    const auto now = std::chrono::steady_clock::now();
    float dtSeconds = 1.0f / 60.0f;
    if (hasUpdateClock_) {
        dtSeconds = std::chrono::duration<float>(now - lastUpdateClock_).count();
    }
    lastUpdateClock_ = now;
    hasUpdateClock_ = true;
    update(pose, values, std::clamp(dtSeconds, 0.001f, 0.25f));
}

}  // namespace ffatmo
