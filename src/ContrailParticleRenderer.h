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

// Renderer Foundation v5.3 pooled-billboard foundation.
//
// Ribbon mode is deliberately retired: the live Vulkan path rendered every
// ribbon black even when the same texture, tint and emissive settings previewed
// correctly in X-Plane's particle editor. v5.3 uses one ATTACH billboard per
// XPLM instance and positions those billboards directly on the deterministic
// wake-render samples. This exposes the existing wake roll-up, descent and
// spreading without allowing the particle system to invent a separate path.
//
// The particle OBJ is still not loaded during XPluginStart/XPluginEnable. The
// safe-start gate waits for three consecutive non-empty flight-loop updates.
class ContrailParticleRenderer {
public:
    static constexpr std::size_t kAssetCount = 1;
    static constexpr std::size_t kInstancesPerAsset = 768;
    static constexpr std::size_t kVisibleCapacity = kInstancesPerAsset;
    static constexpr std::size_t kDiagnosticAssetCount =
        render::kContrailRenderAssetCount;

    using DiagnosticCounts =
        std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailParticleRenderer() { stop(); }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;

        assetDirectory_ = assetDirectory;
        objectPath_ = assetDirectory_ / "contrail_billboard.obj";
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        createdInstanceCount_ = 0;
        poolCapacityDropCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        instances_.fill(nullptr);
        active_.fill(false);
        enabled_ = true;
        loadAttempted_ = false;
        deferredNonEmptyFrameCount_ = 0;
        poolReadyLogged_ = false;

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
            log("Could not register Renderer v5.3 billboard instance datarefs.\n");
            stop();
            return false;
        }

        if (!std::filesystem::exists(objectPath_)) {
            log("Missing Renderer v5.3 billboard asset: " +
                objectPath_.string() + "\n");
            stop();
            return false;
        }

        running_ = true;
        log("Renderer v5.3 armed; billboard OBJ load is deferred until live wake samples exist.\n");
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
        createdInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        loadAttempted_ = false;
        deferredNonEmptyFrameCount_ = 0;
        poolReadyLogged_ = false;
    }

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;

        if (!enabled_ || !running_) {
            hideAllInstances();
            return;
        }

        if (!object_) {
            if (samples.empty()) {
                deferredNonEmptyFrameCount_ = 0;
                return;
            }

            if (!loadAttempted_) {
                ++deferredNonEmptyFrameCount_;
                if (deferredNonEmptyFrameCount_ < 3) return;

                loadAttempted_ = true;
                object_ = XPLMLoadObject(objectPath_.string().c_str());
                if (!object_) {
                    log("Renderer v5.3 could not load the deferred billboard OBJ.\n");
                    return;
                }

                loadedObjectCount_ = 1;
                log("Renderer v5.3 billboard object loaded after the safe-start gate.\n");
            }
        }

        growInstancePool();
        if (!ready()) return;

        if (samples.empty()) {
            hideAllInstances();
            return;
        }

        const std::size_t available = availableInstanceCount();
        if (available == 0) return;

        std::array<std::vector<const render::ContrailRenderSample*>, 2> perEngine;
        for (const auto& sample : samples) {
            if (sample.engineIndex >= perEngine.size() ||
                !finiteSample(sample) ||
                sample.opacityStrength <= 0.0005f) {
                continue;
            }
            perEngine[sample.engineIndex].push_back(&sample);
        }

        for (auto& stream : perEngine) {
            std::stable_sort(
                stream.begin(),
                stream.end(),
                [](const auto* lhs, const auto* rhs) {
                    if (lhs->ageSeconds != rhs->ageSeconds) {
                        return lhs->ageSeconds < rhs->ageSeconds;
                    }
                    return lhs->renderId < rhs->renderId;
                });
        }

        std::array<std::size_t, 2> budgets {available / 2, available - available / 2};
        if (perEngine[0].empty()) budgets = {0, available};
        if (perEngine[1].empty()) budgets = {available, 0};

        std::vector<const render::ContrailRenderSample*> selected;
        selected.reserve(available);
        appendStreamSelection(perEngine[0], budgets[0], selected);
        appendStreamSelection(perEngine[1], budgets[1], selected);

        selectedPerAsset_[0] = selected.size();

        std::size_t selectedIndex = 0;
        std::size_t rendered = 0;
        for (std::size_t poolIndex = 0;
             poolIndex < createdInstanceCount_ && selectedIndex < selected.size();
             ++poolIndex) {
            if (!instances_[poolIndex]) continue;
            setBillboard(poolIndex, *selected[selectedIndex++]);
            ++rendered;
        }

        for (std::size_t poolIndex = 0;
             poolIndex < createdInstanceCount_;
             ++poolIndex) {
            if (!instances_[poolIndex]) continue;
            if (renderedIndexActive(poolIndex, rendered)) continue;
            hideInstance(poolIndex);
        }

        visibleInstanceCount_ = rendered;
        renderedPerAsset_[0] = rendered;
        if (selectedIndex < selected.size()) {
            poolCapacityDropCount_ += selected.size() - selectedIndex;
        }
    }

    void setEnabled(bool enabled) {
        if (enabled_ == enabled) return;
        enabled_ = enabled;
        if (!enabled_) hideAllInstances();
    }

    bool enabled() const { return enabled_; }

    bool ready() const {
        return running_ && object_ && availableInstanceCount() > 0;
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
    static constexpr std::size_t kInstanceCreationBatch = 64;

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
    static float readSize(void*) { return 0.30f; }
    static float readAlpha(void*) { return 0.0f; }
    static float readLifetime(void*) { return 0.0f; }

    static bool finiteSample(
        const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.trailTangentLocal.x) &&
               std::isfinite(sample.trailTangentLocal.y) &&
               std::isfinite(sample.trailTangentLocal.z) &&
               std::isfinite(sample.widthM) &&
               sample.widthM > 0.0f &&
               std::isfinite(sample.opacityStrength) &&
               sample.opacityStrength >= 0.0f &&
               std::isfinite(sample.ageSeconds);
    }

    static std::uint64_t mix64(std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    static float unitHash(std::uint64_t value) {
        return static_cast<float>(mix64(value) & 0x00ffffffULL) /
               static_cast<float>(0x01000000ULL);
    }

    std::size_t availableInstanceCount() const {
        std::size_t count = 0;
        for (std::size_t index = 0; index < createdInstanceCount_; ++index) {
            if (instances_[index]) ++count;
        }
        return count;
    }

    void growInstancePool() {
        if (!object_ || createdInstanceCount_ >= instances_.size()) return;

        const char* datarefs[] = {
            "ffatmo/contrail_particle/rate",
            "ffatmo/contrail_particle/size",
            "ffatmo/contrail_particle/alpha",
            "ffatmo/contrail_particle/lifetime",
            nullptr
        };

        const std::size_t target = std::min(
            createdInstanceCount_ + kInstanceCreationBatch,
            instances_.size());
        while (createdInstanceCount_ < target) {
            const std::size_t index = createdInstanceCount_++;
            instances_[index] = XPLMCreateInstance(object_, datarefs);
            if (!instances_[index]) {
                ++poolCapacityDropCount_;
                continue;
            }
            hideInstance(index);
        }

        if (createdInstanceCount_ == instances_.size() && !poolReadyLogged_) {
            poolReadyLogged_ = true;
            log("Renderer v5.3 pooled billboard field is fully allocated.\n");
        }
    }

    void destroyInstances() {
        for (auto& instance : instances_) {
            if (instance) XPLMDestroyInstance(instance);
            instance = nullptr;
        }
        active_.fill(false);
        createdInstanceCount_ = 0;
    }

    static void appendStreamSelection(
        const std::vector<const render::ContrailRenderSample*>& stream,
        std::size_t budget,
        std::vector<const render::ContrailRenderSample*>& output) {
        if (stream.empty() || budget == 0) return;
        if (stream.size() <= budget) {
            output.insert(output.end(), stream.begin(), stream.end());
            return;
        }

        std::vector<bool> chosen(stream.size(), false);
        std::size_t added = 0;
        if (budget == 1) {
            output.push_back(stream.front());
            return;
        }

        // A power curve retains extra resolution in the young near field while
        // still selecting the oldest parcel and preserving the full wake shape.
        for (std::size_t slot = 0; slot < budget; ++slot) {
            const double t = static_cast<double>(slot) /
                             static_cast<double>(budget - 1);
            std::size_t index = static_cast<std::size_t>(std::llround(
                std::pow(t, 1.32) * static_cast<double>(stream.size() - 1)));
            while (index + 1 < stream.size() && chosen[index]) ++index;
            if (chosen[index]) {
                while (index > 0 && chosen[index]) --index;
            }
            if (chosen[index]) continue;
            chosen[index] = true;
            output.push_back(stream[index]);
            ++added;
        }

        if (added < budget) {
            for (std::size_t index = 0;
                 index < stream.size() && added < budget;
                 ++index) {
                if (chosen[index]) continue;
                chosen[index] = true;
                output.push_back(stream[index]);
                ++added;
            }
        }
    }

    static bool renderedIndexActive(std::size_t poolIndex,
                                    std::size_t renderedCount) {
        return poolIndex < renderedCount;
    }

    void setBillboard(
        std::size_t poolIndex,
        const render::ContrailRenderSample& sample) {
        if (poolIndex >= instances_.size() || !instances_[poolIndex]) return;

        double tx = sample.trailTangentLocal.x;
        double ty = sample.trailTangentLocal.y;
        double tz = sample.trailTangentLocal.z;
        const double tangentLength = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (tangentLength > 1.0e-6) {
            tx /= tangentLength;
            ty /= tangentLength;
            tz /= tangentLength;
        } else {
            tx = 0.0;
            ty = 0.0;
            tz = -1.0;
        }

        // Build a stable plane perpendicular to the trail tangent and apply a
        // small deterministic cross-section offset. This gives the centreline
        // cloud visible depth without disconnecting it from the wake solution.
        double sx = -tz;
        double sy = 0.0;
        double sz = tx;
        double sideLength = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (sideLength < 1.0e-5) {
            sx = 1.0;
            sy = 0.0;
            sz = 0.0;
            sideLength = 1.0;
        }
        sx /= sideLength;
        sy /= sideLength;
        sz /= sideLength;

        const double vx = ty * sz - tz * sy;
        const double vy = tz * sx - tx * sz;
        const double vz = tx * sy - ty * sx;

        const float angle = unitHash(sample.renderId) * 6.28318530718f;
        const float radialHash = unitHash(sample.renderId ^ 0xa5a5a5a55a5a5a5aULL);
        const float crossSectionScale = sample.nearField ? 0.16f : 0.34f;
        const float radialOffset = std::min(sample.widthM * crossSectionScale, 2.4f) *
                                   std::sqrt(radialHash);
        const double lateral = std::cos(angle) * radialOffset;
        const double vertical = std::sin(angle) * radialOffset;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(
            sample.localPositionM.x + sx * lateral + vx * vertical);
        drawInfo.y = static_cast<float>(
            sample.localPositionM.y + sy * lateral + vy * vertical);
        drawInfo.z = static_cast<float>(
            sample.localPositionM.z + sz * lateral + vz * vertical);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = 0.0f;

        const float targetSizeM = std::clamp(
            sample.widthM * (sample.nearField ? 0.86f : 1.18f),
            0.55f,
            24.0f);
        const float normalizedSize = std::clamp(
            (targetSizeM - 0.50f) / 23.50f,
            0.0f,
            1.0f);

        const float opticalResponse = std::sqrt(std::max(sample.opacityStrength, 0.0f));
        float normalizedAlpha = std::clamp(
            0.24f + opticalResponse * 1.75f,
            0.20f,
            0.88f);
        if (sample.nearField) normalizedAlpha *= 0.82f;

        const float rotationSeed = unitHash(
            sample.renderId ^ (sample.sourceParcelId << 1U));
        const float values[] = {
            1.0f,
            normalizedSize,
            normalizedAlpha,
            rotationSeed
        };

        XPLMInstanceSetPosition(
            instances_[poolIndex],
            &drawInfo,
            values);
        active_[poolIndex] = true;
    }

    void hideInstance(std::size_t index) {
        if (index >= instances_.size() || !instances_[index]) return;
        if (!active_[index] && createdInstanceCount_ > kInstanceCreationBatch) return;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = -250000.0f - static_cast<float>(index % 128);
        drawInfo.y = -250000.0f - static_cast<float>(index / 128);
        drawInfo.z = -250000.0f;
        const float values[] = {
            0.0f,
            0.0f,
            0.0f,
            0.0f
        };
        XPLMInstanceSetPosition(instances_[index], &drawInfo, values);
        active_[index] = false;
    }

    void hideAllInstances() {
        for (std::size_t index = 0; index < createdInstanceCount_; ++index) {
            if (instances_[index] && active_[index]) hideInstance(index);
        }
        visibleInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
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
    std::size_t createdInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    std::size_t deferredNonEmptyFrameCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
    bool loadAttempted_ = false;
    bool poolReadyLogged_ = false;
};

}  // namespace ffatmo
