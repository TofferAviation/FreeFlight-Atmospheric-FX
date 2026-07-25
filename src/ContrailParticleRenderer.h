#pragma once

#include "render/ContrailRenderPlanner.h"

#include "XPLMDataAccess.h"
#include "XPLMInstance.h"
#include "XPLMScenery.h"
#include "XPLMUtilities.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace ffatmo {

// Renderer Foundation v5.0 proof: use X-Plane's native ribbon particle system
// for the two live condensation streams. The wake solver and render planner stay
// active; this class replaces only the failed OBJ-card drawing path.
class ContrailParticleRenderer {
public:
    static constexpr std::size_t kAssetCount = 1;
    static constexpr std::size_t kInstancesPerAsset = 2;
    static constexpr std::size_t kVisibleCapacity = 1536;
    static constexpr std::size_t kDiagnosticAssetCount = render::kContrailRenderAssetCount;

    using DiagnosticCounts = std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailParticleRenderer() { stop(); }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;
        assetDirectory_ = assetDirectory;
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        poolCapacityDropCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        enabled_ = true;

        rateDataRef_ = registerDataRef("ffatmo/contrail_particle/rate", readRate);
        sizeDataRef_ = registerDataRef("ffatmo/contrail_particle/size", readSize);
        alphaDataRef_ = registerDataRef("ffatmo/contrail_particle/alpha", readAlpha);
        lifetimeDataRef_ = registerDataRef("ffatmo/contrail_particle/lifetime", readLifetime);
        if (!rateDataRef_ || !sizeDataRef_ || !alphaDataRef_ || !lifetimeDataRef_) {
            log("Could not register Renderer v5 particle instance datarefs.\n");
            stop();
            return false;
        }

        const auto objectPath = assetDirectory_ / "contrail_ribbon.obj";
        if (!std::filesystem::exists(objectPath)) {
            log("Missing Renderer v5 particle asset: " + objectPath.string() + "\n");
            stop();
            return false;
        }

        running_ = true;
        XPLMLoadObjectAsync(objectPath.string().c_str(), objectLoadedCallback, this);
        return true;
    }

    void stop() {
        running_ = false;
        destroyInstances();
        if (object_) {
            XPLMUnloadObject(object_);
            object_ = nullptr;
        }
        unregisterDataRef(rateDataRef_);
        unregisterDataRef(sizeDataRef_);
        unregisterDataRef(alphaDataRef_);
        unregisterDataRef(lifetimeDataRef_);
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        active_.fill(false);
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
    }

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;

        if (!enabled_ || !ready()) {
            if (ready()) resetInstances();
            return;
        }

        std::array<const render::ContrailRenderSample*, kInstancesPerAsset> youngest {};
        for (const auto& sample : samples) {
            if (sample.engineIndex >= youngest.size() || !finiteSample(sample)) continue;
            auto*& current = youngest[sample.engineIndex];
            if (!current || sample.ageSeconds < current->ageSeconds ||
                (sample.ageSeconds == current->ageSeconds && sample.nearField && !current->nearField)) {
                current = &sample;
            }
        }

        bool anyRequested = false;
        for (std::size_t engine = 0; engine < youngest.size(); ++engine) {
            if (!youngest[engine]) {
                stopEmitter(engine);
                continue;
            }
            anyRequested = true;
            setEmitter(engine, *youngest[engine]);
            ++visibleInstanceCount_;
        }

        if (!anyRequested && anyActive()) resetInstances();
        selectedPerAsset_[0] = visibleInstanceCount_;
        renderedPerAsset_[0] = visibleInstanceCount_;
    }

    void setEnabled(bool enabled) {
        if (enabled_ == enabled) return;
        enabled_ = enabled;
        if (!enabled_ && ready()) resetInstances();
    }

    bool enabled() const { return enabled_; }
    bool ready() const {
        if (!running_ || !object_) return false;
        for (const auto instance : instances_) {
            if (!instance) return false;
        }
        return true;
    }

    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }
    std::uint64_t poolCapacityDropCount() const { return poolCapacityDropCount_; }
    double maximumBillboardErrorDeg() const { return 0.0; }
    double maximumTrailAlignmentErrorDeg() const { return 0.0; }
    double minimumTrailProjectionFactor() const { return 1.0; }
    double maximumLengthCompressionRatio() const { return 0.0; }
    const DiagnosticCounts& selectedPerAsset() const { return selectedPerAsset_; }
    const DiagnosticCounts& renderedPerAsset() const { return renderedPerAsset_; }

private:
    using FloatReadCallback = float (*)(void*);

    static void log(const std::string& message) {
        XPLMDebugString(("[FFAtmo Contrail Particle Renderer] " + message).c_str());
    }

    XPLMDataRef registerDataRef(const char* name, FloatReadCallback callback) {
        return XPLMRegisterDataAccessor(
            name,
            xplmType_Float,
            0,
            nullptr,
            nullptr,
            callback,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            this,
            nullptr);
    }

    static void unregisterDataRef(XPLMDataRef& dataRef) {
        if (!dataRef) return;
        XPLMUnregisterDataAccessor(dataRef);
        dataRef = nullptr;
    }

    static float readRate(void*) { return 0.0f; }
    static float readSize(void*) { return 0.35f; }
    static float readAlpha(void*) { return 0.0f; }
    static float readLifetime(void*) { return 0.50f; }

    static bool finiteSample(const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.widthM) && sample.widthM > 0.0f &&
               std::isfinite(sample.opacityStrength) && sample.opacityStrength >= 0.0f &&
               std::isfinite(sample.ageSeconds);
    }

    static void objectLoadedCallback(XPLMObjectRef object, void* refcon) {
        auto* self = static_cast<ContrailParticleRenderer*>(refcon);
        if (!self) {
            if (object) XPLMUnloadObject(object);
            return;
        }
        self->objectLoaded(object);
    }

    void objectLoaded(XPLMObjectRef object) {
        if (!object) {
            log("X-Plane could not load contrail_ribbon.obj.\n");
            return;
        }
        if (!running_) {
            XPLMUnloadObject(object);
            return;
        }
        object_ = object;
        loadedObjectCount_ = 1;
        createInstances();
        if (ready()) {
            log("Renderer Foundation v5 native ribbon particle emitters are ready.\n");
        } else {
            log("Renderer v5 could not create both particle emitter instances.\n");
        }
    }

    void createInstances() {
        if (!object_) return;
        const char* datarefs[] = {
            "ffatmo/contrail_particle/rate",
            "ffatmo/contrail_particle/size",
            "ffatmo/contrail_particle/alpha",
            "ffatmo/contrail_particle/lifetime",
            nullptr
        };
        for (std::size_t engine = 0; engine < instances_.size(); ++engine) {
            if (instances_[engine]) continue;
            instances_[engine] = XPLMCreateInstance(object_, datarefs);
            if (!instances_[engine]) {
                ++poolCapacityDropCount_;
                continue;
            }
            hideEmitter(engine);
        }
    }

    void destroyInstances() {
        for (auto& instance : instances_) {
            if (instance) XPLMDestroyInstance(instance);
            instance = nullptr;
        }
        active_.fill(false);
    }

    void resetInstances() {
        if (!object_) return;
        destroyInstances();
        createInstances();
        visibleInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
    }

    bool anyActive() const {
        return std::any_of(active_.begin(), active_.end(), [](bool active) { return active; });
    }

    void setEmitter(std::size_t engine, const render::ContrailRenderSample& sample) {
        if (engine >= instances_.size() || !instances_[engine]) return;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(sample.localPositionM.x);
        drawInfo.y = static_cast<float>(sample.localPositionM.y);
        drawInfo.z = static_cast<float>(sample.localPositionM.z);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = 0.0f;

        const float normalizedSize = std::clamp(sample.widthM / 4.0f, 0.08f, 1.0f);
        const float normalizedAlpha = std::clamp(0.38f + sample.opacityStrength * 2.5f, 0.38f, 1.0f);
        const float values[] = {
            1.0f,
            normalizedSize,
            normalizedAlpha,
            0.55f
        };
        XPLMInstanceSetPosition(instances_[engine], &drawInfo, values);
        active_[engine] = true;
    }

    void stopEmitter(std::size_t engine) {
        if (engine >= instances_.size() || !instances_[engine]) return;
        if (!active_[engine]) return;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        const float values[] = {0.0f, 0.20f, 0.0f, 0.0f};
        XPLMInstanceSetPosition(instances_[engine], &drawInfo, values);
        active_[engine] = false;
    }

    void hideEmitter(std::size_t engine) {
        if (engine >= instances_.size() || !instances_[engine]) return;
        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = -250000.0f - static_cast<float>(engine);
        drawInfo.y = -250000.0f;
        drawInfo.z = -250000.0f;
        const float values[] = {0.0f, 0.20f, 0.0f, 0.0f};
        XPLMInstanceSetPosition(instances_[engine], &drawInfo, values);
        active_[engine] = false;
    }

    std::filesystem::path assetDirectory_;
    XPLMObjectRef object_ = nullptr;
    std::array<XPLMInstanceRef, kInstancesPerAsset> instances_ {};
    std::array<bool, kInstancesPerAsset> active_ {};
    XPLMDataRef rateDataRef_ = nullptr;
    XPLMDataRef sizeDataRef_ = nullptr;
    XPLMDataRef alphaDataRef_ = nullptr;
    XPLMDataRef lifetimeDataRef_ = nullptr;
    DiagnosticCounts selectedPerAsset_ {};
    DiagnosticCounts renderedPerAsset_ {};
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
};

}  // namespace ffatmo
