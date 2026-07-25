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
#include <unordered_map>
#include <utility>
#include <vector>

namespace ffatmo {

// Renderer Foundation v5.4.1 stabilised billboard cloud field.
//
// v5.4 proved that one oversized billboard per selected section creates opaque
// cotton blobs and magnifies every wake bend. v5.4.1 keeps the proven attached
// billboard path but gives each render ID persistent ownership of one XPLM
// instance, restores physically scaled low-opacity puffs, and only records
// wake-turn/descent diagnostics for the later controlled swirl pass.
//
// The particle OBJ is never loaded during XPluginStart/XPluginEnable. The
// safe-start gate waits for three consecutive non-empty flight-loop updates.
class ContrailParticleRenderer {
public:
    static constexpr std::size_t kAssetCount = 1;
    static constexpr std::size_t kInstancesPerAsset = 1024;
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
        usedThisFrame_.fill(false);
        slotBound_.fill(false);
        boundRenderIds_.fill(0);
        renderIdToPool_.clear();
        ownershipReuseCount_ = 0;
        ownershipNewBindingCount_ = 0;
        ownershipReleaseCount_ = 0;
        maximumWakeTurnDeg_ = 0.0;
        maximumWakeDescentM_ = 0.0;
        swirlCandidateCount_ = 0;
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
            log("Could not register Renderer v5.4.1 billboard instance datarefs.\n");
            stop();
            return false;
        }

        if (!std::filesystem::exists(objectPath_)) {
            log("Missing Renderer v5.4.1 billboard asset: " +
                objectPath_.string() + "\n");
            stop();
            return false;
        }

        running_ = true;
        log("Renderer v5.4.1 armed; stable billboard OBJ load is deferred until live wake samples exist.\n");
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
                    log("Renderer v5.4.1 could not load the deferred billboard OBJ.\n");
                    return;
                }

                loadedObjectCount_ = 1;
                log("Renderer v5.4.1 billboard object loaded after the safe-start gate.\n");
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

        std::array<std::size_t, 2> budgets {
            available / 2,
            available - available / 2
        };
        if (perEngine[0].empty()) budgets = {0, available};
        if (perEngine[1].empty()) budgets = {available, 0};

        std::vector<const render::ContrailRenderSample*> selected;
        selected.reserve(available);
        appendStreamSelection(perEngine[0], budgets[0], selected);
        appendStreamSelection(perEngine[1], budgets[1], selected);
        selectedPerAsset_[0] = selected.size();
        updateSwirlDiagnostics(selected);

        struct Assignment {
            std::size_t poolIndex = 0;
            const render::ContrailRenderSample* sample = nullptr;
        };
        std::vector<Assignment> assignments;
        assignments.reserve(selected.size());
        std::vector<const render::ContrailRenderSample*> pending;
        pending.reserve(selected.size());

        // First retain every render ID that was visible in the previous frame.
        // This prevents cloud puffs from being reassigned by vector position.
        for (const auto* sample : selected) {
            const auto existing = renderIdToPool_.find(sample->renderId);
            if (existing == renderIdToPool_.end()) {
                pending.push_back(sample);
                continue;
            }

            const std::size_t poolIndex = existing->second;
            if (poolIndex >= createdInstanceCount_ ||
                !instances_[poolIndex] ||
                usedThisFrame_[poolIndex] ||
                !slotBound_[poolIndex] ||
                boundRenderIds_[poolIndex] != sample->renderId) {
                renderIdToPool_.erase(existing);
                pending.push_back(sample);
                continue;
            }

            usedThisFrame_[poolIndex] = true;
            assignments.push_back({poolIndex, sample});
            ++ownershipReuseCount_;
        }

        std::size_t searchCursor = 0;
        for (const auto* sample : pending) {
            while (searchCursor < createdInstanceCount_ &&
                   (!instances_[searchCursor] || usedThisFrame_[searchCursor])) {
                ++searchCursor;
            }
            if (searchCursor >= createdInstanceCount_) break;

            const std::size_t poolIndex = searchCursor++;
            releaseOwnership(poolIndex);
            slotBound_[poolIndex] = true;
            boundRenderIds_[poolIndex] = sample->renderId;
            renderIdToPool_[sample->renderId] = poolIndex;
            usedThisFrame_[poolIndex] = true;
            assignments.push_back({poolIndex, sample});
            ++ownershipNewBindingCount_;
        }

        for (const auto& assignment : assignments) {
            setBillboard(assignment.poolIndex, *assignment.sample);
        }

        for (std::size_t poolIndex = 0;
             poolIndex < createdInstanceCount_;
             ++poolIndex) {
            if (!instances_[poolIndex] || usedThisFrame_[poolIndex]) continue;
            if (active_[poolIndex]) hideInstance(poolIndex);
            releaseOwnership(poolIndex);
        }

        visibleInstanceCount_ = assignments.size();
        renderedPerAsset_[0] = assignments.size();
        if (assignments.size() < selected.size()) {
            poolCapacityDropCount_ += selected.size() - assignments.size();
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

    std::size_t ownershipReuseCount() const {
        return ownershipReuseCount_;
    }

    std::size_t ownershipNewBindingCount() const {
        return ownershipNewBindingCount_;
    }

    std::size_t ownershipReleaseCount() const {
        return ownershipReleaseCount_;
    }

    double maximumWakeTurnDeg() const {
        return maximumWakeTurnDeg_;
    }

    double maximumWakeDescentM() const {
        return maximumWakeDescentM_;
    }

    std::size_t swirlCandidateCount() const {
        return swirlCandidateCount_;
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
    static constexpr std::size_t kInstanceCreationBatch = 48;
    static constexpr float kTwoPi = 6.28318530718f;
    static constexpr double kRadiansToDegrees = 57.29577951308232;

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

    static float smoothstep(float edge0, float edge1, float value) {
        if (edge0 == edge1) return value >= edge1 ? 1.0f : 0.0f;
        const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

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
               std::isfinite(sample.lengthM) &&
               sample.lengthM >= 0.0f &&
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

    static void normalizeTangent(double& x, double& y, double& z) {
        const double magnitude = std::sqrt(x * x + y * y + z * z);
        if (magnitude > 1.0e-6 && std::isfinite(magnitude)) {
            x /= magnitude;
            y /= magnitude;
            z /= magnitude;
        } else {
            x = 0.0;
            y = 0.0;
            z = -1.0;
        }
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
            log("Renderer v5.4.1 1024-cloud stable billboard field is fully allocated.\n");
        }
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

    void releaseOwnership(std::size_t poolIndex) {
        if (poolIndex >= slotBound_.size() || !slotBound_[poolIndex]) return;
        const auto existing = renderIdToPool_.find(boundRenderIds_[poolIndex]);
        if (existing != renderIdToPool_.end() && existing->second == poolIndex) {
            renderIdToPool_.erase(existing);
        }
        slotBound_[poolIndex] = false;
        boundRenderIds_[poolIndex] = 0;
        ++ownershipReleaseCount_;
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

        // Preserve the condensation head and far wake while maintaining a
        // smoother age distribution than v5.4's oversized continuity puffs.
        for (std::size_t slot = 0; slot < budget; ++slot) {
            const double t = static_cast<double>(slot) /
                             static_cast<double>(budget - 1);
            std::size_t index = static_cast<std::size_t>(std::llround(
                std::pow(t, 1.12) * static_cast<double>(stream.size() - 1)));
            while (index + 1 < stream.size() && chosen[index]) ++index;
            if (chosen[index]) {
                while (index > 0 && chosen[index]) --index;
            }
            if (chosen[index]) continue;
            chosen[index] = true;
            output.push_back(stream[index]);
            ++added;
        }

        for (std::size_t index = 0;
             index < stream.size() && added < budget;
             ++index) {
            if (chosen[index]) continue;
            chosen[index] = true;
            output.push_back(stream[index]);
            ++added;
        }
    }

    void updateSwirlDiagnostics(
        const std::vector<const render::ContrailRenderSample*>& selected) {
        std::array<std::vector<const render::ContrailRenderSample*>, 2> streams;
        for (const auto* sample : selected) {
            if (sample->engineIndex < streams.size()) {
                streams[sample->engineIndex].push_back(sample);
            }
            if (sample->ageSeconds >= 4.0f && sample->ageSeconds <= 45.0f) {
                ++swirlCandidateCount_;
            }
        }

        for (auto& stream : streams) {
            if (stream.empty()) continue;
            std::stable_sort(stream.begin(), stream.end(), [](const auto* lhs, const auto* rhs) {
                if (lhs->ageSeconds != rhs->ageSeconds) {
                    return lhs->ageSeconds < rhs->ageSeconds;
                }
                return lhs->renderId < rhs->renderId;
            });

            maximumWakeDescentM_ = std::max(
                maximumWakeDescentM_,
                std::max(0.0,
                    stream.front()->localPositionM.y -
                    stream.back()->localPositionM.y));

            for (std::size_t index = 1; index < stream.size(); ++index) {
                double ax = stream[index - 1]->trailTangentLocal.x;
                double ay = stream[index - 1]->trailTangentLocal.y;
                double az = stream[index - 1]->trailTangentLocal.z;
                double bx = stream[index]->trailTangentLocal.x;
                double by = stream[index]->trailTangentLocal.y;
                double bz = stream[index]->trailTangentLocal.z;
                normalizeTangent(ax, ay, az);
                normalizeTangent(bx, by, bz);
                const double cosine = std::clamp(
                    ax * bx + ay * by + az * bz, -1.0, 1.0);
                const double angleDeg = std::acos(cosine) * kRadiansToDegrees;
                if (std::isfinite(angleDeg)) {
                    maximumWakeTurnDeg_ = std::max(maximumWakeTurnDeg_, angleDeg);
                }
            }
        }
    }

    void setBillboard(
        std::size_t poolIndex,
        const render::ContrailRenderSample& sample) {
        if (poolIndex >= instances_.size() || !instances_[poolIndex]) return;

        double tx = sample.trailTangentLocal.x;
        double ty = sample.trailTangentLocal.y;
        double tz = sample.trailTangentLocal.z;
        normalizeTangent(tx, ty, tz);

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

        const std::uint64_t seed = sample.renderId ^
            (sample.sourceParcelId * 0x9e3779b97f4a7c15ULL);
        const float angle = unitHash(seed) * kTwoPi;
        const float radialHash = unitHash(seed ^ 0xa5a5a5a55a5a5a5aULL);
        const float axialHash = unitHash(seed ^ 0x94d049bb133111ebULL);
        const float sizeHash = unitHash(seed ^ 0xbf58476d1ce4e5b9ULL);
        const float densityHash = unitHash(seed ^ 0x632be59bd9b4e019ULL);

        const float ageSpread = smoothstep(2.0f, 30.0f, sample.ageSeconds);
        const float crossSectionScale = sample.nearField
            ? 0.012f
            : 0.040f + 0.055f * ageSpread;
        const float radialOffset = std::min(
            sample.widthM * crossSectionScale *
                std::pow(std::max(radialHash, 0.0001f), 1.85f),
            1.80f);
        const double lateral = std::cos(angle) * radialOffset;
        const double vertical = std::sin(angle) * radialOffset;

        const float maximumAxialJitter = std::min(
            std::max(sample.lengthM, 0.25f) *
                (sample.nearField ? 0.025f : 0.075f),
            sample.nearField ? 0.08f : 0.45f);
        const double axial = (static_cast<double>(axialHash) - 0.5) *
                             2.0 * maximumAxialJitter;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(
            sample.localPositionM.x + tx * axial + sx * lateral + vx * vertical);
        drawInfo.y = static_cast<float>(
            sample.localPositionM.y + ty * axial + sy * lateral + vy * vertical);
        drawInfo.z = static_cast<float>(
            sample.localPositionM.z + tz * axial + sz * lateral + vz * vertical);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = 0.0f;

        // Size follows physical wake width only. Section length is deliberately
        // excluded so sparse planner samples cannot become giant opaque blobs.
        const float physicalScale = sample.nearField
            ? 0.58f
            : 0.72f + 0.18f * ageSpread;
        const float sizeVariation = 0.84f + 0.24f * sizeHash;
        const float targetSizeM = std::clamp(
            sample.widthM * physicalScale * sizeVariation,
            0.35f,
            10.0f);
        const float normalizedSize = std::clamp(
            (targetSizeM - 0.30f) / 11.70f,
            0.0f,
            1.0f);

        const float opticalResponse = std::sqrt(
            std::max(sample.opacityStrength, 0.0f));
        const float densityVariation = 0.72f + 0.30f * densityHash;
        const float formationScale = sample.nearField ? 0.62f : 1.0f;
        const float oldAgeFade = 1.0f - 0.68f *
            smoothstep(30.0f, 62.0f, sample.ageSeconds);
        const float normalizedAlpha = std::clamp(
            (0.018f + opticalResponse * 0.48f) *
                densityVariation * formationScale * oldAgeFade,
            0.012f,
            0.30f);

        const float rotationSeed = unitHash(
            seed ^ 0xd6e8feb86659fd93ULL);
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
        renderIdToPool_.clear();
        slotBound_.fill(false);
        boundRenderIds_.fill(0);
        visibleInstanceCount_ = 0;
        selectedPerAsset_.fill(0);
        renderedPerAsset_.fill(0);
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
    std::size_t ownershipReuseCount_ = 0;
    std::size_t ownershipNewBindingCount_ = 0;
    std::size_t ownershipReleaseCount_ = 0;
    double maximumWakeTurnDeg_ = 0.0;
    double maximumWakeDescentM_ = 0.0;
    std::size_t swirlCandidateCount_ = 0;
    std::size_t deferredNonEmptyFrameCount_ = 0;
    bool running_ = false;
    bool enabled_ = true;
    bool loadAttempted_ = false;
    bool poolReadyLogged_ = false;
};

}  // namespace ffatmo
