#pragma once

#include "render/ContrailRenderPlanner.h"

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
#include <vector>

namespace ffatmo {

// Renderer Foundation v6.1: supported native 3-D cloudlet volume field.
//
// X-Plane 12.4.x no longer dispatches the legacy Modern3D OpenGL bridge in the
// photometric renderer, so the v6.0 ray-march callback never ran. v6.1 keeps
// the wake-fluid simulation but returns to the supported XPLMInstance boundary.
// The visual primitive is NOT a billboard or a particle: it is a closed,
// irregular, translucent 3-D OBJ cloudlet with real depth and parallax. Dense
// overlap of stable cloudlets forms the visible contrail volume.
class ContrailCloudletRenderer {
public:
    static constexpr std::size_t kAssetCount = 1;
    static constexpr std::size_t kInstancesPerAsset = 768;
    static constexpr std::size_t kVisibleCapacity = kInstancesPerAsset;
    static constexpr std::size_t kDiagnosticAssetCount = render::kContrailRenderAssetCount;
    using DiagnosticCounts = std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailCloudletRenderer() { stop(); }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;
        assetDirectory_ = assetDirectory;
        objectPath_ = assetDirectory_ / "contrail_cloudlet.obj";
        if (!std::filesystem::exists(objectPath_)) {
            log("Missing v6.1 native 3-D cloudlet asset: " + objectPath_.string() + "\n");
            return false;
        }
        instances_.fill(nullptr);
        active_.fill(false);
        usedThisFrame_.fill(false);
        slotBound_.fill(false);
        boundRenderIds_.fill(0);
        renderIdToPool_.clear();
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        createdInstanceCount_ = 0;
        visibleInstanceCount_ = 0;
        loadedObjectCount_ = 0;
        poolCapacityDropCount_ = 0;
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumWakeTurnDeg_ = 0.0;
        maximumWakeDescentM_ = 0.0;
        swirlCandidateCount_ = 0;
        deferredNonEmptyFrameCount_ = 0;
        loadAttempted_ = false;
        poolReadyLogged_ = false;
        enabled_ = true;
        running_ = true;
        log("Renderer v6.1 armed; native 3-D cloudlet OBJ load is deferred until live wake samples exist.\n");
        return true;
    }

    void stop() {
        running_ = false;
        destroyInstances();
        if (object_) {
            XPLMUnloadObject(object_);
            object_ = nullptr;
        }
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        loadAttempted_ = false;
        deferredNonEmptyFrameCount_ = 0;
        assetDirectory_.clear();
        objectPath_.clear();
    }

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;
        usedThisFrame_.fill(false);
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumWakeTurnDeg_ = 0.0;
        maximumWakeDescentM_ = 0.0;
        swirlCandidateCount_ = 0;

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
                    log("Renderer v6.1 could not load contrail_cloudlet.obj.\n");
                    return;
                }
                loadedObjectCount_ = 1;
                log("Renderer v6.1 native 3-D cloudlet object loaded after safe-start gate.\n");
            }
        }

        growInstancePool();
        if (!ready()) return;
        if (samples.empty()) {
            hideAllInstances();
            return;
        }

        std::array<std::vector<const render::ContrailRenderSample*>, 2> streams;
        for (const auto& sample : samples) {
            if (sample.engineIndex >= streams.size()) continue;
            if (!finiteSample(sample) || sample.opacityStrength <= 0.001f) continue;
            streams[sample.engineIndex].push_back(&sample);
        }
        for (auto& stream : streams) {
            std::stable_sort(stream.begin(), stream.end(), [](const auto* a, const auto* b) {
                if (a->ageSeconds != b->ageSeconds) return a->ageSeconds < b->ageSeconds;
                return a->renderId < b->renderId;
            });
        }

        const std::size_t available = availableInstanceCount();
        if (available == 0) return;
        std::array<std::size_t, 2> budgets {available / 2, available - available / 2};
        if (streams[0].empty()) budgets = {0, available};
        if (streams[1].empty()) budgets = {available, 0};

        std::vector<const render::ContrailRenderSample*> selected;
        selected.reserve(available);
        appendStreamSelection(streams[0], budgets[0], selected);
        appendStreamSelection(streams[1], budgets[1], selected);
        selectedPerAsset_[0] = selected.size();
        updateDiagnostics(selected);

        struct Assignment { std::size_t poolIndex; const render::ContrailRenderSample* sample; };
        std::vector<Assignment> assignments;
        std::vector<const render::ContrailRenderSample*> pending;
        assignments.reserve(selected.size());
        pending.reserve(selected.size());

        for (const auto* sample : selected) {
            const auto existing = renderIdToPool_.find(sample->renderId);
            if (existing == renderIdToPool_.end()) {
                pending.push_back(sample);
                continue;
            }
            const std::size_t index = existing->second;
            if (index >= createdInstanceCount_ || !instances_[index] || usedThisFrame_[index] ||
                !slotBound_[index] || boundRenderIds_[index] != sample->renderId) {
                renderIdToPool_.erase(existing);
                pending.push_back(sample);
                continue;
            }
            usedThisFrame_[index] = true;
            assignments.push_back({index, sample});
            ++ownershipReuseCount_;
        }

        std::size_t cursor = 0;
        for (const auto* sample : pending) {
            while (cursor < createdInstanceCount_ && (!instances_[cursor] || usedThisFrame_[cursor])) ++cursor;
            if (cursor >= createdInstanceCount_) break;
            const std::size_t index = cursor++;
            releaseOwnership(index);
            slotBound_[index] = true;
            boundRenderIds_[index] = sample->renderId;
            renderIdToPool_[sample->renderId] = index;
            usedThisFrame_[index] = true;
            assignments.push_back({index, sample});
            ++ownershipNewBindingCount_;
        }

        for (const auto& assignment : assignments) setCloudlet(assignment.poolIndex, *assignment.sample);
        for (std::size_t index = 0; index < createdInstanceCount_; ++index) {
            if (!instances_[index] || usedThisFrame_[index]) continue;
            if (active_[index]) hideInstance(index);
            releaseOwnership(index);
        }

        visibleInstanceCount_ = assignments.size();
        renderedPerAsset_[0] = assignments.size();
        if (assignments.size() < selected.size()) poolCapacityDropCount_ += selected.size() - assignments.size();
    }

    void setEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) hideAllInstances();
    }

    bool enabled() const { return enabled_; }
    bool ready() const { return running_ && object_ && availableInstanceCount() > 0; }
    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }
    std::uint64_t poolCapacityDropCount() const { return poolCapacityDropCount_; }
    std::size_t ownershipReuseCount() const { return ownershipReuseCount_; }
    std::size_t ownershipNewBindingCount() const { return ownershipNewBindingCount_; }
    std::size_t ownershipReleaseCount() const { return ownershipReleaseCount_; }
    std::size_t swirlCandidateCount() const { return swirlCandidateCount_; }
    double maximumWakeTurnDeg() const { return maximumWakeTurnDeg_; }
    double maximumWakeDescentM() const { return maximumWakeDescentM_; }
    double maximumBillboardErrorDeg() const { return 0.0; }
    double maximumTrailAlignmentErrorDeg() const { return maximumTrailAlignmentErrorDeg_; }
    double minimumTrailProjectionFactor() const { return 1.0; }
    double maximumLengthCompressionRatio() const { return 0.0; }
    const DiagnosticCounts& selectedPerAsset() const { return selectedPerAsset_; }
    const DiagnosticCounts& renderedPerAsset() const { return renderedPerAsset_; }

private:
    static constexpr std::size_t kInstanceCreationBatch = 48;
    static constexpr double kRadiansToDegrees = 57.29577951308232;

    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo Contrail Cloudlet Renderer] " + text).c_str());
    }

    static bool finiteSample(const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) && std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) && std::isfinite(sample.trailTangentLocal.x) &&
               std::isfinite(sample.trailTangentLocal.y) && std::isfinite(sample.trailTangentLocal.z) &&
               std::isfinite(sample.opacityStrength) && sample.opacityStrength >= 0.0f &&
               std::isfinite(sample.ageSeconds);
    }

    static std::uint64_t mix64(std::uint64_t value) {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    static float unitHash(std::uint64_t value) {
        return static_cast<float>(mix64(value) & 0x00ffffffULL) / static_cast<float>(0x01000000ULL);
    }

    static void normalize(double& x, double& y, double& z) {
        const double m = std::sqrt(x*x + y*y + z*z);
        if (std::isfinite(m) && m > 1.0e-7) { x /= m; y /= m; z /= m; }
        else { x = 0.0; y = 0.0; z = -1.0; }
    }

    std::size_t availableInstanceCount() const {
        std::size_t count = 0;
        for (std::size_t i = 0; i < createdInstanceCount_; ++i) if (instances_[i]) ++count;
        return count;
    }

    void growInstancePool() {
        if (!object_ || createdInstanceCount_ >= instances_.size()) return;
        const char* datarefs[] = {nullptr};
        const std::size_t target = std::min(createdInstanceCount_ + kInstanceCreationBatch, instances_.size());
        while (createdInstanceCount_ < target) {
            const std::size_t index = createdInstanceCount_++;
            instances_[index] = XPLMCreateInstance(object_, datarefs);
            if (!instances_[index]) { ++poolCapacityDropCount_; continue; }
            hideInstance(index);
        }
        if (createdInstanceCount_ == instances_.size() && !poolReadyLogged_) {
            poolReadyLogged_ = true;
            log("Renderer v6.1 768 native 3-D cloudlet instances fully allocated.\n");
        }
    }

    static void appendStreamSelection(const std::vector<const render::ContrailRenderSample*>& stream,
                                      std::size_t budget,
                                      std::vector<const render::ContrailRenderSample*>& output) {
        if (stream.empty() || budget == 0) return;
        if (stream.size() <= budget) { output.insert(output.end(), stream.begin(), stream.end()); return; }
        for (std::size_t slot = 0; slot < budget; ++slot) {
            const double t = budget > 1 ? static_cast<double>(slot) / static_cast<double>(budget - 1) : 0.0;
            const std::size_t index = static_cast<std::size_t>(std::llround(
                std::pow(t, 1.10) * static_cast<double>(stream.size() - 1)));
            output.push_back(stream[std::min(index, stream.size() - 1)]);
        }
    }

    void setCloudlet(std::size_t index, const render::ContrailRenderSample& sample) {
        if (index >= instances_.size() || !instances_[index]) return;
        double tx = sample.trailTangentLocal.x;
        double ty = sample.trailTangentLocal.y;
        double tz = sample.trailTangentLocal.z;
        normalize(tx, ty, tz);

        const double heading = std::atan2(tx, -tz) * kRadiansToDegrees;
        const double pitch = std::asin(std::clamp(ty, -1.0, 1.0)) * kRadiansToDegrees;
        const float roll = unitHash(sample.renderId ^ 0xd6e8feb86659fd93ULL) * 360.0f;

        XPLMDrawInfo_t draw {};
        draw.structSize = sizeof(draw);
        draw.x = static_cast<float>(sample.localPositionM.x);
        draw.y = static_cast<float>(sample.localPositionM.y);
        draw.z = static_cast<float>(sample.localPositionM.z);
        draw.pitch = static_cast<float>(pitch);
        draw.heading = static_cast<float>(heading);
        draw.roll = roll;
        const float noData[] = {0.0f};
        XPLMInstanceSetPosition(instances_[index], &draw, noData);
        active_[index] = true;
    }

    void updateDiagnostics(const std::vector<const render::ContrailRenderSample*>& selected) {
        std::array<std::vector<const render::ContrailRenderSample*>, 2> streams;
        for (const auto* sample : selected) {
            if (sample->engineIndex < streams.size()) streams[sample->engineIndex].push_back(sample);
            if (sample->ageSeconds >= 4.0f && sample->ageSeconds <= 45.0f) ++swirlCandidateCount_;
        }
        for (auto& stream : streams) {
            if (stream.empty()) continue;
            std::stable_sort(stream.begin(), stream.end(), [](const auto* a, const auto* b) {
                if (a->ageSeconds != b->ageSeconds) return a->ageSeconds < b->ageSeconds;
                return a->renderId < b->renderId;
            });
            maximumWakeDescentM_ = std::max(maximumWakeDescentM_,
                std::max(0.0, stream.front()->localPositionM.y - stream.back()->localPositionM.y));
            for (std::size_t i = 1; i < stream.size(); ++i) {
                double ax = stream[i-1]->trailTangentLocal.x, ay = stream[i-1]->trailTangentLocal.y, az = stream[i-1]->trailTangentLocal.z;
                double bx = stream[i]->trailTangentLocal.x, by = stream[i]->trailTangentLocal.y, bz = stream[i]->trailTangentLocal.z;
                normalize(ax, ay, az); normalize(bx, by, bz);
                const double cosine = std::clamp(ax*bx + ay*by + az*bz, -1.0, 1.0);
                maximumWakeTurnDeg_ = std::max(maximumWakeTurnDeg_, std::acos(cosine) * kRadiansToDegrees);
            }
        }
        maximumTrailAlignmentErrorDeg_ = 0.0;
    }

    void releaseOwnership(std::size_t index) {
        if (index >= slotBound_.size() || !slotBound_[index]) return;
        const auto found = renderIdToPool_.find(boundRenderIds_[index]);
        if (found != renderIdToPool_.end() && found->second == index) renderIdToPool_.erase(found);
        slotBound_[index] = false;
        boundRenderIds_[index] = 0;
        ++ownershipReleaseCount_;
    }

    void hideInstance(std::size_t index) {
        if (index >= instances_.size() || !instances_[index]) return;
        XPLMDrawInfo_t draw {};
        draw.structSize = sizeof(draw);
        draw.x = -250000.0f - static_cast<float>(index % 128);
        draw.y = -250000.0f - static_cast<float>(index / 128);
        draw.z = -250000.0f;
        draw.pitch = draw.heading = draw.roll = 0.0f;
        const float noData[] = {0.0f};
        XPLMInstanceSetPosition(instances_[index], &draw, noData);
        active_[index] = false;
    }

    void hideAllInstances() {
        for (std::size_t i = 0; i < createdInstanceCount_; ++i) if (instances_[i] && active_[i]) hideInstance(i);
        renderIdToPool_.clear();
        slotBound_.fill(false);
        boundRenderIds_.fill(0);
        visibleInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
    }

    void destroyInstances() {
        for (auto& instance : instances_) {
            if (instance) XPLMDestroyInstance(instance);
            instance = nullptr;
        }
        active_.fill(false);
        usedThisFrame_.fill(false);
        slotBound_.fill(false);
        boundRenderIds_.fill(0);
        renderIdToPool_.clear();
        createdInstanceCount_ = 0;
    }

    std::filesystem::path assetDirectory_;
    std::filesystem::path objectPath_;
    XPLMObjectRef object_ = nullptr;
    std::array<XPLMInstanceRef, kInstancesPerAsset> instances_ {};
    std::array<bool, kInstancesPerAsset> active_ {};
    std::array<bool, kInstancesPerAsset> usedThisFrame_ {};
    std::array<bool, kInstancesPerAsset> slotBound_ {};
    std::array<std::uint64_t, kInstancesPerAsset> boundRenderIds_ {};
    std::unordered_map<std::uint64_t, std::size_t> renderIdToPool_;
    DiagnosticCounts selectedPerAsset_ {};
    DiagnosticCounts renderedPerAsset_ {};
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    std::size_t createdInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    std::size_t ownershipReuseCount_ = 0;
    std::size_t ownershipNewBindingCount_ = 0;
    std::size_t ownershipReleaseCount_ = 0;
    std::size_t swirlCandidateCount_ = 0;
    double maximumWakeTurnDeg_ = 0.0;
    double maximumWakeDescentM_ = 0.0;
    double maximumTrailAlignmentErrorDeg_ = 0.0;
    std::size_t deferredNonEmptyFrameCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
    bool loadAttempted_ = false;
    bool poolReadyLogged_ = false;
};

}  // namespace ffatmo
