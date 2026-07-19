#include "ParticleInstance.h"

#include <algorithm>
#include <cmath>

namespace ffatmo {
namespace {

constexpr std::size_t kWakeNodeCount = 28;
constexpr float kWakeSampleIntervalSeconds = 0.24f;
constexpr float kCaptureDelaySeconds = 0.75f;
constexpr float kCaptureDurationSeconds = 5.50f;
constexpr float kWakeNodeRetireSeconds = 8.50f;
constexpr float kPi = 3.14159265358979323846f;

float smoothstep(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float squaredDistance(const AircraftPose& a, const AircraftPose& b) {
    const double dx = a.localX - b.localX;
    const double dy = a.localY - b.localY;
    const double dz = a.localZ - b.localZ;
    return static_cast<float>(dx * dx + dy * dy + dz * dz);
}

}  // namespace

ParticleInstance::~ParticleInstance() {
    unload();
}

bool ParticleInstance::load(const std::string& objectPath,
                            const AircraftProfile& profile,
                            std::string* error) {
    unload();
    object_ = XPLMLoadObject(objectPath.c_str());
    if (!object_) {
        if (error) *error = "XPLM could not load particle object: " + objectPath;
        return false;
    }

    const auto& names = PublishedDatarefs::instanceNames();
    liveInstance_ = XPLMCreateInstance(object_, const_cast<const char**>(names.data()));
    if (!liveInstance_) {
        if (error) *error = "XPLM could not create live particle object instance";
        unload();
        return false;
    }

    leftEngineX_ = profile.engineExhaust[0].x;
    rightEngineX_ = profile.engineExhaust[1].x;
    if (!createWakePool(error)) {
        unload();
        return false;
    }
    return true;
}

bool ParticleInstance::createWakePool(std::string* error) {
    const auto& names = PublishedDatarefs::instanceNames();
    wakeNodes_.resize(kWakeNodeCount);
    for (WakeNode& node : wakeNodes_) {
        node.leftInstance = XPLMCreateInstance(object_, const_cast<const char**>(names.data()));
        node.rightInstance = XPLMCreateInstance(object_, const_cast<const char**>(names.data()));
        if (!node.leftInstance || !node.rightInstance) {
            if (error) *error = "XPLM could not create the contrail wake-instance pool";
            return false;
        }
    }
    return true;
}

void ParticleInstance::unload() {
    for (WakeNode& node : wakeNodes_) {
        if (node.leftInstance) XPLMDestroyInstance(node.leftInstance);
        if (node.rightInstance) XPLMDestroyInstance(node.rightInstance);
        node.leftInstance = nullptr;
        node.rightInstance = nullptr;
    }
    wakeNodes_.clear();
    if (liveInstance_) {
        XPLMDestroyInstance(liveInstance_);
        liveInstance_ = nullptr;
    }
    if (object_) {
        XPLMUnloadObject(object_);
        object_ = nullptr;
    }
    nextWakeNode_ = 0;
    wakeSampleAccumulator_ = 0.0f;
    hadActiveContrail_ = false;
    hasLastPose_ = false;
}

void ParticleInstance::resetTrail() {
    for (WakeNode& node : wakeNodes_) {
        silenceWakeNode(node);
        node.active = false;
        node.ageSeconds = 0.0f;
    }
    nextWakeNode_ = 0;
    wakeSampleAccumulator_ = 0.0f;
    hadActiveContrail_ = false;
    hasLastPose_ = false;
}

XPLMDrawInfo_t ParticleInstance::drawInfo(const AircraftPose& pose) {
    XPLMDrawInfo_t draw {};
    draw.structSize = sizeof(draw);
    draw.x = static_cast<float>(pose.localX);
    draw.y = static_cast<float>(pose.localY);
    draw.z = static_cast<float>(pose.localZ);
    draw.pitch = pose.pitchDeg;
    draw.heading = pose.headingDeg;
    draw.roll = pose.rollDeg;
    return draw;
}

AircraftPose ParticleInstance::offsetPose(const AircraftPose& pose,
                                          float localRightMetres,
                                          float localUpMetres,
                                          float localAftMetres) {
    const float headingRadians = pose.headingDeg * kPi / 180.0f;
    const float cosine = std::cos(headingRadians);
    const float sine = std::sin(headingRadians);

    AircraftPose shifted = pose;
    shifted.localX += static_cast<double>(cosine * localRightMetres - sine * localAftMetres);
    shifted.localY += static_cast<double>(localUpMetres);
    shifted.localZ += static_cast<double>(sine * localRightMetres + cosine * localAftMetres);
    return shifted;
}

void ParticleInstance::setPosition(
    XPLMInstanceRef instance,
    const AircraftPose& pose,
    const std::array<float, PublishedDatarefs::InstanceValueCount>& values) {
    if (!instance) return;
    XPLMDrawInfo_t draw = drawInfo(pose);
    XPLMInstanceSetPosition(instance, &draw, values.data());
}

void ParticleInstance::silenceWakeNode(WakeNode& node) {
    std::array<float, PublishedDatarefs::InstanceValueCount> zero {};
    setPosition(node.leftInstance, node.sourcePose, zero);
    setPosition(node.rightInstance, node.sourcePose, zero);
}

void ParticleInstance::spawnWakeNode(
    const AircraftPose& pose,
    const std::array<float, PublishedDatarefs::InstanceValueCount>& values) {
    if (wakeNodes_.empty()) return;
    WakeNode& node = wakeNodes_[nextWakeNode_];
    node.sourcePose = pose;
    node.sourceValues = values;
    node.ageSeconds = 0.0f;
    node.active = true;
    nextWakeNode_ = (nextWakeNode_ + 1) % wakeNodes_.size();
}

void ParticleInstance::updateWakeNode(WakeNode& node, float dtSeconds) {
    if (!node.active) return;
    node.ageSeconds += dtSeconds;
    if (node.ageSeconds >= kWakeNodeRetireSeconds) {
        silenceWakeNode(node);
        node.active = false;
        return;
    }

    const float rawCapture =
        (node.ageSeconds - kCaptureDelaySeconds) / kCaptureDurationSeconds;
    const float capture = std::clamp(rawCapture, 0.0f, 1.0f);
    const float eased = smoothstep(capture);

    const float initialRadius = std::max(0.65f, (rightEngineX_ - leftEngineX_) * 0.168f);
    const float radius = lerp(initialRadius, 0.32f, eased);
    const float centre = lerp(3.0f, 2.20f, eased);
    const float orbit = 1.55f * kPi * eased;
    const float leftAngle = kPi - orbit;
    const float rightAngle = orbit;

    const float leftDesiredX = -centre + radius * std::cos(leftAngle);
    const float rightDesiredX = centre + radius * std::cos(rightAngle);
    const float descent = -2.65f * eased;
    const float leftDesiredY = descent + radius * 0.62f * std::sin(leftAngle);
    const float rightDesiredY = descent + radius * 0.62f * std::sin(rightAngle);

    const AircraftPose leftPose =
        offsetPose(node.sourcePose, leftDesiredX - leftEngineX_, leftDesiredY);
    const AircraftPose rightPose =
        offsetPose(node.sourcePose, rightDesiredX - rightEngineX_, rightDesiredY);

    std::array<float, PublishedDatarefs::InstanceValueCount> leftValues {};
    std::array<float, PublishedDatarefs::InstanceValueCount> rightValues {};

    // Fade the moving parcel in after the straight core, then out before node reuse.
    const float wakeEnvelope = capture <= 0.0f
        ? 0.0f
        : std::pow(std::max(0.0f, std::sin(kPi * capture)), 0.72f);
    const float nodeOutput = 0.060f * wakeEnvelope;
    const float coreDemand = std::max(
        node.sourceValues[PublishedDatarefs::ContrailCore],
        node.sourceValues[PublishedDatarefs::PrimaryWake]);

    leftValues[PublishedDatarefs::EngineLeft] =
        node.sourceValues[PublishedDatarefs::EngineLeft];
    leftValues[PublishedDatarefs::PrimaryWake] = coreDemand * nodeOutput;
    leftValues[PublishedDatarefs::ParticleBudget] =
        node.sourceValues[PublishedDatarefs::ParticleBudget];
    leftValues[PublishedDatarefs::WakeCapture] = wakeEnvelope;
    leftValues[PublishedDatarefs::WakeLifetimeSeconds] = 2.8f;
    leftValues[PublishedDatarefs::VortexPhase] = capture;

    rightValues[PublishedDatarefs::EngineRight] =
        node.sourceValues[PublishedDatarefs::EngineRight];
    rightValues[PublishedDatarefs::PrimaryWake] = coreDemand * nodeOutput;
    rightValues[PublishedDatarefs::ParticleBudget] =
        node.sourceValues[PublishedDatarefs::ParticleBudget];
    rightValues[PublishedDatarefs::WakeCapture] = wakeEnvelope;
    rightValues[PublishedDatarefs::WakeLifetimeSeconds] = 2.8f;
    rightValues[PublishedDatarefs::VortexPhase] = capture;

    setPosition(node.leftInstance, leftPose, leftValues);
    setPosition(node.rightInstance, rightPose, rightValues);
}

void ParticleInstance::update(
    const AircraftPose& pose,
    const std::array<float, PublishedDatarefs::InstanceValueCount>& values,
    float dtSeconds) {
    if (!liveInstance_) return;

    if (hasLastPose_ && squaredDistance(lastPose_, pose) > 250000.0f) {
        resetTrail();
    }
    lastPose_ = pose;
    hasLastPose_ = true;

    // The aircraft-attached instance only creates the young, separated engine cores.
    // Historical wake instances below create the rolled-up vortex path.
    auto liveValues = values;
    liveValues[PublishedDatarefs::PrimaryWake] = 0.0f;
    liveValues[PublishedDatarefs::SecondaryCurtain] = 0.0f;
    liveValues[PublishedDatarefs::ContrailCirrus] = 0.0f;
    liveValues[PublishedDatarefs::WakeCapture] = 0.0f;
    setPosition(liveInstance_, pose, liveValues);

    const bool activeContrail =
        values[PublishedDatarefs::ContrailCore] > 0.01f &&
        (values[PublishedDatarefs::EngineLeft] > 0.01f ||
         values[PublishedDatarefs::EngineRight] > 0.01f);

    if (activeContrail) {
        wakeSampleAccumulator_ += std::clamp(dtSeconds, 0.0f, 0.25f);
        if (!hadActiveContrail_ || wakeSampleAccumulator_ >= kWakeSampleIntervalSeconds) {
            spawnWakeNode(pose, values);
            wakeSampleAccumulator_ = 0.0f;
        }
    } else {
        wakeSampleAccumulator_ = 0.0f;
    }
    hadActiveContrail_ = activeContrail;

    for (WakeNode& node : wakeNodes_) updateWakeNode(node, dtSeconds);
}

}  // namespace ffatmo
