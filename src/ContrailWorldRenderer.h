#pragma once

#include "ContrailDebugOverlay.h"

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

class ContrailWorldRenderer {
public:
    static constexpr std::size_t kOpacityBucketCount = 4;
    static constexpr std::size_t kInstancesPerBucket = 144;

    ContrailWorldRenderer() {
        for (std::size_t index = 0; index < loadContexts_.size(); ++index) {
            loadContexts_[index].owner = this;
            loadContexts_[index].bucket = index;
        }
    }

    ~ContrailWorldRenderer() {
        stop();
    }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;
        assetDirectory_ = assetDirectory;
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        enabled_ = true;

        scaleDataRef_ = XPLMRegisterDataAccessor(
            "ffatmo/contrail_debug/scale",
            xplmType_Float,
            0,
            nullptr,
            nullptr,
            readScale,
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
        if (!scaleDataRef_) {
            log("Could not register the instance scale dataref.\n");
            return false;
        }

        running_ = true;
        for (std::size_t bucket = 0; bucket < pools_.size(); ++bucket) {
            const auto objectPath = assetDirectory_ /
                ("contrail_puff_" + std::to_string(bucket) + ".obj");
            if (!std::filesystem::exists(objectPath)) {
                log("Missing asset: " + objectPath.string() + "\n");
                stop();
                return false;
            }
            XPLMLoadObjectAsync(objectPath.string().c_str(),
                                objectLoadedCallback,
                                &loadContexts_[bucket]);
        }
        return true;
    }

    void stop() {
        running_ = false;
        visibleInstanceCount_ = 0;
        loadedObjectCount_ = 0;
        for (auto& pool : pools_) {
            for (XPLMInstanceRef instance : pool.instances) {
                if (instance) XPLMDestroyInstance(instance);
            }
            pool.instances.clear();
            if (pool.object) {
                XPLMUnloadObject(pool.object);
                pool.object = nullptr;
            }
        }
        if (scaleDataRef_) {
            XPLMUnregisterDataAccessor(scaleDataRef_);
            scaleDataRef_ = nullptr;
        }
    }

    void update(const std::vector<ContrailDebugRenderParcel>& parcels) {
        if (!enabled_ || !ready()) {
            if (ready()) hideAll();
            return;
        }

        std::vector<const ContrailDebugRenderParcel*> visible;
        visible.reserve(parcels.size());
        for (const auto& parcel : parcels) {
            if (parcel.opticalDepth >= 0.010f &&
                std::isfinite(parcel.localPositionM.x) &&
                std::isfinite(parcel.localPositionM.y) &&
                std::isfinite(parcel.localPositionM.z) &&
                std::isfinite(parcel.radiusM)) {
                visible.push_back(&parcel);
            }
        }

        constexpr std::size_t maximumVisible =
            kOpacityBucketCount * kInstancesPerBucket;
        std::array<std::vector<const ContrailDebugRenderParcel*>, kOpacityBucketCount> buckets;
        const std::size_t selectedCount = std::min(visible.size(), maximumVisible);
        for (std::size_t selected = 0; selected < selectedCount; ++selected) {
            std::size_t sourceIndex = selected;
            if (selectedCount > 1 && visible.size() > selectedCount) {
                sourceIndex = static_cast<std::size_t>(std::llround(
                    static_cast<double>(selected) *
                    static_cast<double>(visible.size() - 1) /
                    static_cast<double>(selectedCount - 1)));
            }
            const auto* parcel = visible[sourceIndex];
            buckets[opacityBucket(*parcel)].push_back(parcel);
        }

        visibleInstanceCount_ = 0;
        for (std::size_t bucket = 0; bucket < pools_.size(); ++bucket) {
            auto& pool = pools_[bucket];
            auto& assigned = buckets[bucket];
            if (assigned.size() > pool.instances.size()) {
                assigned.erase(assigned.begin(),
                               assigned.begin() + static_cast<std::ptrdiff_t>(
                                   assigned.size() - pool.instances.size()));
            }

            std::size_t index = 0;
            for (; index < assigned.size(); ++index) {
                const auto& parcel = *assigned[index];
                const float ageExpansion = 1.0f +
                    0.18f * std::clamp(parcel.ageSeconds / 55.0f, 0.0f, 1.0f);
                const float scaleM = std::clamp(parcel.radiusM * ageExpansion,
                                                0.70f,
                                                28.0f);
                const float roll = static_cast<float>(
                    (parcel.parcelId * 47u + parcel.engineIndex * 83u) % 360u);
                setInstance(pool.instances[index], parcel, scaleM, roll);
                ++visibleInstanceCount_;
            }
            for (; index < pool.instances.size(); ++index) {
                hideInstance(pool.instances[index],
                             index + bucket * kInstancesPerBucket);
            }
        }
    }

    void setEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) hideAll();
    }

    bool enabled() const { return enabled_; }

    bool ready() const {
        if (!running_ || loadedObjectCount_ != pools_.size()) return false;
        for (const auto& pool : pools_) {
            if (!pool.object || pool.instances.empty()) return false;
        }
        return true;
    }

    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }

private:
    struct Pool {
        XPLMObjectRef object = nullptr;
        std::vector<XPLMInstanceRef> instances;
    };

    struct LoadContext {
        ContrailWorldRenderer* owner = nullptr;
        std::size_t bucket = 0;
    };

    static void log(const std::string& message) {
        XPLMDebugString(("[FFAtmo Contrail World Renderer] " + message).c_str());
    }

    static float readScale(void*) {
        return 1.0f;
    }

    static void objectLoadedCallback(XPLMObjectRef object, void* refcon) {
        auto* context = static_cast<LoadContext*>(refcon);
        if (!context || !context->owner) {
            if (object) XPLMUnloadObject(object);
            return;
        }
        context->owner->objectLoaded(context->bucket, object);
    }

    void objectLoaded(std::size_t bucket, XPLMObjectRef object) {
        if (!object) {
            log("X-Plane could not load opacity bucket " +
                std::to_string(bucket) + ".\n");
            return;
        }
        if (!running_ || bucket >= pools_.size()) {
            XPLMUnloadObject(object);
            return;
        }

        auto& pool = pools_[bucket];
        pool.object = object;
        const char* datarefs[] = {"ffatmo/contrail_debug/scale", nullptr};
        pool.instances.reserve(kInstancesPerBucket);
        for (std::size_t index = 0; index < kInstancesPerBucket; ++index) {
            XPLMInstanceRef instance = XPLMCreateInstance(object, datarefs);
            if (!instance) break;
            pool.instances.push_back(instance);
            hideInstance(instance, index + bucket * kInstancesPerBucket);
        }
        if (pool.instances.empty()) {
            log("No instances could be created for opacity bucket " +
                std::to_string(bucket) + ".\n");
            return;
        }
        ++loadedObjectCount_;
        log("Loaded opacity bucket " + std::to_string(bucket) +
            " with " + std::to_string(pool.instances.size()) +
            " pooled instances.\n");
    }

    void hideAll() {
        for (std::size_t bucket = 0; bucket < pools_.size(); ++bucket) {
            auto& pool = pools_[bucket];
            for (std::size_t index = 0; index < pool.instances.size(); ++index) {
                hideInstance(pool.instances[index],
                             index + bucket * kInstancesPerBucket);
            }
        }
        visibleInstanceCount_ = 0;
    }

    void setInstance(XPLMInstanceRef instance,
                     const ContrailDebugRenderParcel& parcel,
                     float scaleM,
                     float rollDeg) {
        if (!instance) return;
        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(parcel.localPositionM.x);
        drawInfo.y = static_cast<float>(parcel.localPositionM.y);
        drawInfo.z = static_cast<float>(parcel.localPositionM.z);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = rollDeg;
        float data[] = {scaleM};
        XPLMInstanceSetPosition(instance, &drawInfo, data);
    }

    void hideInstance(XPLMInstanceRef instance, std::size_t ordinal) {
        if (!instance) return;
        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = -250000.0f - static_cast<float>(ordinal);
        drawInfo.y = -250000.0f;
        drawInfo.z = -250000.0f;
        float data[] = {0.001f};
        XPLMInstanceSetPosition(instance, &drawInfo, data);
    }

    static std::size_t opacityBucket(const ContrailDebugRenderParcel& parcel) {
        const float optical = parcel.opticalDepth;
        const float ageFade = std::clamp(
            1.0f - parcel.ageSeconds / 90.0f, 0.0f, 1.0f);
        const float strength = optical * (0.55f + 0.45f * ageFade);
        if (strength < 0.035f) return 0;
        if (strength < 0.075f) return 1;
        if (strength < 0.145f) return 2;
        return 3;
    }

    std::array<Pool, kOpacityBucketCount> pools_;
    std::array<LoadContext, kOpacityBucketCount> loadContexts_;
    XPLMDataRef scaleDataRef_ = nullptr;
    std::filesystem::path assetDirectory_;
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
};

}  // namespace ffatmo
