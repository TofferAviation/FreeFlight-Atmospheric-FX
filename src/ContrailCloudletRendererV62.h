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

// Renderer Foundation v6.2: native 3-D filled cloud field.
//
// This renderer deliberately stays on X-Plane's supported XPLMInstance path.
// There are no billboards, particles, camera-facing cards, Modern3D callbacks,
// or custom OpenGL/Vulkan hooks. Four closed irregular 3-D OBJ size classes are
// layered along the physical wake. Stable midpoint fills close longitudinal
// gaps, while a smaller deterministic companion field exposes the simulated
// counter-rotating wake without bending the primary contrail into giant loops.
class ContrailCloudletRenderer {
public:
    static constexpr std::size_t kAssetCount = 4;
    static constexpr std::size_t kInstancesPerAsset = 256;
    static constexpr std::size_t kVisibleCapacity = kAssetCount * kInstancesPerAsset;
    static constexpr std::size_t kDiagnosticAssetCount = render::kContrailRenderAssetCount;
    using DiagnosticCounts = std::array<std::size_t, kDiagnosticAssetCount>;

    ~ContrailCloudletRenderer() { stop(); }

    bool start(const std::filesystem::path& assetDirectory) {
        if (running_) return true;
        assetDirectory_ = assetDirectory;
        objectPaths_ = {
            assetDirectory_ / "contrail_cloudlet_near.obj",
            assetDirectory_ / "contrail_cloudlet_young.obj",
            assetDirectory_ / "contrail_cloudlet_mature.obj",
            assetDirectory_ / "contrail_cloudlet_old.obj"
        };
        for (const auto& path : objectPaths_) {
            if (!std::filesystem::exists(path)) {
                log("Missing v6.2 native 3-D cloudlet asset: " + path.string() + "\n");
                return false;
            }
        }

        for (auto& row : instances_) row.fill(nullptr);
        for (auto& row : active_) row.fill(false);
        for (auto& row : usedThisFrame_) row.fill(false);
        for (auto& row : slotBound_) row.fill(false);
        for (auto& row : boundCloudIds_) row.fill(0);
        objects_.fill(nullptr);
        createdInstanceCount_.fill(0);
        poolReadyLogged_.fill(false);
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        cloudIdToSlot_.clear();
        loadedObjectCount_ = 0;
        visibleInstanceCount_ = 0;
        poolCapacityDropCount_ = 0;
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumWakeTurnDeg_ = 0.0;
        maximumWakeDescentM_ = 0.0;
        swirlCandidateCount_ = 0;
        primaryCloudletCount_ = 0;
        fillCloudletCount_ = 0;
        swirlCloudletCount_ = 0;
        deferredNonEmptyFrameCount_ = 0;
        loadAttempted_ = false;
        enabled_ = true;
        running_ = true;
        log("Renderer v6.2 armed; four native 3-D cloudlet classes are deferred until live wake samples exist.\n");
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

    void update(const std::vector<render::ContrailRenderSample>& samples) {
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
        visibleInstanceCount_ = 0;
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumWakeTurnDeg_ = 0.0;
        maximumWakeDescentM_ = 0.0;
        swirlCandidateCount_ = 0;
        primaryCloudletCount_ = 0;
        fillCloudletCount_ = 0;
        swirlCloudletCount_ = 0;
        for (auto& row : usedThisFrame_) row.fill(false);

        if (!enabled_ || !running_) {
            hideAllInstances();
            return;
        }

        if (loadedObjectCount_ < kAssetCount) {
            if (samples.empty()) {
                deferredNonEmptyFrameCount_ = 0;
                return;
            }
            if (!loadAttempted_) {
                ++deferredNonEmptyFrameCount_;
                if (deferredNonEmptyFrameCount_ < 3) return;
                loadAttempted_ = true;
                for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
                    objects_[asset] = XPLMLoadObject(objectPaths_[asset].string().c_str());
                    if (!objects_[asset]) {
                        log("Renderer v6.2 could not load " + objectPaths_[asset].filename().string() + ".\n");
                        return;
                    }
                    ++loadedObjectCount_;
                }
                log("Renderer v6.2 four-class native 3-D cloud field loaded after safe-start gate.\n");
            }
        }

        growInstancePools();
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
        updateWakeDiagnostics(streams);

        CandidateBuckets candidates;
        for (std::size_t engine = 0; engine < streams.size(); ++engine) {
            buildStreamCandidates(streams[engine], static_cast<std::uint32_t>(engine), candidates);
        }

        Selection selection;
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            const std::size_t budget = availableInstanceCount(asset);
            selectAssetCandidates(candidates[asset], budget, selection[asset]);
            if (asset < selectedPerAsset_.size()) selectedPerAsset_[asset] = selection[asset].size();
        }

        retainExistingAssignments(selection);
        assignPendingNodes(selection);
        releaseUnusedSlots();

        visibleInstanceCount_ = 0;
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            std::size_t rendered = 0;
            for (std::size_t index = 0; index < createdInstanceCount_[asset]; ++index) {
                if (instances_[asset][index] && usedThisFrame_[asset][index]) ++rendered;
            }
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
    std::size_t visibleInstanceCount() const { return visibleInstanceCount_; }
    std::size_t loadedObjectCount() const { return loadedObjectCount_; }
    std::uint64_t poolCapacityDropCount() const { return poolCapacityDropCount_; }
    std::size_t ownershipReuseCount() const { return ownershipReuseCount_; }
    std::size_t ownershipNewBindingCount() const { return ownershipNewBindingCount_; }
    std::size_t ownershipReleaseCount() const { return ownershipReleaseCount_; }
    std::size_t swirlCandidateCount() const { return swirlCandidateCount_; }
    std::size_t primaryCloudletCount() const { return primaryCloudletCount_; }
    std::size_t fillCloudletCount() const { return fillCloudletCount_; }
    std::size_t swirlCloudletCount() const { return swirlCloudletCount_; }
    double maximumWakeTurnDeg() const { return maximumWakeTurnDeg_; }
    double maximumWakeDescentM() const { return maximumWakeDescentM_; }
    double maximumBillboardErrorDeg() const { return 0.0; }
    double maximumTrailAlignmentErrorDeg() const { return 0.0; }
    double minimumTrailProjectionFactor() const { return 1.0; }
    double maximumLengthCompressionRatio() const { return 0.0; }
    const DiagnosticCounts& selectedPerAsset() const { return selectedPerAsset_; }
    const DiagnosticCounts& renderedPerAsset() const { return renderedPerAsset_; }

private:
    enum class Layer : std::uint8_t { Primary = 0, Fill = 1, Swirl = 2 };

    struct CloudNode {
        std::uint64_t cloudId = 0;
        std::uint32_t engineIndex = 0;
        std::size_t assetIndex = 0;
        Layer layer = Layer::Primary;
        engine::Vec3d position {};
        engine::Vec3d tangent {0.0, 0.0, -1.0};
        float ageSeconds = 0.0f;
        float opacityStrength = 0.0f;
    };

    struct SlotRef {
        std::uint16_t asset = 0;
        std::uint16_t index = 0;
    };

    using AssetCandidates = std::array<std::vector<CloudNode>, 3>;
    using CandidateBuckets = std::array<AssetCandidates, kAssetCount>;
    using Selection = std::array<std::vector<CloudNode>, kAssetCount>;

    static constexpr std::size_t kInstanceCreationBatchPerAsset = 24;
    static constexpr double kRadiansToDegrees = 57.29577951308232;
    static constexpr double kPi = 3.14159265358979323846;

    static void log(const std::string& text) {
        XPLMDebugString(("[FFAtmo Contrail Cloudlet Renderer] " + text).c_str());
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

    static float smoothstep(float edge0, float edge1, float value) {
        if (edge0 == edge1) return value >= edge1 ? 1.0f : 0.0f;
        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static bool finiteSample(const render::ContrailRenderSample& sample) {
        return std::isfinite(sample.localPositionM.x) &&
               std::isfinite(sample.localPositionM.y) &&
               std::isfinite(sample.localPositionM.z) &&
               std::isfinite(sample.trailTangentLocal.x) &&
               std::isfinite(sample.trailTangentLocal.y) &&
               std::isfinite(sample.trailTangentLocal.z) &&
               std::isfinite(sample.opacityStrength) && sample.opacityStrength >= 0.0f &&
               std::isfinite(sample.ageSeconds);
    }

    static double distanceM(const engine::Vec3d& a, const engine::Vec3d& b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static engine::Vec3d midpoint(const engine::Vec3d& a, const engine::Vec3d& b) {
        return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
    }

    static void normalize(engine::Vec3d& v) {
        const double m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (std::isfinite(m) && m > 1.0e-7) {
            v.x /= m; v.y /= m; v.z /= m;
        } else {
            v = {0.0, 0.0, -1.0};
        }
    }

    static std::size_t assetForAge(float ageSeconds) {
        if (ageSeconds < 3.0f) return 0;
        if (ageSeconds < 10.0f) return 1;
        if (ageSeconds < 27.0f) return 2;
        return 3;
    }

    static std::uint64_t primaryId(const render::ContrailRenderSample& sample) {
        return mix64(sample.renderId ^ 0x13579bdf2468ace0ULL);
    }

    static std::uint64_t fillId(const render::ContrailRenderSample& a,
                                const render::ContrailRenderSample& b) {
        return mix64(a.renderId ^ mix64(b.renderId ^ 0xf11f11f11f11f11fULL));
    }

    static std::uint64_t swirlId(const render::ContrailRenderSample& sample) {
        return mix64(sample.renderId ^ 0x5a17a11c10d5eedULL);
    }

    void buildStreamCandidates(
        const std::vector<const render::ContrailRenderSample*>& stream,
        std::uint32_t engineIndex,
        CandidateBuckets& candidates) {
        for (std::size_t i = 0; i < stream.size(); ++i) {
            const auto& sample = *stream[i];
            const std::size_t asset = assetForAge(sample.ageSeconds);

            // Far-wake LOD: keep a physical volume field, but spend the fixed
            // instance budget on the first ~30 seconds where structure matters.
            bool keepPrimary = true;
            if (sample.ageSeconds >= 38.0f) {
                keepPrimary = (mix64(sample.renderId) % 3ULL) == 0ULL;
            } else if (sample.ageSeconds >= 30.0f) {
                keepPrimary = (mix64(sample.renderId) & 1ULL) == 0ULL;
            }
            if (keepPrimary) {
                CloudNode node;
                node.cloudId = primaryId(sample);
                node.engineIndex = engineIndex;
                node.assetIndex = asset;
                node.layer = Layer::Primary;
                node.position = sample.localPositionM;
                node.tangent = sample.trailTangentLocal;
                normalize(node.tangent);
                node.ageSeconds = sample.ageSeconds;
                node.opacityStrength = sample.opacityStrength;
                candidates[asset][static_cast<std::size_t>(Layer::Primary)].push_back(node);
            }

            // A real 3-D midpoint body closes the measured 5-10 m gaps without
            // enlarging the primary cloudlet into a repeated giant blob.
            if (i + 1 < stream.size() && sample.ageSeconds <= 31.0f) {
                const auto& next = *stream[i + 1];
                const double gap = distanceM(sample.localPositionM, next.localPositionM);
                if (std::isfinite(gap) && gap >= 3.2 && gap <= 14.0) {
                    CloudNode fill;
                    fill.cloudId = fillId(sample, next);
                    fill.engineIndex = engineIndex;
                    fill.ageSeconds = 0.5f * (sample.ageSeconds + next.ageSeconds);
                    fill.assetIndex = assetForAge(fill.ageSeconds);
                    fill.layer = Layer::Fill;
                    fill.position = midpoint(sample.localPositionM, next.localPositionM);
                    fill.tangent = {
                        sample.trailTangentLocal.x + next.trailTangentLocal.x,
                        sample.trailTangentLocal.y + next.trailTangentLocal.y,
                        sample.trailTangentLocal.z + next.trailTangentLocal.z
                    };
                    normalize(fill.tangent);
                    fill.opacityStrength = 0.5f * (sample.opacityStrength + next.opacityStrength);
                    candidates[fill.assetIndex][static_cast<std::size_t>(Layer::Fill)].push_back(fill);
                }
            }

            // A companion volume orbits inside the local wake cross-section.
            // It exposes roll-up, while the primary remains on the physical wake
            // centreline. Half-rate sampling keeps it subtle and affordable.
            if (sample.ageSeconds >= 5.0f && sample.ageSeconds <= 27.0f &&
                ((mix64(sample.renderId ^ 0x77ULL) & 1ULL) == 0ULL)) {
                ++swirlCandidateCount_;
                CloudNode swirl;
                swirl.cloudId = swirlId(sample);
                swirl.engineIndex = engineIndex;
                swirl.ageSeconds = sample.ageSeconds;
                swirl.assetIndex = assetForAge(sample.ageSeconds);
                swirl.layer = Layer::Swirl;
                swirl.tangent = sample.trailTangentLocal;
                normalize(swirl.tangent);
                swirl.opacityStrength = sample.opacityStrength * 0.72f;

                engine::Vec3d side {-swirl.tangent.z, 0.0, swirl.tangent.x};
                const double sideM = std::sqrt(side.x * side.x + side.z * side.z);
                if (sideM < 1.0e-6) side = {1.0, 0.0, 0.0};
                else { side.x /= sideM; side.z /= sideM; }
                engine::Vec3d up {
                    swirl.tangent.y * side.z - swirl.tangent.z * side.y,
                    swirl.tangent.z * side.x - swirl.tangent.x * side.z,
                    swirl.tangent.x * side.y - swirl.tangent.y * side.x
                };
                normalize(up);

                const float grow = smoothstep(5.0f, 15.0f, sample.ageSeconds);
                const float fade = 1.0f - 0.58f * smoothstep(20.0f, 27.0f, sample.ageSeconds);
                const double radius = (0.20 + 1.75 * grow) * fade;
                const double direction = engineIndex == 0 ? -1.0 : 1.0;
                const double phaseSeed = (static_cast<double>(unitHash(sample.renderId ^ 0xa55aULL)) - 0.5) * 0.45;
                const double phase = direction * static_cast<double>(sample.ageSeconds - 5.0f) * 0.42 + phaseSeed;
                swirl.position = {
                    sample.localPositionM.x + side.x * std::cos(phase) * radius + up.x * std::sin(phase) * radius,
                    sample.localPositionM.y + side.y * std::cos(phase) * radius + up.y * std::sin(phase) * radius,
                    sample.localPositionM.z + side.z * std::cos(phase) * radius + up.z * std::sin(phase) * radius
                };
                candidates[swirl.assetIndex][static_cast<std::size_t>(Layer::Swirl)].push_back(swirl);
            }
        }
    }

    static void appendEven(const std::vector<CloudNode>& source,
                           std::size_t budget,
                           std::vector<CloudNode>& output) {
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
            const double t = static_cast<double>(slot) / static_cast<double>(budget - 1);
            const std::size_t index = static_cast<std::size_t>(std::llround(
                t * static_cast<double>(source.size() - 1)));
            output.push_back(source[std::min(index, source.size() - 1)]);
        }
    }

    static void selectAssetCandidates(const AssetCandidates& source,
                                      std::size_t budget,
                                      std::vector<CloudNode>& output) {
        output.clear();
        if (budget == 0) return;
        const auto& primaries = source[static_cast<std::size_t>(Layer::Primary)];
        const auto& fills = source[static_cast<std::size_t>(Layer::Fill)];
        const auto& swirls = source[static_cast<std::size_t>(Layer::Swirl)];

        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.58);
        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.25);
        std::size_t swirlBudget = budget - primaryBudget - fillBudget;

        primaryBudget = std::min(primaryBudget, primaries.size());
        fillBudget = std::min(fillBudget, fills.size());
        swirlBudget = std::min(swirlBudget, swirls.size());
        std::size_t used = primaryBudget + fillBudget + swirlBudget;

        // Redistribute unused quota in physical importance order.
        while (used < budget) {
            bool added = false;
            if (primaryBudget < primaries.size()) { ++primaryBudget; ++used; added = true; }
            if (used >= budget) break;
            if (fillBudget < fills.size()) { ++fillBudget; ++used; added = true; }
            if (used >= budget) break;
            if (swirlBudget < swirls.size()) { ++swirlBudget; ++used; added = true; }
            if (!added) break;
        }

        output.reserve(used);
        appendEven(primaries, primaryBudget, output);
        appendEven(fills, fillBudget, output);
        appendEven(swirls, swirlBudget, output);
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
                log("Renderer v6.2 asset " + std::to_string(asset) + " reached 256 persistent 3-D instances.\n");
            }
        }
    }

    void retainExistingAssignments(const Selection& selection) {
        pendingNodes_.fill({});
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            for (const auto& node : selection[asset]) {
                const auto existing = cloudIdToSlot_.find(node.cloudId);
                if (existing == cloudIdToSlot_.end() || existing->second.asset != asset) {
                    if (existing != cloudIdToSlot_.end()) releaseOwnership(existing->second.asset, existing->second.index);
                    pendingNodes_[asset].push_back(node);
                    continue;
                }
                const std::size_t index = existing->second.index;
                if (index >= createdInstanceCount_[asset] || !instances_[asset][index] ||
                    usedThisFrame_[asset][index] || !slotBound_[asset][index] ||
                    boundCloudIds_[asset][index] != node.cloudId) {
                    cloudIdToSlot_.erase(existing);
                    pendingNodes_[asset].push_back(node);
                    continue;
                }
                usedThisFrame_[asset][index] = true;
                setCloudlet(asset, index, node);
                ++ownershipReuseCount_;
                countLayer(node.layer);
            }
        }
    }

    void assignPendingNodes(const Selection&) {
        for (std::size_t asset = 0; asset < kAssetCount; ++asset) {
            std::size_t cursor = 0;
            for (const auto& node : pendingNodes_[asset]) {
                while (cursor < createdInstanceCount_[asset] &&
                       (!instances_[asset][cursor] || usedThisFrame_[asset][cursor])) ++cursor;
                if (cursor >= createdInstanceCount_[asset]) {
                    ++poolCapacityDropCount_;
                    continue;
                }
                const std::size_t index = cursor++;
                releaseOwnership(asset, index);
                slotBound_[asset][index] = true;
                boundCloudIds_[asset][index] = node.cloudId;
                cloudIdToSlot_[node.cloudId] = {
                    static_cast<std::uint16_t>(asset),
                    static_cast<std::uint16_t>(index)
                };
                usedThisFrame_[asset][index] = true;
                setCloudlet(asset, index, node);
                ++ownershipNewBindingCount_;
                countLayer(node.layer);
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

    void countLayer(Layer layer) {
        if (layer == Layer::Primary) ++primaryCloudletCount_;
        else if (layer == Layer::Fill) ++fillCloudletCount_;
        else ++swirlCloudletCount_;
    }

    void setCloudlet(std::size_t asset, std::size_t index, const CloudNode& node) {
        if (asset >= kAssetCount || index >= kInstancesPerAsset || !instances_[asset][index]) return;
        engine::Vec3d tangent = node.tangent;
        normalize(tangent);
        const double heading = std::atan2(tangent.x, -tangent.z) * kRadiansToDegrees;
        const double pitch = std::asin(std::clamp(tangent.y, -1.0, 1.0)) * kRadiansToDegrees;
        const float roll = unitHash(node.cloudId ^ 0xd6e8feb86659fd93ULL) * 360.0f;

        XPLMDrawInfo_t draw {};
        draw.structSize = sizeof(draw);
        draw.x = static_cast<float>(node.position.x);
        draw.y = static_cast<float>(node.position.y);
        draw.z = static_cast<float>(node.position.z);
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
        cloudIdToSlot_.clear();
        for (auto& row : slotBound_) row.fill(false);
        for (auto& row : boundCloudIds_) row.fill(0);
        visibleInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
    }

    void releaseOwnership(std::size_t asset, std::size_t index) {
        if (asset >= kAssetCount || index >= kInstancesPerAsset || !slotBound_[asset][index]) return;
        const auto existing = cloudIdToSlot_.find(boundCloudIds_[asset][index]);
        if (existing != cloudIdToSlot_.end() && existing->second.asset == asset && existing->second.index == index) {
            cloudIdToSlot_.erase(existing);
        }
        slotBound_[asset][index] = false;
        boundCloudIds_[asset][index] = 0;
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
                boundCloudIds_[asset][index] = 0;
            }
            createdInstanceCount_[asset] = 0;
        }
        cloudIdToSlot_.clear();
    }

    void updateWakeDiagnostics(
        const std::array<std::vector<const render::ContrailRenderSample*>, 2>& streams) {
        for (const auto& stream : streams) {
            if (stream.empty()) continue;
            maximumWakeDescentM_ = std::max(
                maximumWakeDescentM_,
                std::max(0.0, stream.front()->localPositionM.y - stream.back()->localPositionM.y));
            for (std::size_t i = 1; i < stream.size(); ++i) {
                engine::Vec3d a = stream[i - 1]->trailTangentLocal;
                engine::Vec3d b = stream[i]->trailTangentLocal;
                normalize(a); normalize(b);
                const double cosine = std::clamp(a.x*b.x + a.y*b.y + a.z*b.z, -1.0, 1.0);
                const double angle = std::acos(cosine) * kRadiansToDegrees;
                if (std::isfinite(angle)) maximumWakeTurnDeg_ = std::max(maximumWakeTurnDeg_, angle);
            }
        }
    }

    std::filesystem::path assetDirectory_;
    std::array<std::filesystem::path, kAssetCount> objectPaths_ {};
    std::array<XPLMObjectRef, kAssetCount> objects_ {};
    std::array<std::array<XPLMInstanceRef, kInstancesPerAsset>, kAssetCount> instances_ {};
    std::array<std::array<bool, kInstancesPerAsset>, kAssetCount> active_ {};
    std::array<std::array<bool, kInstancesPerAsset>, kAssetCount> usedThisFrame_ {};
    std::array<std::array<bool, kInstancesPerAsset>, kAssetCount> slotBound_ {};
    std::array<std::array<std::uint64_t, kInstancesPerAsset>, kAssetCount> boundCloudIds_ {};
    std::array<std::size_t, kAssetCount> createdInstanceCount_ {};
    std::array<bool, kAssetCount> poolReadyLogged_ {};
    std::unordered_map<std::uint64_t, SlotRef> cloudIdToSlot_;
    std::array<std::vector<CloudNode>, kAssetCount> pendingNodes_ {};
    DiagnosticCounts selectedPerAsset_ {};
    DiagnosticCounts renderedPerAsset_ {};
    std::size_t loadedObjectCount_ = 0;
    std::size_t visibleInstanceCount_ = 0;
    std::uint64_t poolCapacityDropCount_ = 0;
    std::size_t ownershipReuseCount_ = 0;
    std::size_t ownershipNewBindingCount_ = 0;
    std::size_t ownershipReleaseCount_ = 0;
    std::size_t swirlCandidateCount_ = 0;
    std::size_t primaryCloudletCount_ = 0;
    std::size_t fillCloudletCount_ = 0;
    std::size_t swirlCloudletCount_ = 0;
    double maximumWakeTurnDeg_ = 0.0;
    double maximumWakeDescentM_ = 0.0;
    std::size_t deferredNonEmptyFrameCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
    bool loadAttempted_ = false;
};

} // namespace ffatmo
