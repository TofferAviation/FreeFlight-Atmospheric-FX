#pragma once

#include "render/BillboardMath.h"
#include "render/ContrailRenderPlanner.h"

#include "XPLMCamera.h"
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
    static constexpr std::size_t kTextureVariantCount = 2;
    static constexpr std::size_t kAssetCount = kOpacityBucketCount * kTextureVariantCount;
    static constexpr std::size_t kInstancesPerAsset = 192;
    static constexpr std::size_t kVisibleCapacity = 1024;

    ContrailWorldRenderer() {
        for (std::size_t index = 0; index < loadContexts_.size(); ++index) {
            loadContexts_[index].owner = this;
            loadContexts_[index].assetIndex = index;
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
        poolCapacityDropCount_ = 0;
        maximumBillboardErrorDeg_ = 0.0;
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
        for (std::size_t bucket = 0; bucket < kOpacityBucketCount; ++bucket) {
            for (std::size_t variant = 0; variant < kTextureVariantCount; ++variant) {
                const std::size_t assetIndex = bucket * kTextureVariantCount + variant;
                const char variantName = variant == 0 ? 'a' : 'b';
                const auto objectPath = assetDirectory_ /
                    ("contrail_core_" + std::to_string(bucket) + "_" +
                     std::string(1, variantName) + ".obj");
                if (!std::filesystem::exists(objectPath)) {
                    log("Missing v4 asset: " + objectPath.string() + "\n");
                    stop();
                    return false;
                }
                XPLMLoadObjectAsync(objectPath.string().c_str(),
                                    objectLoadedCallback,
                                    &loadContexts_[assetIndex]);
            }
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

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        if (!enabled_ || !ready()) {
            if (ready()) hideAll();
            return;
        }

        XPLMCameraPosition_t cameraRaw {};
        XPLMReadCameraPosition(&cameraRaw);
        const engine::Vec3d cameraPosition {
            static_cast<double>(cameraRaw.x),
            static_cast<double>(cameraRaw.y),
            static_cast<double>(cameraRaw.z)
        };

        struct VisibleSample {
            const render::ContrailRenderSample* sample = nullptr;
            double distanceSquared = 0.0;
        };

        std::vector<VisibleSample> visible;
        visible.reserve(samples.size());
        constexpr double maximumDistanceSquared = 120000.0 * 120000.0;
        for (const auto& sample : samples) {
            if (!finiteSample(sample) || sample.opacityStrength < 0.002f ||
                sample.opacityBucket >= kOpacityBucketCount ||
                sample.textureVariant >= kTextureVariantCount) {
                continue;
            }
            const double dx = cameraPosition.x - sample.localPositionM.x;
            const double dy = cameraPosition.y - sample.localPositionM.y;
            const double dz = cameraPosition.z - sample.localPositionM.z;
            const double distanceSquared = dx * dx + dy * dy + dz * dz;
            if (!std::isfinite(distanceSquared) || distanceSquared > maximumDistanceSquared) continue;
            visible.push_back({&sample, distanceSquared});
        }

        if (visible.size() > kVisibleCapacity) {
            std::stable_sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) {
                if (a.sample->priority != b.sample->priority) {
                    return a.sample->priority > b.sample->priority;
                }
                if (a.sample->ageSeconds != b.sample->ageSeconds) {
                    return a.sample->ageSeconds < b.sample->ageSeconds;
                }
                return a.sample->renderId < b.sample->renderId;
            });
            visible.resize(kVisibleCapacity);
        }

        std::array<std::vector<VisibleSample>, kAssetCount> assigned;
        for (const auto& item : visible) {
            const std::size_t assetIndex =
                static_cast<std::size_t>(item.sample->opacityBucket) * kTextureVariantCount +
                static_cast<std::size_t>(item.sample->textureVariant);
            assigned[assetIndex].push_back(item);
        }

        visibleInstanceCount_ = 0;
        for (std::size_t assetIndex = 0; assetIndex < pools_.size(); ++assetIndex) {
            auto& pool = pools_[assetIndex];
            auto& bucketSamples = assigned[assetIndex];
            if (bucketSamples.size() > pool.instances.size()) {
                std::stable_sort(bucketSamples.begin(), bucketSamples.end(), [](const auto& a, const auto& b) {
                    if (a.sample->priority != b.sample->priority) {
                        return a.sample->priority > b.sample->priority;
                    }
                    return a.sample->renderId < b.sample->renderId;
                });
                poolCapacityDropCount_ += bucketSamples.size() - pool.instances.size();
                bucketSamples.resize(pool.instances.size());
            }

            std::stable_sort(bucketSamples.begin(), bucketSamples.end(), [](const auto& a, const auto& b) {
                if (a.distanceSquared != b.distanceSquared) {
                    return a.distanceSquared > b.distanceSquared;
                }
                return a.sample->renderId < b.sample->renderId;
            });

            std::size_t instanceIndex = 0;
            for (; instanceIndex < bucketSamples.size(); ++instanceIndex) {
                const auto& sample = *bucketSamples[instanceIndex].sample;
                const float textureRollDeg = static_cast<float>(
                    (sample.renderId * 47u + sample.engineIndex * 83u) % 360u);
                setInstance(pool.instances[instanceIndex],
                            sample,
                            textureRollDeg,
                            cameraRaw,
                            cameraPosition);
                ++visibleInstanceCount_;
            }
            for (; instanceIndex < pool.instances.size(); ++instanceIndex) {
                hideInstance(pool.instances[instanceIndex],
                             instanceIndex + assetIndex * kInstancesPerAsset);
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
    std::uint64_t poolCapacityDropCount() const { return poolCapacityDropCount_; }
    double maximumBillboardErrorDeg() const { return maximumBillboardErrorDeg_; }

private:
    struct Pool {
        XPLMObjectRef object = nullptr;
        std::vector<XPLMInstanceRef> instances;
    };

    struct LoadContext {
        ContrailWorldRenderer* owner = nullptr;
        std::size_t assetIndex = 0;
    };

    static void log(const std::string& message) {
        XPLMDebugString(("[FFAtmo Contrail World Renderer] " + message).c_str());
    }

    static float readScale(void*) {
        return 1.0f;
    }

    static bool finiteSample(const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.radiusM) && sample.radiusM > 0.0f &&
               std::isfinite(sample.opacityStrength) &&
               std::isfinite(sample.ageSeconds);
    }

    static void objectLoadedCallback(XPLMObjectRef object, void* refcon) {
        auto* context = static_cast<LoadContext*>(refcon);
        if (!context || !context->owner) {
            if (object) XPLMUnloadObject(object);
            return;
        }
        context->owner->objectLoaded(context->assetIndex, object);
    }

    void objectLoaded(std::size_t assetIndex, XPLMObjectRef object) {
        if (!object) {
            log("X-Plane could not load v4 asset " + std::to_string(assetIndex) + ".\n");
            return;
        }
        if (!running_ || assetIndex >= pools_.size()) {
            XPLMUnloadObject(object);
            return;
        }

        auto& pool = pools_[assetIndex];
        pool.object = object;
        const char* datarefs[] = {"ffatmo/contrail_debug/scale", nullptr};
        pool.instances.reserve(kInstancesPerAsset);
        for (std::size_t index = 0; index < kInstancesPerAsset; ++index) {
            XPLMInstanceRef instance = XPLMCreateInstance(object, datarefs);
            if (!instance) break;
            pool.instances.push_back(instance);
            hideInstance(instance, index + assetIndex * kInstancesPerAsset);
        }
        if (pool.instances.empty()) {
            log("No instances could be created for v4 asset " +
                std::to_string(assetIndex) + ".\n");
            return;
        }
        ++loadedObjectCount_;
        log("Loaded v4 asset " + std::to_string(assetIndex) + " with " +
            std::to_string(pool.instances.size()) + " pooled instances.\n");
    }

    void hideAll() {
        for (std::size_t assetIndex = 0; assetIndex < pools_.size(); ++assetIndex) {
            auto& pool = pools_[assetIndex];
            for (std::size_t index = 0; index < pool.instances.size(); ++index) {
                hideInstance(pool.instances[index], index + assetIndex * kInstancesPerAsset);
            }
        }
        visibleInstanceCount_ = 0;
    }

    void setInstance(XPLMInstanceRef instance,
                     const render::ContrailRenderSample& sample,
                     float textureRollDeg,
                     const XPLMCameraPosition_t& cameraRaw,
                     const engine::Vec3d& cameraPosition) {
        if (!instance) return;
        const auto angles = render::calculateBillboardAngles(sample.localPositionM, cameraPosition);
        maximumBillboardErrorDeg_ = std::max(
            maximumBillboardErrorDeg_,
            render::billboardAlignmentErrorDegrees(
                sample.localPositionM, cameraPosition, angles));

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(sample.localPositionM.x);
        drawInfo.y = static_cast<float>(sample.localPositionM.y);
        drawInfo.z = static_cast<float>(sample.localPositionM.z);
        drawInfo.pitch = angles.pitchDeg;
        drawInfo.heading = angles.headingDeg;
        drawInfo.roll = cameraRaw.roll + textureRollDeg;
        float data[] = {std::clamp(sample.radiusM, 0.25f, 12.0f)};
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

    std::array<Pool, kAssetCount> pools_;
    std::array<LoadContext, kAssetCount> loadContexts_;
    XPLMDataRef scaleDataRef_ = nullptr;
    std::filesystem::path assetDirectory_;
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    double maximumBillboardErrorDeg_ = 0.0;
    bool running_ = false;
    bool enabled_ = true;
};

}  // namespace ffatmo
