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
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ffatmo {

// Renderer Foundation v5.0.1 safe-start proof.
//
// The particle OBJ is deliberately NOT loaded from XPluginStart/XPluginEnable.
// start() only registers instance datarefs and arms the renderer. The tiny OBJ
// is loaded synchronously only after real contrail samples have existed for
// three consecutive flight-loop updates. At that point the user aircraft and
// X-Plane world/scenery systems are operational.
class ContrailParticleRenderer {
public:
    static constexpr std::size_t kAssetCount = 1;
    static constexpr std::size_t kInstancesPerAsset = 2;
    static constexpr std::size_t kVisibleCapacity = 1536;
    static constexpr std::size_t kDiagnosticAssetCount =
        render::kContrailRenderAssetCount;

    using DiagnosticCounts =
        std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailParticleRenderer() { stop(); }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;

        assetDirectory_ = assetDirectory;
        objectPath_ = assetDirectory_ / "contrail_ribbon.obj";
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        poolCapacityDropCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        active_.fill(false);
        enabled_ = true;
        loadAttempted_ = false;
        deferredNonEmptyFrameCount_ = 0;

        rateDataRef_ = registerDataRef(
            "ffatmo/contrail_particle/rate", readRate);
        sizeDataRef_ = registerDataRef(
            "ffatmo/contrail_particle/size", readSize);
        alphaDataRef_ = registerDataRef(
            "ffatmo/contrail_particle/alpha", readAlpha);
        lifetimeDataRef_ = registerDataRef(
            "ffatmo/contrail_particle/lifetime", readLifetime);

        if (!rateDataRef_ || !sizeDataRef_ ||
            !alphaDataRef_ || !lifetimeDataRef_) {
            log("Could not register Renderer v5.0.1 particle instance datarefs.\n");
            stop();
            return false;
        }

        if (!std::filesystem::exists(objectPath_)) {
            log("Missing Renderer v5.0.1 particle asset: " +
                objectPath_.string() + "\n");
            stop();
            return false;
        }

        running_ = true;
        log("Renderer v5.0.1 armed; particle OBJ load is deferred until live trail samples exist.\n");
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

        assetDirectory_.clear();
        objectPath_.clear();
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        active_.fill(false);
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        loadAttempted_ = false;
        deferredNonEmptyFrameCount_ = 0;
    }

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;

        if (!enabled_ || !running_) {
            if (ready()) resetInstances();
            return;
        }

        if (!ready()) {
            // An empty update is common during startup, loading screens, while
            // waiting for the ACF, and while the preview gate is closed. It must
            // never trigger an OBJ or particle-system load.
            if (samples.empty()) {
                deferredNonEmptyFrameCount_ = 0;
                return;
            }

            if (!object_ && !loadAttempted_) {
                ++deferredNonEmptyFrameCount_;
                if (deferredNonEmptyFrameCount_ < 3) return;

                // The object is tiny. A synchronous load at this late, gated
                // point avoids the uncancellable async callback and guarantees
                // that no loader request is issued from plugin startup.
                loadAttempted_ = true;
                object_ = XPLMLoadObject(objectPath_.string().c_str());
                if (!object_) {
                    log("Renderer v5.0.1 could not load the deferred particle OBJ.\n");
                    return;
                }

                loadedObjectCount_ = 1;
                createInstances();
                if (ready()) {
                    log("Renderer v5.0.1 native ribbon emitters loaded after the safe-start gate.\n");
                } else {
                    log("Renderer v5.0.1 could not create both particle emitter instances.\n");
                }
            }

            if (!ready()) return;
        }

        std::array<const render::ContrailRenderSample*,
                   kInstancesPerAsset> youngest {};
        for (const auto& sample : samples) {
            if (sample.engineIndex >= youngest.size() ||
                !finiteSample(sample)) {
                continue;
            }

            auto*& current = youngest[sample.engineIndex];
            if (!current || sample.ageSeconds < current->ageSeconds ||
                (sample.ageSeconds == current->ageSeconds &&
                 sample.nearField && !current->nearField)) {
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

    std::size_t visibleInstanceCount() const {
        return visibleInstanceCount_;
    }

    std::size_t loadedObjectCount() const {
        return loadedObjectCount_;
    }

    std::uint64_t poolCapacityDropCount() const {
        return poolCapacityDropCount_;
    }

    double maximumBillboardErrorDeg() const { return 0.0; }
    double maximumTrailAlignmentErrorDeg() const { return 0.0; }
    double minimumTrailProjectionFactor() const { return 1.0; }
    double maximumLengthCompressionRatio() const { return 0.0; }

    const DiagnosticCounts& selectedPerAsset() const {
        return selectedPerAsset_;
    }

    const DiagnosticCounts& renderedPerAsset() const {
        return renderedPerAsset_;
    }

private:
    using FloatReadCallback = float (*)(void*);

    static void log(const std::string& message) {
        XPLMDebugString(
            ("[FFAtmo Contrail Particle Renderer] " + message).c_str());
    }

    XPLMDataRef registerDataRef(const char* name,
                                FloatReadCallback callback) {
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

    static bool finiteSample(
        const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.widthM) &&
               sample.widthM > 0.0f &&
               std::isfinite(sample.opacityStrength) &&
               sample.opacityStrength >= 0.0f &&
               std::isfinite(sample.ageSeconds);
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

        for (std::size_t engine = 0;
             engine < instances_.size();
             ++engine) {
            if (instances_[engine]) continue;

            instances_[engine] =
                XPLMCreateInstance(object_, datarefs);
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
        return std::any_of(
            active_.begin(),
            active_.end(),
            [](bool active) { return active; });
    }

    void setEmitter(
        std::size_t engine,
        const render::ContrailRenderSample& sample) {
        if (engine >= instances_.size() ||
            !instances_[engine]) {
            return;
        }

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(sample.localPositionM.x);
        drawInfo.y = static_cast<float>(sample.localPositionM.y);
        drawInfo.z = static_cast<float>(sample.localPositionM.z);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = 0.0f;

        const float normalizedSize = std::clamp(
            sample.widthM / 4.0f,
            0.08f,
            1.0f);
        const float normalizedAlpha = std::clamp(
            0.38f + sample.opacityStrength * 2.5f,
            0.38f,
            1.0f);
        const float values[] = {
            1.0f,
            normalizedSize,
            normalizedAlpha,
            0.55f
        };

        XPLMInstanceSetPosition(
            instances_[engine],
            &drawInfo,
            values);
        active_[engine] = true;
    }

    void stopEmitter(std::size_t engine) {
        if (engine >= instances_.size() ||
            !instances_[engine] ||
            !active_[engine]) {
            return;
        }

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        const float values[] = {
            0.0f,
            0.20f,
            0.0f,
            0.0f
        };
        XPLMInstanceSetPosition(
            instances_[engine],
            &drawInfo,
            values);
        active_[engine] = false;
    }

    void hideEmitter(std::size_t engine) {
        if (engine >= instances_.size() ||
            !instances_[engine]) {
            return;
        }

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = -250000.0f - static_cast<float>(engine);
        drawInfo.y = -250000.0f;
        drawInfo.z = -250000.0f;
        const float values[] = {
            0.0f,
            0.20f,
            0.0f,
            0.0f
        };
        XPLMInstanceSetPosition(
            instances_[engine],
            &drawInfo,
            values);
        active_[engine] = false;
    }

    std::filesystem::path assetDirectory_;
    std::filesystem::path objectPath_;
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
    std::size_t deferredNonEmptyFrameCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
    bool loadAttempted_ = false;
};

}  // namespace ffatmo
