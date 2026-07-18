#include "ParticleInstance.h"

namespace ffatmo {

ParticleInstance::~ParticleInstance() {
    unload();
}

bool ParticleInstance::load(const std::string& objectPath, std::string* error) {
    unload();
    object_ = XPLMLoadObject(objectPath.c_str());
    if (!object_) {
        if (error) *error = "XPLM could not load particle object: " + objectPath;
        return false;
    }

    const auto& names = PublishedDatarefs::instanceNames();
    instance_ = XPLMCreateInstance(object_, const_cast<const char**>(names.data()));
    if (!instance_) {
        if (error) *error = "XPLM could not create particle object instance";
        XPLMUnloadObject(object_);
        object_ = nullptr;
        return false;
    }
    return true;
}

void ParticleInstance::unload() {
    if (instance_) {
        XPLMDestroyInstance(instance_);
        instance_ = nullptr;
    }
    if (object_) {
        XPLMUnloadObject(object_);
        object_ = nullptr;
    }
}

void ParticleInstance::update(
    const AircraftPose& pose,
    const std::array<float, PublishedDatarefs::InstanceValueCount>& values) {
    if (!instance_) return;
    XPLMDrawInfo_t draw {};
    draw.structSize = sizeof(draw);
    draw.x = static_cast<float>(pose.localX);
    draw.y = static_cast<float>(pose.localY);
    draw.z = static_cast<float>(pose.localZ);
    draw.pitch = pose.pitchDeg;
    draw.heading = pose.headingDeg;
    draw.roll = pose.rollDeg;
    XPLMInstanceSetPosition(instance_, &draw, values.data());
}

}  // namespace ffatmo
