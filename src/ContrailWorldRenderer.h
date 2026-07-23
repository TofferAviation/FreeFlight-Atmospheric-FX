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
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ffatmo {

class ContrailWorldRenderer {
public:
    static constexpr std::size_t kOpacityBucketCount = render::kContrailOpacityBucketCount;
    static constexpr std::size_t kTextureVariantCount = render::kContrailTextureVariantCount;
    static constexpr std::size_t kAssetCount = render::kContrailRenderAssetCount;
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
        maximumTrailAlignmentErrorDeg_ = 0.0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        enabled_ = true;

        widthDataRef_ = registerScaleDataRef(
            "ffatmo/contrail_debug/width", readWidth);
        lengthDataRef_ = registerScaleDataRef(
            "ffatmo/contrail_debug/length", readLength);
        if (!widthDataRef_ || !lengthDataRef_) {
            log("Could not register the v4.1 width/length instance datarefs.\n");
            stop();
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
                    log("Missing v4.1 asset: " + objectPath.string() + "\n");
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
            for (auto& slot : pool.slots) {
                if (slot.instance) XPLMDestroyInstance(slot.instance);
            }
            pool.slots.clear();
            pool.slotByRenderId.clear();
            if (pool.object) {
                XPLMUnloadObject(pool.object);
                pool.object = nullptr;
            }
        }
        if (widthDataRef_) {
            XPLMUnregisterDataAccessor(widthDataRef_);
            widthDataRef_ = nullptr;
        }
        if (lengthDataRef_) {
            XPLMUnregisterDataAccessor(lengthDataRef_);
            lengthDataRef_ = nullptr;
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

        std::array<std::vector<VisibleSample>, kAssetCount> assigned;
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
            const std::size_t assetIndex =
                static_cast<std::size_t>(sample.opacityBucket) * kTextureVariantCount +
                static_cast<std::size_t>(sample.textureVariant);
            assigned[assetIndex].push_back({&sample, distanceSquared});
        }

        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;
        for (std::size_t assetIndex = 0; assetIndex < pools_.size(); ++assetIndex) {
            auto& pool = pools_[assetIndex];
            auto& requested = assigned[assetIndex];
            selectedPerAsset_[assetIndex] = requested.size();
            if (requested.size() > pool.slots.size()) {
                poolCapacityDropCount_ += requested.size() - pool.slots.size();
                std::stable_sort(requested.begin(), requested.end(), [](const auto& a, const auto& b) {
                    if (a.sample->priority != b.sample->priority) {
                        return a.sample->priority > b.sample->priority;
                    }
                    return a.sample->renderId < b.sample->renderId;
                });
                requested.resize(pool.slots.size());
            }

            std::unordered_set<std::uint64_t> wantedIds;
            wantedIds.reserve(requested.size() * 2 + 1);
            for (const auto& item : requested) wantedIds.insert(item.sample->renderId);

            for (std::size_t slotIndex = 0; slotIndex < pool.slots.size(); ++slotIndex) {
                auto& slot = pool.slots[slotIndex];
                if (!slot.occupied || wantedIds.find(slot.renderId) != wantedIds.end()) continue;
                pool.slotByRenderId.erase(slot.renderId);
                slot.renderId = 0;
                slot.occupied = false;
                hideInstance(slot.instance, slotIndex + assetIndex * kInstancesPerAsset);
            }

            std::stable_sort(requested.begin(), requested.end(), [](const auto& a, const auto& b) {
                return a.sample->renderId < b.sample->renderId;
            });

            for (const auto& item : requested) {
                const auto existing = pool.slotByRenderId.find(item.sample->renderId);
                std::size_t slotIndex = pool.slots.size();
                if (existing != pool.slotByRenderId.end()) {
                    slotIndex = existing->second;
                } else {
                    for (std::size_t candidate = 0; candidate < pool.slots.size(); ++candidate) {
                        if (!pool.slots[candidate].occupied) {
                            slotIndex = candidate;
                            break;
                        }
                    }
                    if (slotIndex == pool.slots.size()) {
                        ++poolCapacityDropCount_;
                        continue;
                    }
                    auto& slot = pool.slots[slotIndex];
                    slot.renderId = item.sample->renderId;
                    slot.occupied = true;
                    pool.slotByRenderId[item.sample->renderId] = slotIndex;
                }

                setInstance(pool.slots[slotIndex].instance, *item.sample, cameraPosition);
                ++visibleInstanceCount_;
                ++renderedPerAsset_[assetIndex];
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
            if (!pool.object || pool.slots.empty()) return false;
        }
        return true;
    }

    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }
    std::uint64_t poolCapacityDropCount() const { return poolCapacityDropCount_; }
    double maximumBillboardErrorDeg() const { return maximumBillboardErrorDeg_; }
    double maximumTrailAlignmentErrorDeg() const { return maximumTrailAlignmentErrorDeg_; }
    const std::array<std::size_t, kAssetCount>& selectedPerAsset() const {
        return selectedPerAsset_;
    }
    const std::array<std::size_t, kAssetCount>& renderedPerAsset() const {
        return renderedPerAsset_;
    }

private:
    struct InstanceSlot {
        XPLMInstanceRef instance = nullptr;
        std::uint64_t renderId = 0;
        bool occupied = false;
    };

    struct Pool {
        XPLMObjectRef object = nullptr;
        std::vector<InstanceSlot> slots;
        std::unordered_map<std::uint64_t, std::size_t> slotByRenderId;
    };

    struct LoadContext {
        ContrailWorldRenderer* owner = nullptr;
        std::size_t assetIndex = 0;
    };

    using FloatReadCallback = float (*)(void*);

    static void log(const std::string& message) {
        XPLMDebugString(("[FFAtmo Contrail World Renderer] " + message).c_str());
    }

    XPLMDataRef registerScaleDataRef(const char* name, FloatReadCallback callback) {
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

    static float readWidth(void*) { return 1.0f; }
    static float readLength(void*) { return 1.0f; }

    static bool finiteSample(const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.trailTangentLocal.x) &&
               std::isfinite(sample.trailTangentLocal.y) &&
               std::isfinite(sample.trailTangentLocal.z) &&
               std::isfinite(sample.widthM) && sample.widthM > 0.0f &&
               std::isfinite(sample.lengthM) && sample.lengthM > 0.0f &&
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
            log("X-Plane could not load v4.1 asset " + std::to_string(assetIndex) + ".\n");
            return;
        }
        if (!running_ || assetIndex >= pools_.size()) {
            XPLMUnloadObject(object);
            return;
        }

        auto& pool = pools_[assetIndex];
        pool.object = object;
        const char* datarefs[] = {
            "ffatmo/contrail_debug/width",
            "ffatmo/contrail_debug/length",
            nullptr
        };
        pool.slots.reserve(kInstancesPerAsset);
        for (std::size_t index = 0; index < kInstancesPerAsset; ++index) {
            XPLMInstanceRef instance = XPLMCreateInstance(object, datarefs);
            if (!instance) break;
            InstanceSlot slot;
            slot.instance = instance;
            pool.slots.push_back(slot);
            hideInstance(instance, index + assetIndex * kInstancesPerAsset);
        }
        if (pool.slots.empty()) {
            log("No instances could be created for v4.1 asset " +
                std::to_string(assetIndex) + ".\n");
            return;
        }
        ++loadedObjectCount_;
        log("Loaded v4.1 asset " + std::to_string(assetIndex) + " with " +
            std::to_string(pool.slots.size()) + " persistent slots.\n");
    }

    void hideAll() {
        for (std::size_t assetIndex = 0; assetIndex < pools_.size(); ++assetIndex) {
            auto& pool = pools_[assetIndex];
            for (std::size_t index = 0; index < pool.slots.size(); ++index) {
                auto& slot = pool.slots[index];
                hideInstance(slot.instance, index + assetIndex * kInstancesPerAsset);
                slot.renderId = 0;
                slot.occupied = false;
            }
            pool.slotByRenderId.clear();
        }
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;
    }

    void setInstance(XPLMInstanceRef instance,
                     const render::ContrailRenderSample& sample,
                     const engine::Vec3d& cameraPosition) {
        if (!instance) return;
        const auto angles = render::calculateTrailBillboardAngles(
            sample.localPositionM, cameraPosition, sample.trailTangentLocal);
        maximumBillboardErrorDeg_ = std::max(
            maximumBillboardErrorDeg_,
            render::billboardAlignmentErrorDegrees(
                sample.localPositionM, cameraPosition, angles));
        maximumTrailAlignmentErrorDeg_ = std::max(
            maximumTrailAlignmentErrorDeg_,
            render::trailAlignmentErrorDegrees(
                sample.localPositionM,
                cameraPosition,
                sample.trailTangentLocal,
                angles));

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(sample.localPositionM.x);
        drawInfo.y = static_cast<float>(sample.localPositionM.y);
        drawInfo.z = static_cast<float>(sample.localPositionM.z);
        drawInfo.pitch = angles.pitchDeg;
        drawInfo.heading = angles.headingDeg;
        drawInfo.roll = angles.rollDeg;
        float data[] = {
            std::clamp(sample.widthM, 0.50f, 24.0f),
            std::clamp(sample.lengthM, 1.0f, 32.0f)
        };
        XPLMInstanceSetPosition(instance, &drawInfo, data);
    }

    void hideInstance(XPLMInstanceRef instance, std::size_t ordinal) {
        if (!instance) return;
        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = -250000.0f - static_cast<float>(ordinal);
        drawInfo.y = -250000.0f;
        drawInfo.z = -250000.0f;
        float data[] = {0.001f, 0.001f};
        XPLMInstanceSetPosition(instance, &drawInfo, data);
    }

    std::array<Pool, kAssetCount> pools_;
    std::array<LoadContext, kAssetCount> loadContexts_;
    XPLMDataRef widthDataRef_ = nullptr;
    XPLMDataRef lengthDataRef_ = nullptr;
    std::filesystem::path assetDirectory_;
    std::array<std::size_t, kAssetCount> selectedPerAsset_ {};
    std::array<std::size_t, kAssetCount> renderedPerAsset_ {};
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    double maximumBillboardErrorDeg_ = 0.0;
    double maximumTrailAlignmentErrorDeg_ = 0.0;
    bool running_ = false;
    bool enabled_ = true;
};

}  // namespace ffatmo
