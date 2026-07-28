#pragma once

#include "render/VortexSheetRenderField.h"

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

// Renderer Foundation v6.9: native XPLMInstance renderer for the real
// Lagrangian vortex-sheet field. The fixed 1024-instance budget is owned by
// coherent material lanes (inner/core/outer/secondary), not by decorative
// primary/fill/swirl layers.
class ContrailVortexSheetRenderer {
public:
    static constexpr std::size_t kAssetCount = engine::kWakeSheetLaneCount;
    static constexpr std::size_t kInstancesPerAsset = 256;
    static constexpr std::size_t kVisibleCapacity = kAssetCount * kInstancesPerAsset;
    static constexpr std::size_t kDiagnosticAssetCount = 8;
    using DiagnosticCounts = std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailVortexSheetRenderer() { stop(); }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;
        assetDirectory_ = assetDirectory;
        objectPaths_ = {
            assetDirectory_ / "contrail_v69_inner.obj",
            assetDirectory_ / "contrail_v69_core.obj",
            assetDirectory_ / "contrail_v69_outer.obj",
            assetDirectory_ / "contrail_v69_secondary.obj"
        };
        for (const auto& path : objectPaths_) {
            if (!std::filesystem::exists(path)) {
                log("Missing v6.9 vortex-sheet asset: " + path.string() + "\n");
                return false;
            }
        }

        for (auto& row : instances_) row.fill(nullptr);
        for (auto& row : active_) row.fill(false);
        for (auto& row : usedThisFrame_) row.fill(false);
        for (auto& row : slotBound_) row.fill(false);
        for (auto& row : boundIds_) row.fill(0);
        objects_.fill(nullptr);
        createdInstanceCount_.fill(0);
        poolReadyLogged_.fill(false);
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        laneSelected_.fill(0);
        laneRendered_.fill(0);
        idToSlot_.clear();
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        poolCapacityDropCount_ = 0;
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumLaneGapM_ = 0.0;
        maximumLaneCurvatureDeg_ = 0.0;
        deferredNonEmptyFrameCount_ = 0;
        loadAttempted_ = false;
        enabled_ = true;
        running_ = true;
        log("Renderer v6.9 armed: four persistent Lagrangian wake-sheet lanes.\n");
        return true;
    }

    void stop() {
        running_ = false;
        destroyInstances();
        for (auto& object : objects_) {
            if (object) XPLMUnloadObject(object);
            object = nullptr;
        }
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        loadAttempted_ = false;
        deferredNonEmptyFrameCount_ = 0;
        assetDirectory_.clear();
    }

    // Empty-field compatibility path used by trail reset/aircraft reload.
    void update(const render::VortexSheetRenderField& field) {
        if (field.points.empty()) hideAllInstances();
    }

    void update(const render::VortexSheetRenderField& field,
                const engine::Vec3d& localOriginM,
                const engine::Vec3d& worldOriginM) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        laneSelected_.fill(0);
        laneRendered_.fill(0);
        visibleInstanceCount_ = 0;
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumLaneGapM_ = field.metrics.maximumLaneGapM;
        maximumLaneCurvatureDeg_ = static_cast<double>(
            field.metrics.maximumLaneCurvatureRadians) * kRadiansToDegrees;
        for (auto& row : usedThisFrame_) row.fill(false);

        if (!enabled_ || !running_) {
            hideAllInstances();
            return;
        }

        if (field.points.empty()) {
            hideAllInstances();
            return;
        }

        if (loadedObjectCount_ < kAssetCount) {
            if (!loadAttempted_) {
                ++deferredNonEmptyFrameCount_;
                if (deferredNonEmptyFrameCount_ < 3) return;
                loadAttempted_ = true;
                for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
                    objects_[asset] = XPLMLoadObject(objectPaths_[asset].string().c_str());
                    if (!objects_[asset]) {
                        log("Renderer v6.9 could not load " +
                            objectPaths_[asset].filename().string() + ".\n");
                        return;
                    }
                    ++loadedObjectCount_;
                }
                log("Renderer v6.9 elongated ice-volume assets loaded.\n");
            }
        }

        growInstancePools();
        if (!ready()) return;

        CandidateByLaneEngine candidates {};
        buildCandidates(field, localOriginM, worldOriginM, candidates);

        Selection selection {};
        for (std::size_t lane = 0; lane < kAssetCount; ++lane) {
            selectLaneCoherently(candidates[lane], kInstancesPerAsset, selection[lane]);
            laneSelected_[lane] = selection[lane].size();
            if (lane < selectedPerAsset_.size()) selectedPerAsset_[lane] = selection[lane].size();
        }

        retainExistingAssignments(selection);
        assignPendingNodes();
        releaseUnusedSlots();

        visibleInstanceCount_ = 0;
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            std::size_t rendered = 0;
            for (std::size_t index = 0; index < createdInstanceCount_[asset]; ++index) {
                if (instances_[asset][index] && usedThisFrame_[asset][index]) ++rendered;
            }
            laneRendered_[asset] = rendered;
            visibleInstanceCount_ += rendered;
            if (asset < renderedPerAsset_.size()) renderedPerAsset_[asset] = rendered;
        }
    }

    void setEnabled(bool enabled) {
        enabled_ = enabled;
        if (!enabled_) hideAllInstances();
    }

    bool enabled() const { return enabled_; }
    bool ready() const {
        if (!running_ || loadedObjectCount_ != kAssetCount) return false;
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            if (availableInstanceCount(asset) == 0) return false;
        }
        return true;
    }

    // v6.8 compatibility API. v6.9 deliberately does not multiply the physical
    // marker trajectories by a visual gain; the solver-produced sheet is rendered.
    void cycleVortexVisualGain() { vortexVisualGain_ = 1.0; }
    double vortexVisualGain() const { return 1.0; }
    double maximumPrimaryRollupOffsetM() const { return maximumSheetWidthM_; }
    std::size_t primaryRollupCloudletCount() const {
        return laneRendered_[static_cast<std::size_t>(engine::WakeSheetLane::Core)];
    }

    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }
    std::uint64_t poolCapacityDropCount() const { return poolCapacityDropCount_; }
    std::size_t ownershipReuseCount() const { return ownershipReuseCount_; }
    std::size_t ownershipNewBindingCount() const { return ownershipNewBindingCount_; }
    std::size_t ownershipReleaseCount() const { return ownershipReleaseCount_; }
    std::size_t swirlCandidateCount() const {
        return laneSelected_[static_cast<std::size_t>(engine::WakeSheetLane::Secondary)];
    }
    std::size_t primaryCloudletCount() const {
        return laneRendered_[static_cast<std::size_t>(engine::WakeSheetLane::Core)];
    }
    std::size_t fillCloudletCount() const {
        return laneRendered_[static_cast<std::size_t>(engine::WakeSheetLane::Inner)] +
               laneRendered_[static_cast<std::size_t>(engine::WakeSheetLane::Outer)];
    }
    std::size_t swirlCloudletCount() const {
        return laneRendered_[static_cast<std::size_t>(engine::WakeSheetLane::Secondary)];
    }
    double maximumWakeTurnDeg() const { return maximumLaneCurvatureDeg_; }
    double maximumWakeDescentM() const { return maximumWakeDescentM_; }
    double maximumBillboardErrorDeg() const { return 0.0; }
    double maximumTrailAlignmentErrorDeg() const { return 0.0; }
    double minimumTrailProjectionFactor() const { return 1.0; }
    double maximumLengthCompressionRatio() const { return 0.0; }
    const DiagnosticCounts& selectedPerAsset() const { return selectedPerAsset_; }
    const DiagnosticCounts& renderedPerAsset() const { return renderedPerAsset_; }

    const std::array<std::size_t, kAssetCount>& selectedPerLane() const { return laneSelected_; }
    const std::array<std::size_t, kAssetCount>& renderedPerLane() const { return laneRendered_; }
    double maximumLaneGapM() const { return maximumLaneGapM_; }
    double maximumLaneCurvatureDeg() const { return maximumLaneCurvatureDeg_; }
    void setMaximumSheetWidthM(double value) { maximumSheetWidthM_ = value; }
    void setMaximumWakeDescentM(double value) { maximumWakeDescentM_ = value; }

private:
    struct Node {
        std::uint64_t id = 0;
        std::uint32_t engineIndex = 0;
        std::size_t assetIndex = 0;
        engine::WakeSheetLane lane = engine::WakeSheetLane::Core;
        engine::Vec3d localPositionM {};
        engine::Vec3d tangentLocal {0.0, 0.0, -1.0};
        float ageSeconds = 0.0f;
        float opticalDepth = 0.0f;
    };

    struct SlotRef {
        std::uint16_t asset = 0;
        std::uint16_t index = 0;
    };

    using EngineCandidates = std::array<std::vector<Node>, 2>;
    using CandidateByLaneEngine = std::array<EngineCandidates, kAssetCount>;
    using Selection = std::array<std::vector<Node>, kAssetCount>;

    static constexpr std::size_t kInstanceCreationBatchPerAsset = 32;
    static constexpr double kRadiansToDegrees = 57.29577951308232;

    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo V6.9 Vortex Sheet Renderer] " + text).c_str());
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

    static void normalize(engine::Vec3d& value) {
        const double length = std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z);
        if (std::isfinite(length) && length > 1.0e-8) {
            value.x /= length;
            value.y /= length;
            value.z /= length;
        } else {
            value = {0.0, 0.0, -1.0};
        }
    }

    static bool finitePoint(const render::VortexSheetRenderPoint& point) {
        return std::isfinite(point.worldPositionM.x) &&
               std::isfinite(point.worldPositionM.y) &&
               std::isfinite(point.worldPositionM.z) &&
               std::isfinite(point.worldTangent.x) &&
               std::isfinite(point.worldTangent.y) &&
               std::isfinite(point.worldTangent.z) &&
               std::isfinite(point.ageSeconds) &&
               std::isfinite(point.opticalDepth);
    }

    static bool keepByAgeLod(const render::VortexSheetRenderPoint& point) {
        if (point.ageSeconds < 0.10f || point.ageSeconds > 34.0f) return false;
        if (point.ageSeconds < 18.0f) return true;
        if (point.ageSeconds < 26.0f) return (mix64(point.renderId) & 1ULL) == 0ULL;
        return (mix64(point.renderId) % 3ULL) == 0ULL;
    }

    static engine::Vec3d worldToLocalPosition(const engine::Vec3d& world,
                                               const engine::Vec3d& localOrigin,
                                               const engine::Vec3d& worldOrigin) {
        return {
            localOrigin.x + (world.x - worldOrigin.x),
            localOrigin.y + (world.y - worldOrigin.y),
            localOrigin.z - (world.z - worldOrigin.z)
        };
    }

    static engine::Vec3d worldToLocalTangent(const engine::Vec3d& worldTangent) {
        engine::Vec3d local {worldTangent.x, worldTangent.y, -worldTangent.z};
        normalize(local);
        return local;
    }

    static void appendEven(const std::vector<Node>& source,
                           std::size_t budget,
                           std::vector<Node>& output) {
        if (budget == 0 || source.empty()) return;
        if (source.size() <= budget) {
            output.insert(output.end(), source.begin(), source.end());
            return;
        }
        if (budget == 1) {
            output.push_back(source.front());
            return;
        }
        for (std::size_t slot = 0; slot < budget; ++slot) {
            const double t = static_cast<double>(slot) /
                             static_cast<double>(budget - 1);
            const std::size_t index = static_cast<std::size_t>(std::llround(
                t * static_cast<double>(source.size() - 1)));
            output.push_back(source[std::min(index, source.size() - 1)]);
        }
    }

    void buildCandidates(const render::VortexSheetRenderField& field,
                         const engine::Vec3d& localOriginM,
                         const engine::Vec3d& worldOriginM,
                         CandidateByLaneEngine& candidates) {
        for (const auto& point : field.points) {
            if (point.engineIndex >= 2 || !finitePoint(point) ||
                point.opticalDepth < 0.004f || !keepByAgeLod(point)) {
                continue;
            }
            const std::size_t lane = static_cast<std::size_t>(point.lane);
            if (lane >= kAssetCount) continue;

            Node node;
            node.id = point.renderId;
            node.engineIndex = point.engineIndex;
            node.assetIndex = lane;
            node.lane = point.lane;
            node.localPositionM = worldToLocalPosition(
                point.worldPositionM, localOriginM, worldOriginM);
            node.tangentLocal = worldToLocalTangent(point.worldTangent);
            node.ageSeconds = point.ageSeconds;
            node.opticalDepth = point.opticalDepth;
            candidates[lane][point.engineIndex].push_back(node);
        }

        for (auto& lane : candidates) {
            for (auto& engine : lane) {
                std::stable_sort(engine.begin(), engine.end(), [](const Node& a, const Node& b) {
                    if (a.ageSeconds != b.ageSeconds) return a.ageSeconds < b.ageSeconds;
                    return a.id < b.id;
                });
            }
        }
    }

    static void selectLaneCoherently(const EngineCandidates& source,
                                     std::size_t budget,
                                     std::vector<Node>& output) {
        output.clear();
        const bool haveLeft = !source[0].empty();
        const bool haveRight = !source[1].empty();
        if (!haveLeft && !haveRight) return;

        if (haveLeft && haveRight) {
            std::size_t leftBudget = budget / 2;
            std::size_t rightBudget = budget - leftBudget;
            leftBudget = std::min(leftBudget, source[0].size());
            rightBudget = std::min(rightBudget, source[1].size());
            std::size_t used = leftBudget + rightBudget;
            while (used < budget) {
                bool added = false;
                if (leftBudget < source[0].size()) {
                    ++leftBudget; ++used; added = true;
                }
                if (used >= budget) break;
                if (rightBudget < source[1].size()) {
                    ++rightBudget; ++used; added = true;
                }
                if (!added) break;
            }
            output.reserve(used);
            appendEven(source[0], leftBudget, output);
            appendEven(source[1], rightBudget, output);
        } else {
            const std::size_t engine = haveLeft ? 0u : 1u;
            const std::size_t count = std::min(budget, source[engine].size());
            output.reserve(count);
            appendEven(source[engine], count, output);
        }
    }

    std::size_t availableInstanceCount(std::size_t asset) const {
        if (asset >= kAssetCount) return 0;
        std::size_t count = 0;
        for (std::size_t index = 0; index < createdInstanceCount_[asset]; ++index) {
            if (instances_[asset][index]) ++count;
        }
        return count;
    }

    void growInstancePools() {
        const char* datarefs[] = {nullptr};
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            if (!objects_[asset] || createdInstanceCount_[asset] >= kInstancesPerAsset) continue;
            const std::size_t target = std::min(
                createdInstanceCount_[asset] + kInstanceCreationBatchPerAsset,
                kInstancesPerAsset);
            while (createdInstanceCount_[asset] < target) {
                const std::size_t index = createdInstanceCount_[asset]++;
                instances_[asset][index] = XPLMCreateInstance(objects_[asset], datarefs);
                if (!instances_[asset][index]) {
                    ++poolCapacityDropCount_;
                    continue;
                }
                hideInstance(asset, index);
            }
            if (createdInstanceCount_[asset] == kInstancesPerAsset && !poolReadyLogged_[asset]) {
                poolReadyLogged_[asset] = true;
                log("Lane " + std::to_string(asset) +
                    " reached 256 persistent instances.\n");
            }
        }
    }

    void retainExistingAssignments(const Selection& selection) {
        for (auto& pending : pendingNodes_) pending.clear();
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            for (const auto& node : selection[asset]) {
                const auto existing = idToSlot_.find(node.id);
                if (existing == idToSlot_.end() || existing->second.asset != asset) {
                    if (existing != idToSlot_.end()) {
                        releaseOwnership(existing->second.asset, existing->second.index);
                    }
                    pendingNodes_[asset].push_back(node);
                    continue;
                }
                const std::size_t index = existing->second.index;
                if (index >= createdInstanceCount_[asset] || !instances_[asset][index] ||
                    usedThisFrame_[asset][index] || !slotBound_[asset][index] ||
                    boundIds_[asset][index] != node.id) {
                    idToSlot_.erase(existing);
                    pendingNodes_[asset].push_back(node);
                    continue;
                }
                usedThisFrame_[asset][index] = true;
                setNode(asset, index, node);
                ++ownershipReuseCount_;
            }
        }
    }

    void assignPendingNodes() {
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            std::size_t cursor = 0;
            for (const auto& node : pendingNodes_[asset]) {
                while (cursor < createdInstanceCount_[asset] &&
                       (!instances_[asset][cursor] || usedThisFrame_[asset][cursor])) {
                    ++cursor;
                }
                if (cursor >= createdInstanceCount_[asset]) {
                    ++poolCapacityDropCount_;
                    continue;
                }
                const std::size_t index = cursor++;
                releaseOwnership(asset, index);
                slotBound_[asset][index] = true;
                boundIds_[asset][index] = node.id;
                idToSlot_[node.id] = {
                    static_cast<std::uint16_t>(asset),
                    static_cast<std::uint16_t>(index)
                };
                usedThisFrame_[asset][index] = true;
                setNode(asset, index, node);
                ++ownershipNewBindingCount_;
            }
        }
    }

    void releaseUnusedSlots() {
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            for (std::size_t index = 0; index < createdInstanceCount_[asset]; ++index) {
                if (!instances_[asset][index] || usedThisFrame_[asset][index]) continue;
                if (active_[asset][index]) hideInstance(asset, index);
                releaseOwnership(asset, index);
            }
        }
    }

    void setNode(std::size_t asset, std::size_t index, const Node& node) {
        if (asset >= kAssetCount || index >= kInstancesPerAsset || !instances_[asset][index]) return;
        engine::Vec3d tangent = node.tangentLocal;
        normalize(tangent);
        const double heading = std::atan2(tangent.x, -tangent.z) * kRadiansToDegrees;
        const double pitch = std::asin(std::clamp(tangent.y, -1.0, 1.0)) * kRadiansToDegrees;
        // Only a small deterministic axial roll. The geometry direction itself is
        // entirely supplied by the already-curled lane tangent.
        const float roll = (unitHash(node.id ^ 0xd6e8feb86659fd93ULL) - 0.5f) * 28.0f;

        XPLMDrawInfo_t draw {};
        draw.structSize = sizeof(draw);
        draw.x = static_cast<float>(node.localPositionM.x);
        draw.y = static_cast<float>(node.localPositionM.y);
        draw.z = static_cast<float>(node.localPositionM.z);
        draw.pitch = static_cast<float>(-pitch);
        draw.heading = static_cast<float>(heading);
        draw.roll = roll;
        XPLMInstanceSetPosition(instances_[asset][index], &draw, nullptr);
        active_[asset][index] = true;
    }

    void hideInstance(std::size_t asset, std::size_t index) {
        if (asset >= kAssetCount || index >= kInstancesPerAsset || !instances_[asset][index]) return;
        XPLMDrawInfo_t draw {};
        draw.structSize = sizeof(draw);
        draw.x = -500000.0f - static_cast<float>(asset * 1024 + index);
        draw.y = -500000.0f;
        draw.z = -500000.0f;
        XPLMInstanceSetPosition(instances_[asset][index], &draw, nullptr);
        active_[asset][index] = false;
    }

    void hideAllInstances() {
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            for (std::size_t index = 0; index < createdInstanceCount_[asset]; ++index) {
                if (instances_[asset][index] && active_[asset][index]) hideInstance(asset, index);
            }
        }
        idToSlot_.clear();
        for (auto& row : slotBound_) row.fill(false);
        for (auto& row : boundIds_) row.fill(0);
        visibleInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        laneSelected_.fill(0);
        laneRendered_.fill(0);
    }

    void releaseOwnership(std::size_t asset, std::size_t index) {
        if (asset >= kAssetCount || index >= kInstancesPerAsset || !slotBound_[asset][index]) return;
        const auto existing = idToSlot_.find(boundIds_[asset][index]);
        if (existing != idToSlot_.end() &&
            existing->second.asset == asset && existing->second.index == index) {
            idToSlot_.erase(existing);
        }
        slotBound_[asset][index] = false;
        boundIds_[asset][index] = 0;
        ++ownershipReleaseCount_;
    }

    void destroyInstances() {
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            for (std::size_t index = 0; index < kInstancesPerAsset; ++index) {
                if (instances_[asset][index]) XPLMDestroyInstance(instances_[asset][index]);
                instances_[asset][index] = nullptr;
                active_[asset][index] = false;
                usedThisFrame_[asset][index] = false;
                slotBound_[asset][index] = false;
                boundIds_[asset][index] = 0;
            }
            createdInstanceCount_[asset] = 0;
        }
        idToSlot_.clear();
    }

    std::filesystem::path assetDirectory_;
    std::array<std::filesystem::path, kAssetCount> objectPaths_ {};
    std::array<XPLMObjectRef, kAssetCount> objects_ {};
    std::array<std::array<XPLMInstanceRef, kInstancesPerAsset>, kAssetCount> instances_ {};
    std::array<std::array<bool, kInstancesPerAsset>, kAssetCount> active_ {};
    std::array<std::array<bool, kInstancesPerAsset>, kAssetCount> usedThisFrame_ {};
    std::array<std::array<bool, kInstancesPerAsset>, kAssetCount> slotBound_ {};
    std::array<std::array<std::uint64_t, kInstancesPerAsset>, kAssetCount> boundIds_ {};
    std::array<std::size_t, kAssetCount> createdInstanceCount_ {};
    std::array<bool, kAssetCount> poolReadyLogged_ {};
    std::unordered_map<std::uint64_t, SlotRef> idToSlot_;
    std::array<std::vector<Node>, kAssetCount> pendingNodes_ {};
    DiagnosticCounts selectedPerAsset_ {};
    DiagnosticCounts renderedPerAsset_ {};
    std::array<std::size_t, kAssetCount> laneSelected_ {};
    std::array<std::size_t, kAssetCount> laneRendered_ {};
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    std::size_t ownershipReuseCount_ = 0;
    std::size_t ownershipNewBindingCount_ = 0;
    std::size_t ownershipReleaseCount_ = 0;
    double maximumLaneGapM_ = 0.0;
    double maximumLaneCurvatureDeg_ = 0.0;
    double maximumSheetWidthM_ = 0.0;
    double maximumWakeDescentM_ = 0.0;
    double vortexVisualGain_ = 1.0;
    std::size_t deferredNonEmptyFrameCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
    bool loadAttempted_ = false;
};

}  // namespace ffatmo
