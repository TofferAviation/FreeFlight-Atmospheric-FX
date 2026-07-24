#pragma once

#include "AircraftProfile.h"
#include "PublishedDatarefs.h"
#include "SimDatarefs.h"

#include "XPLMInstance.h"
#include "XPLMScenery.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace ffatmo {

class ParticleInstance {
public:
    ~ParticleInstance();

    bool load(const std::string& objectPath, std::string* error = nullptr);
    bool load(const std::string& objectPath,
              const AircraftProfile& profile,
              std::string* error = nullptr);
    void unload();
    void resetTrail();
    void update(const AircraftPose& pose,
                const std::array<float, PublishedDatarefs::InstanceValueCount>& values);
    void update(const AircraftPose& pose,
                const std::array<float, PublishedDatarefs::InstanceValueCount>& values,
                float dtSeconds);
    bool loaded() const { return liveInstance_ != nullptr; }

private:
    struct WakeNode {
        XPLMInstanceRef leftInstance = nullptr;
        XPLMInstanceRef rightInstance = nullptr;
        AircraftPose sourcePose {};
        std::array<float, PublishedDatarefs::InstanceValueCount> sourceValues {};
        float ageSeconds = 0.0f;
        bool active = false;
    };

    static XPLMDrawInfo_t drawInfo(const AircraftPose& pose);
    static AircraftPose offsetPose(const AircraftPose& pose,
                                   float localRightMetres,
                                   float localUpMetres,
                                   float localAftMetres = 0.0f);
    static void setPosition(
        XPLMInstanceRef instance,
        const AircraftPose& pose,
        const std::array<float, PublishedDatarefs::InstanceValueCount>& values);

    bool createWakePool(std::string* error);
    void spawnWakeNode(
        const AircraftPose& pose,
        const std::array<float, PublishedDatarefs::InstanceValueCount>& values);
    void updateWakeNode(WakeNode& node, float dtSeconds);
    void silenceWakeNode(WakeNode& node);

    XPLMObjectRef object_ = nullptr;
    XPLMInstanceRef liveInstance_ = nullptr;
    std::vector<WakeNode> wakeNodes_;
    std::size_t nextWakeNode_ = 0;
    float wakeSampleAccumulator_ = 0.0f;
    float leftEngineX_ = -4.5f;
    float rightEngineX_ = 4.5f;
    bool hadActiveContrail_ = false;
    bool hasLastPose_ = false;
    AircraftPose lastPose_ {};
    bool hasUpdateClock_ = false;
    std::chrono::steady_clock::time_point lastUpdateClock_ {};
};

}  // namespace ffatmo
