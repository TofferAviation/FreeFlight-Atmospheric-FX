#pragma once

#include "PublishedDatarefs.h"
#include "SimDatarefs.h"

#include "XPLMInstance.h"
#include "XPLMScenery.h"

#include <string>

namespace ffatmo {

class ParticleInstance {
public:
    ~ParticleInstance();

    bool load(const std::string& objectPath, std::string* error = nullptr);
    void unload();
    void update(const AircraftPose& pose,
                const std::array<float, PublishedDatarefs::InstanceValueCount>& values);
    bool loaded() const { return instance_ != nullptr; }

private:
    XPLMObjectRef object_ = nullptr;
    XPLMInstanceRef instance_ = nullptr;
};

}  // namespace ffatmo
