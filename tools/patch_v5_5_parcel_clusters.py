#!/usr/bin/env python3
"""Patch the v5 billboard renderer into a parcel-anchored fluffy swirl field."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailParticleRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

renderer = RENDERER.read_text(encoding="utf-8")
renderer = renderer.replace("v5.4.1", "v5.5")
renderer = renderer.replace("V5.4.1", "V5.5")

private_marker = """private:
    using FloatReadCallback = float (*)(void*);
    static constexpr std::size_t kInstanceCreationBatch = 48;
    static constexpr float kTwoPi = 6.28318530718f;
    static constexpr double kRadiansToDegrees = 57.29577951308232;
"""
private_replacement = private_marker + """

    struct CloudNode {
        std::uint64_t cloudId = 0;
        std::uint64_t parcelId = 0;
        std::uint32_t engineIndex = 0;
        engine::Vec3d position {};
        engine::Vec3d tangent {0.0, 0.0, -1.0};
        float widthM = 0.4f;
        float opacityStrength = 0.0f;
        float ageSeconds = 0.0f;
        float sizeM = 0.4f;
        float alpha = 0.01f;
        bool nearField = false;
        bool companion = false;
    };
"""
if "struct CloudNode" not in renderer:
    if private_marker not in renderer:
        raise SystemExit("v5.5 CloudNode insertion point was not found")
    renderer = renderer.replace(private_marker, private_replacement)

update_replacement = r'''    void update(const std::vector<render::ContrailRenderSample>& samples) {
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
                    log("Renderer v5.5 could not load the deferred billboard OBJ.\n");
                    return;
                }
                loadedObjectCount_ = 1;
                log("Renderer v5.5 parcel-cluster billboard object loaded after the safe-start gate.\n");
            }
        }

        growInstancePool();
        if (!ready()) return;
        if (samples.empty()) {
            hideAllInstances();
            return;
        }

        struct Accumulator {
            std::uint64_t parcelId = 0;
            std::uint32_t engineIndex = 0;
            engine::Vec3d position {};
            engine::Vec3d tangent {};
            float widthTotal = 0.0f;
            float maximumOpacity = 0.0f;
            float ageTotal = 0.0f;
            std::size_t count = 0;
            bool nearField = false;
        };

        std::unordered_map<std::uint64_t, Accumulator> grouped;
        grouped.reserve(samples.size());
        for (const auto& sample : samples) {
            if (sample.engineIndex >= 2 || !finiteSample(sample) ||
                sample.opacityStrength <= 0.0005f) {
                continue;
            }
            const std::uint64_t key = mix64(
                sample.sourceParcelId ^
                (static_cast<std::uint64_t>(sample.engineIndex) << 57U));
            auto& value = grouped[key];
            value.parcelId = sample.sourceParcelId;
            value.engineIndex = sample.engineIndex;
            value.position.x += sample.localPositionM.x;
            value.position.y += sample.localPositionM.y;
            value.position.z += sample.localPositionM.z;
            value.tangent.x += sample.trailTangentLocal.x;
            value.tangent.y += sample.trailTangentLocal.y;
            value.tangent.z += sample.trailTangentLocal.z;
            value.widthTotal += sample.widthM;
            value.maximumOpacity = std::max(
                value.maximumOpacity, sample.opacityStrength);
            value.ageTotal += sample.ageSeconds;
            value.nearField = value.nearField || sample.nearField;
            ++value.count;
        }

        std::array<std::vector<CloudNode>, 2> anchors;
        for (const auto& item : grouped) {
            const auto& value = item.second;
            if (value.count == 0 || value.engineIndex >= anchors.size()) continue;
            const double inverse = 1.0 / static_cast<double>(value.count);
            CloudNode node;
            node.cloudId = value.parcelId;
            node.parcelId = value.parcelId;
            node.engineIndex = value.engineIndex;
            node.position = {
                value.position.x * inverse,
                value.position.y * inverse,
                value.position.z * inverse
            };
            node.tangent = {
                value.tangent.x * inverse,
                value.tangent.y * inverse,
                value.tangent.z * inverse
            };
            normalizeTangent(
                node.tangent.x, node.tangent.y, node.tangent.z);
            node.widthM = value.widthTotal /
                static_cast<float>(value.count);
            node.opacityStrength = value.maximumOpacity;
            node.ageSeconds = value.ageTotal /
                static_cast<float>(value.count);
            node.nearField = value.nearField || node.ageSeconds < 2.0f;
            anchors[node.engineIndex].push_back(node);
        }

        auto distanceM = [](const engine::Vec3d& a, const engine::Vec3d& b) {
            const double x = b.x - a.x;
            const double y = b.y - a.y;
            const double z = b.z - a.z;
            return std::sqrt(x * x + y * y + z * z);
        };
        auto normalize = [](engine::Vec3d value) {
            const double length = std::sqrt(
                value.x * value.x + value.y * value.y + value.z * value.z);
            if (length > 1.0e-6 && std::isfinite(length)) {
                value.x /= length;
                value.y /= length;
                value.z /= length;
            } else {
                value = {0.0, 0.0, -1.0};
            }
            return value;
        };

        for (auto& stream : anchors) {
            std::stable_sort(stream.begin(), stream.end(), [](const auto& a, const auto& b) {
                if (a.ageSeconds != b.ageSeconds) return a.ageSeconds < b.ageSeconds;
                return a.parcelId < b.parcelId;
            });
            const auto original = stream;
            for (std::size_t index = 0; index < stream.size(); ++index) {
                const bool previous = index > 0 &&
                    std::abs(original[index - 1].ageSeconds - original[index].ageSeconds) <= 1.6f &&
                    distanceM(original[index - 1].position, original[index].position) <= 420.0;
                const bool next = index + 1 < stream.size() &&
                    std::abs(original[index + 1].ageSeconds - original[index].ageSeconds) <= 1.6f &&
                    distanceM(original[index + 1].position, original[index].position) <= 420.0;
                if (original[index].ageSeconds >= 2.0f && previous && next) {
                    stream[index].position.x =
                        0.22 * original[index - 1].position.x +
                        0.56 * original[index].position.x +
                        0.22 * original[index + 1].position.x;
                    stream[index].position.y =
                        0.22 * original[index - 1].position.y +
                        0.56 * original[index].position.y +
                        0.22 * original[index + 1].position.y;
                    stream[index].position.z =
                        0.22 * original[index - 1].position.z +
                        0.56 * original[index].position.z +
                        0.22 * original[index + 1].position.z;
                    stream[index].tangent = normalize({
                        original[index + 1].position.x - original[index - 1].position.x,
                        original[index + 1].position.y - original[index - 1].position.y,
                        original[index + 1].position.z - original[index - 1].position.z
                    });
                }
            }
        }

        const std::size_t available = availableInstanceCount();
        std::array<std::size_t, 2> budgets {
            available / 2,
            available - available / 2
        };
        if (anchors[0].empty()) budgets = {0, available};
        if (anchors[1].empty()) budgets = {available, 0};

        std::vector<CloudNode> selected;
        selected.reserve(available);
        for (std::size_t engine = 0; engine < anchors.size(); ++engine) {
            const std::size_t endBudget = selected.size() + budgets[engine];
            std::vector<CloudNode> young;
            std::vector<CloudNode> companions;
            std::vector<CloudNode> middle;
            std::vector<CloudNode> old;
            for (const auto& anchor : anchors[engine]) {
                CloudNode primary = anchor;
                primary.cloudId = mix64(
                    anchor.parcelId ^
                    (static_cast<std::uint64_t>(engine) << 57U) ^
                    0xd6e8feb86659fd93ULL);
                const std::uint64_t primarySeed = primary.cloudId;
                const float sizeVariation = 0.88f +
                    0.22f * unitHash(primarySeed ^ 0x94d049bb133111ebULL);
                const float densityVariation = 0.82f +
                    0.18f * unitHash(primarySeed ^ 0x632be59bd9b4e019ULL);
                const float ageFade = 1.0f - 0.55f *
                    smoothstep(36.0f, 70.0f, anchor.ageSeconds);
                primary.sizeM = std::clamp(
                    anchor.widthM * (anchor.nearField ? 0.76f : 0.96f) *
                        sizeVariation,
                    0.40f,
                    12.0f);
                primary.alpha = std::clamp(
                    (0.030f + std::sqrt(anchor.opacityStrength) * 0.24f) *
                        densityVariation * ageFade *
                        (anchor.nearField ? 0.75f : 1.0f),
                    0.014f,
                    0.24f);

                if (anchor.ageSeconds <= 28.0f) {
                    young.push_back(primary);
                } else if (anchor.ageSeconds <= 42.0f) {
                    middle.push_back(primary);
                } else if ((anchor.parcelId & 3ULL) == 0ULL) {
                    old.push_back(primary);
                }

                if (anchor.ageSeconds >= 5.0f && anchor.ageSeconds <= 20.0f) {
                    CloudNode companion = anchor;
                    companion.companion = true;
                    companion.cloudId = mix64(
                        anchor.parcelId ^
                        (static_cast<std::uint64_t>(engine) << 57U) ^
                        0xa5a5a5a55a5a5a5aULL);
                    const float engineSign = engine == 0 ? -1.0f : 1.0f;
                    const float phase = engineSign *
                        ((anchor.ageSeconds - 5.0f) * 0.32f +
                         unitHash(companion.cloudId) * 0.65f);
                    engine::Vec3d side {
                        -anchor.tangent.z,
                        0.0,
                        anchor.tangent.x
                    };
                    side = normalize(side);
                    engine::Vec3d up {
                        anchor.tangent.y * side.z - anchor.tangent.z * side.y,
                        anchor.tangent.z * side.x - anchor.tangent.x * side.z,
                        anchor.tangent.x * side.y - anchor.tangent.y * side.x
                    };
                    up = normalize(up);
                    const float rollRamp = smoothstep(
                        5.0f, 14.0f, anchor.ageSeconds);
                    const float rollRadius = std::min(
                        anchor.widthM * (0.08f + 0.15f * rollRamp),
                        2.5f);
                    companion.position.x +=
                        side.x * std::cos(phase) * rollRadius +
                        up.x * std::sin(phase) * rollRadius;
                    companion.position.y +=
                        side.y * std::cos(phase) * rollRadius +
                        up.y * std::sin(phase) * rollRadius;
                    companion.position.z +=
                        side.z * std::cos(phase) * rollRadius +
                        up.z * std::sin(phase) * rollRadius;
                    companion.sizeM = std::clamp(
                        anchor.widthM *
                            (0.56f + 0.12f * unitHash(
                                companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                        0.35f,
                        8.0f);
                    companion.alpha = std::clamp(
                        (0.012f + std::sqrt(anchor.opacityStrength) * 0.12f) *
                            (0.78f + 0.18f * unitHash(
                                companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                        0.008f,
                        0.11f);
                    companions.push_back(companion);
                }
            }

            auto appendTier = [&](const std::vector<CloudNode>& tier) {
                if (tier.empty() || selected.size() >= endBudget) return;
                const std::size_t remaining = endBudget - selected.size();
                if (tier.size() <= remaining) {
                    selected.insert(selected.end(), tier.begin(), tier.end());
                    return;
                }
                if (remaining == 1) {
                    selected.push_back(tier.front());
                    return;
                }
                for (std::size_t slot = 0; slot < remaining; ++slot) {
                    const double ratio = static_cast<double>(slot) /
                        static_cast<double>(remaining - 1);
                    const std::size_t index = static_cast<std::size_t>(
                        std::llround(ratio * static_cast<double>(tier.size() - 1)));
                    selected.push_back(tier[index]);
                }
            };
            appendTier(young);
            appendTier(companions);
            appendTier(middle);
            appendTier(old);
        }

        selectedPerAsset_[0] = selected.size();
        for (const auto& stream : anchors) {
            if (stream.empty()) continue;
            maximumWakeDescentM_ = std::max(
                maximumWakeDescentM_,
                std::max(0.0,
                    stream.front().position.y - stream.back().position.y));
            for (const auto& anchor : stream) {
                if (anchor.ageSeconds >= 5.0f && anchor.ageSeconds <= 20.0f) {
                    ++swirlCandidateCount_;
                }
            }
            for (std::size_t index = 1; index < stream.size(); ++index) {
                const double cosine = std::clamp(
                    stream[index - 1].tangent.x * stream[index].tangent.x +
                    stream[index - 1].tangent.y * stream[index].tangent.y +
                    stream[index - 1].tangent.z * stream[index].tangent.z,
                    -1.0,
                    1.0);
                const double turn = std::acos(cosine) * kRadiansToDegrees;
                if (std::isfinite(turn)) {
                    maximumWakeTurnDeg_ = std::max(maximumWakeTurnDeg_, turn);
                }
            }
        }

        struct Assignment {
            std::size_t poolIndex = 0;
            const CloudNode* cloud = nullptr;
        };
        std::vector<Assignment> assignments;
        assignments.reserve(selected.size());
        std::vector<const CloudNode*> pending;
        pending.reserve(selected.size());

        for (const auto& cloud : selected) {
            const auto existing = renderIdToPool_.find(cloud.cloudId);
            if (existing == renderIdToPool_.end()) {
                pending.push_back(&cloud);
                continue;
            }
            const std::size_t poolIndex = existing->second;
            if (poolIndex >= createdInstanceCount_ || !instances_[poolIndex] ||
                usedThisFrame_[poolIndex] || !slotBound_[poolIndex] ||
                boundRenderIds_[poolIndex] != cloud.cloudId) {
                renderIdToPool_.erase(existing);
                pending.push_back(&cloud);
                continue;
            }
            usedThisFrame_[poolIndex] = true;
            assignments.push_back({poolIndex, &cloud});
            ++ownershipReuseCount_;
        }

        std::size_t searchCursor = 0;
        for (const auto* cloud : pending) {
            while (searchCursor < createdInstanceCount_ &&
                   (!instances_[searchCursor] || usedThisFrame_[searchCursor])) {
                ++searchCursor;
            }
            if (searchCursor >= createdInstanceCount_) break;
            const std::size_t poolIndex = searchCursor++;
            releaseOwnership(poolIndex);
            slotBound_[poolIndex] = true;
            boundRenderIds_[poolIndex] = cloud->cloudId;
            renderIdToPool_[cloud->cloudId] = poolIndex;
            usedThisFrame_[poolIndex] = true;
            assignments.push_back({poolIndex, cloud});
            ++ownershipNewBindingCount_;
        }

        for (const auto& assignment : assignments) {
            setBillboard(assignment.poolIndex, *assignment.cloud);
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
'''

renderer, count = re.subn(
    r"    void update\(const std::vector<render::ContrailRenderSample>& samples\) \{.*?\n    \}\n\n    void setEnabled",
    update_replacement + "\n    void setEnabled",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("v5.5 renderer update function was not replaced")

set_billboard_replacement = r'''    void setBillboard(
        std::size_t poolIndex,
        const CloudNode& cloud) {
        if (poolIndex >= instances_.size() || !instances_[poolIndex]) return;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(cloud.position.x);
        drawInfo.y = static_cast<float>(cloud.position.y);
        drawInfo.z = static_cast<float>(cloud.position.z);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = 0.0f;

        const float normalizedSize = std::clamp(
            (cloud.sizeM - 0.30f) / 11.70f,
            0.0f,
            1.0f);
        const float rotationSeed = unitHash(
            cloud.cloudId ^ 0x9e3779b97f4a7c15ULL);
        const float values[] = {
            1.0f,
            normalizedSize,
            cloud.alpha,
            rotationSeed
        };
        XPLMInstanceSetPosition(
            instances_[poolIndex],
            &drawInfo,
            values);
        active_[poolIndex] = true;
    }
'''
renderer, count = re.subn(
    r"    void setBillboard\(.*?\n    \}\n\n    void hideInstance",
    set_billboard_replacement + "\n    void hideInstance",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("v5.5 setBillboard function was not replaced")

RENDERER.write_text(renderer, encoding="utf-8", newline="")

plugin = PLUGIN.read_text(encoding="utf-8")
plugin = plugin.replace("v5.4.1", "v5.5")
plugin = plugin.replace("V5.4.1", "V5.5")
plugin = plugin.replace("v5 point 4 point 1", "v5 point 5")
plugin = plugin.replace("v5.2", "v5.5")
plugin = plugin.replace("V5.2", "V5.5")
plugin = plugin.replace("v5 point 2", "v5 point 5")
plugin = plugin.replace(
    "renderPlannerSettings_.visibleCapacity = ContrailParticleRenderer::kVisibleCapacity;",
    "renderPlannerSettings_.visibleCapacity = 4096;",
)
plugin = plugin.replace(
    "renderPlannerSettings_.maximumSamplesPerSegment = 8;",
    "renderPlannerSettings_.maximumSamplesPerSegment = 8;\n"
    "        renderPlannerSettings_.maximumSegmentGapM = 420.0;\n"
    "        renderPlannerSettings_.maximumAgeGapSeconds = 1.6f;",
)
plugin = plugin.replace(
    "renderPlannerSettings_.assetCapacities.fill(512);",
    "renderPlannerSettings_.assetCapacities.fill(1024);",
)
PLUGIN.write_text(plugin, encoding="utf-8", newline="")
print("Applied Renderer Foundation v5.5 parcel-cluster and controlled swirl patch")
