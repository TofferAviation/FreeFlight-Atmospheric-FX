#!/usr/bin/env python3
"""Apply the v5.4.1 controlled-density billboard recovery to the renderer source."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    args = parser.parse_args()

    text = args.header.read_text(encoding="utf-8")
    if "Renderer Foundation v5.4.1 controlled-density recovery" in text:
        return 0

    text = replace_once(
        text,
        "#include <string>\n#include <vector>",
        "#include <limits>\n#include <string>\n#include <unordered_map>\n#include <vector>",
        "container includes",
    )
    text = text.replace(
        "Renderer Foundation v5.4 ultra-real pooled billboard field.",
        "Renderer Foundation v5.4.1 controlled-density recovery.",
    )
    text = text.replace("Renderer v5.4", "Renderer v5.4.1")

    text = replace_once(
        text,
        "        active_.fill(false);\n        usedThisFrame_.fill(false);",
        "        active_.fill(false);\n        usedThisFrame_.fill(false);\n"
        "        slotRenderIds_.fill(kEmptyRenderId);\n"
        "        slotLastUsedFrame_.fill(0);\n"
        "        slotByRenderId_.clear();\n"
        "        frameCounter_ = 0;",
        "stable-slot startup state",
    )

    text = replace_once(
        text,
        "        poolReadyLogged_ = false;\n    }\n\n    void update",
        "        poolReadyLogged_ = false;\n"
        "        slotByRenderId_.clear();\n"
        "        slotRenderIds_.fill(kEmptyRenderId);\n"
        "        slotLastUsedFrame_.fill(0);\n"
        "        frameCounter_ = 0;\n"
        "    }\n\n    void update",
        "stable-slot stop state",
    )

    text = replace_once(
        text,
        "                !finiteSample(sample) ||\n                sample.opacityStrength <= 0.0005f)",
        "                !finiteSample(sample) ||\n"
        "                sample.opacityStrength <= 0.0010f ||\n"
        "                sample.ageSeconds > 40.0f)",
        "bounded visible age filter",
    )

    old_assignment = """        std::size_t selectedIndex = 0;
        std::size_t rendered = 0;
        for (std::size_t poolIndex = 0;
             poolIndex < createdInstanceCount_ && selectedIndex < selected.size();
             ++poolIndex) {
            if (!instances_[poolIndex]) continue;
            setBillboard(poolIndex, *selected[selectedIndex++]);
            usedThisFrame_[poolIndex] = true;
            ++rendered;
        }

        for (std::size_t poolIndex = 0;
             poolIndex < createdInstanceCount_;
             ++poolIndex) {
            if (!instances_[poolIndex] || usedThisFrame_[poolIndex]) continue;
            hideInstance(poolIndex);
        }

        visibleInstanceCount_ = rendered;
        renderedPerAsset_[0] = rendered;
        if (selectedIndex < selected.size()) {
            poolCapacityDropCount_ += selected.size() - selectedIndex;
        }
"""
    new_assignment = """        ++frameCounter_;
        std::size_t rendered = 0;
        for (const auto* sample : selected) {
            const std::size_t poolIndex = acquireStableSlot(sample->renderId);
            if (poolIndex == kInvalidSlot) {
                ++poolCapacityDropCount_;
                continue;
            }
            setBillboard(poolIndex, *sample);
            usedThisFrame_[poolIndex] = true;
            slotLastUsedFrame_[poolIndex] = frameCounter_;
            ++rendered;
        }

        for (std::size_t poolIndex = 0;
             poolIndex < createdInstanceCount_;
             ++poolIndex) {
            if (!instances_[poolIndex] || usedThisFrame_[poolIndex]) continue;
            hideInstance(poolIndex);
        }

        visibleInstanceCount_ = rendered;
        renderedPerAsset_[0] = rendered;
"""
    text = replace_once(text, old_assignment, new_assignment, "stable assignment loop")

    text = replace_once(
        text,
        "    static constexpr std::size_t kInstanceCreationBatch = 48;\n"
        "    static constexpr float kTwoPi = 6.28318530718f;",
        "    static constexpr std::size_t kInstanceCreationBatch = 48;\n"
        "    static constexpr float kTwoPi = 6.28318530718f;\n"
        "    static constexpr std::size_t kInvalidSlot =\n"
        "        std::numeric_limits<std::size_t>::max();\n"
        "    static constexpr std::uint64_t kEmptyRenderId =\n"
        "        std::numeric_limits<std::uint64_t>::max();",
        "slot constants",
    )

    text = replace_once(
        text,
        "        active_.fill(false);\n        usedThisFrame_.fill(false);\n        createdInstanceCount_ = 0;",
        "        active_.fill(false);\n"
        "        usedThisFrame_.fill(false);\n"
        "        slotRenderIds_.fill(kEmptyRenderId);\n"
        "        slotLastUsedFrame_.fill(0);\n"
        "        slotByRenderId_.clear();\n"
        "        createdInstanceCount_ = 0;",
        "stable-slot destruction",
    )

    function_pattern = re.compile(
        r"    void setBillboard\(\n.*?\n    }\n\n    void hideInstance",
        re.DOTALL,
    )
    replacement = r'''    std::size_t acquireStableSlot(std::uint64_t renderId) {
        const auto existing = slotByRenderId_.find(renderId);
        if (existing != slotByRenderId_.end()) {
            const std::size_t index = existing->second;
            if (index < createdInstanceCount_ && instances_[index] &&
                !usedThisFrame_[index]) {
                return index;
            }
            slotByRenderId_.erase(existing);
        }

        std::size_t selected = kInvalidSlot;
        std::uint64_t oldestFrame = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t index = 0; index < createdInstanceCount_; ++index) {
            if (!instances_[index] || usedThisFrame_[index]) continue;
            if (slotRenderIds_[index] == kEmptyRenderId) {
                selected = index;
                break;
            }
            if (slotLastUsedFrame_[index] < oldestFrame) {
                oldestFrame = slotLastUsedFrame_[index];
                selected = index;
            }
        }
        if (selected == kInvalidSlot) return kInvalidSlot;

        const std::uint64_t previous = slotRenderIds_[selected];
        if (previous != kEmptyRenderId) slotByRenderId_.erase(previous);
        slotRenderIds_[selected] = renderId;
        slotByRenderId_[renderId] = selected;
        return selected;
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

        const float ageSpread = smoothstep(3.0f, 30.0f, sample.ageSeconds);
        const float crossSectionScale = sample.nearField
            ? 0.012f
            : 0.025f + 0.055f * ageSpread;
        const float radialOffset = std::min(
            sample.widthM * crossSectionScale * std::sqrt(radialHash),
            1.25f);
        const double lateral = std::cos(angle) * radialOffset;
        const double vertical = std::sin(angle) * radialOffset;

        const float maximumAxialJitter = sample.nearField
            ? 0.04f
            : std::min(std::max(sample.lengthM, 0.25f) * 0.055f, 0.42f);
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

        // Billboards represent the wake cross-section only. They must never be
        // enlarged to cover longitudinal sample spacing; that produced the
        // v5.4 cotton-sausage failure.
        const float widthScale = sample.nearField
            ? 0.62f
            : 0.72f + 0.12f * ageSpread;
        const float sizeVariation = 0.92f + 0.16f * sizeHash;
        const float targetSizeM = std::clamp(
            sample.widthM * widthScale * sizeVariation,
            0.34f,
            9.0f);
        const float normalizedSize = std::clamp(
            (targetSizeM - 0.30f) / 8.70f,
            0.0f,
            1.0f);

        const float opticalResponse = std::sqrt(
            std::max(sample.opacityStrength, 0.0f));
        const float densityVariation = 0.82f + 0.24f * densityHash;
        const float nearFieldScale = sample.nearField ? 0.64f : 1.0f;
        const float ageFade = 1.0f - smoothstep(24.0f, 40.0f, sample.ageSeconds);
        const float normalizedAlpha = std::clamp(
            (0.018f + opticalResponse * 0.52f) *
                densityVariation * nearFieldScale * ageFade,
            0.006f,
            0.24f);

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

    void hideInstance'''
    text, count = function_pattern.subn(replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"billboard function replacement expected one match, found {count}")

    text = replace_once(
        text,
        "    std::array<bool, kInstancesPerAsset> usedThisFrame_ {};\n"
        "    XPLMDataRef rateDataRef_ = nullptr;",
        "    std::array<bool, kInstancesPerAsset> usedThisFrame_ {};\n"
        "    std::array<std::uint64_t, kInstancesPerAsset> slotRenderIds_ {};\n"
        "    std::array<std::uint64_t, kInstancesPerAsset> slotLastUsedFrame_ {};\n"
        "    std::unordered_map<std::uint64_t, std::size_t> slotByRenderId_;\n"
        "    std::uint64_t frameCounter_ = 0;\n"
        "    XPLMDataRef rateDataRef_ = nullptr;",
        "stable-slot members",
    )

    args.header.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
