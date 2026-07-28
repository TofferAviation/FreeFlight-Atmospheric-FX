#!/usr/bin/env python3
"""Apply Renderer Foundation v5.6 connected-cloud and rebase-safety fixes."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailParticleRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

renderer = RENDERER.read_text(encoding="utf-8")
plugin = PLUGIN.read_text(encoding="utf-8")

renderer = renderer.replace("v5.5.2", "v5.6").replace("V5.5.2", "V5.6")
plugin = plugin.replace("v5.5.2", "v5.6").replace("V5.5.2", "V5.6")
plugin = plugin.replace("v5 point 5 point 2", "v5 point 6")

renderer, count = re.subn(
    r"static constexpr std::size_t kInstancesPerAsset = \d+;",
    "static constexpr std::size_t kInstancesPerAsset = 1536;",
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("Instance pool constant was not found")

renderer, count = re.subn(
    r"static constexpr std::size_t kInstanceCreationBatch = \d+;",
    "static constexpr std::size_t kInstanceCreationBatch = 72;",
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("Instance creation batch was not found")

if "bool fill = false;" not in renderer:
    renderer = renderer.replace(
        "        bool companion = false;\n",
        "        bool companion = false;\n        bool fill = false;\n",
        1,
    )

selection = r'''        std::vector<CloudNode> selected;
        selected.reserve(available);
        for (std::size_t engine = 0; engine < anchors.size(); ++engine) {
            // Keep only the contiguous wake connected to the aircraft. A local
            // origin shift or malformed parcel must not leave a detached hook.
            auto& stream = anchors[engine];
            if (stream.size() > 1) {
                std::size_t contiguous = stream.size();
                for (std::size_t index = 1; index < stream.size(); ++index) {
                    const double gap = distanceM(
                        stream[index - 1].position, stream[index].position);
                    const float ageGap = std::abs(
                        stream[index].ageSeconds - stream[index - 1].ageSeconds);
                    if (!std::isfinite(gap) || gap > 140.0 || ageGap > 1.8f) {
                        contiguous = index;
                        break;
                    }
                }
                stream.resize(contiguous);
            }

            const std::size_t endBudget = selected.size() + budgets[engine];
            std::vector<CloudNode> young;
            std::vector<CloudNode> fills;
            std::vector<CloudNode> companions;
            std::vector<CloudNode> middle;
            std::vector<CloudNode> old;
            young.reserve(stream.size());
            fills.reserve(stream.size());
            companions.reserve(stream.size());
            middle.reserve(stream.size());
            old.reserve(stream.size() / 4 + 1);

            auto ageWidthFloor = [](float ageSeconds) {
                return 1.35f +
                    2.25f * smoothstep(1.0f, 6.0f, ageSeconds) +
                    3.75f * smoothstep(6.0f, 18.0f, ageSeconds) +
                    5.25f * smoothstep(18.0f, 48.0f, ageSeconds);
            };

            std::vector<CloudNode> primaries;
            primaries.reserve(stream.size());
            for (const auto& anchor : stream) {
                CloudNode primary = anchor;
                primary.cloudId = mix64(
                    anchor.parcelId ^
                    (static_cast<std::uint64_t>(engine) << 57U) ^
                    0xd6e8feb86659fd93ULL);
                const std::uint64_t primarySeed = primary.cloudId;
                const float sizeVariation = 0.90f +
                    0.18f * unitHash(primarySeed ^ 0x94d049bb133111ebULL);
                const float densityVariation = 0.86f +
                    0.14f * unitHash(primarySeed ^ 0x632be59bd9b4e019ULL);
                const float ageFade = 1.0f - 0.42f *
                    smoothstep(38.0f, 68.0f, anchor.ageSeconds);
                const float physicalWidth = anchor.widthM *
                    (anchor.nearField ? 1.18f : 1.52f);
                primary.sizeM = std::clamp(
                    std::max(physicalWidth, ageWidthFloor(anchor.ageSeconds)) *
                        sizeVariation,
                    1.10f,
                    19.0f);
                primary.alpha = std::clamp(
                    (0.14f + std::sqrt(anchor.opacityStrength) * 0.58f) *
                        densityVariation * ageFade *
                        (anchor.nearField ? 0.82f : 1.0f),
                    0.10f,
                    0.68f);
                primaries.push_back(primary);

                if (anchor.ageSeconds <= 28.0f) {
                    young.push_back(primary);
                } else if (anchor.ageSeconds <= 44.0f) {
                    middle.push_back(primary);
                } else if ((anchor.parcelId & 3ULL) == 0ULL) {
                    old.push_back(primary);
                }

                if (anchor.ageSeconds >= 5.0f && anchor.ageSeconds <= 22.0f) {
                    CloudNode companion = anchor;
                    companion.companion = true;
                    companion.cloudId = mix64(
                        anchor.parcelId ^
                        (static_cast<std::uint64_t>(engine) << 57U) ^
                        0xa5a5a5a55a5a5a5aULL);
                    const float engineSign = engine == 0 ? -1.0f : 1.0f;
                    const float phase = engineSign *
                        ((anchor.ageSeconds - 5.0f) * 0.34f +
                         unitHash(companion.cloudId) * 0.60f);
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
                        5.0f, 15.0f, anchor.ageSeconds);
                    const float rollRadius = std::clamp(
                        0.55f + anchor.widthM *
                            (0.12f + 0.22f * rollRamp),
                        0.55f,
                        4.0f);
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
                        primary.sizeM *
                            (0.50f + 0.10f * unitHash(
                                companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                        1.0f,
                        10.5f);
                    companion.alpha = std::clamp(
                        primary.alpha *
                            (0.28f + 0.10f * unitHash(
                                companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                        0.045f,
                        0.24f);
                    companions.push_back(companion);
                }
            }

            // Fill the 25-30 metre cruise gaps with a stable midpoint cloud.
            // IDs are derived from the younger parcel, so ownership remains
            // stable and the filler never jumps between pool instances.
            for (std::size_t index = 0; index + 1 < stream.size(); ++index) {
                const auto& younger = stream[index];
                const auto& older = stream[index + 1];
                if (younger.ageSeconds > 32.0f) continue;
                const double gap = distanceM(younger.position, older.position);
                const float ageGap = older.ageSeconds - younger.ageSeconds;
                if (!std::isfinite(gap) || gap < 4.0 || gap > 70.0 ||
                    ageGap <= 0.0f || ageGap > 0.8f) {
                    continue;
                }
                CloudNode fill = younger;
                fill.fill = true;
                fill.companion = false;
                fill.cloudId = mix64(
                    younger.parcelId ^
                    (static_cast<std::uint64_t>(engine) << 57U) ^
                    0x6a09e667f3bcc909ULL);
                fill.position = {
                    0.50 * (younger.position.x + older.position.x),
                    0.50 * (younger.position.y + older.position.y),
                    0.50 * (younger.position.z + older.position.z)
                };
                fill.tangent = normalize({
                    older.position.x - younger.position.x,
                    older.position.y - younger.position.y,
                    older.position.z - younger.position.z
                });
                const CloudNode& primary = primaries[index];
                fill.sizeM = std::clamp(primary.sizeM * 0.82f, 1.0f, 15.0f);
                fill.alpha = std::clamp(primary.alpha * 0.58f, 0.065f, 0.36f);
                fills.push_back(fill);
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
            appendTier(fills);
            appendTier(companions);
            appendTier(middle);
            appendTier(old);
        }

        selectedPerAsset_[0] = selected.size();'''

renderer, count = re.subn(
    r"        std::vector<CloudNode> selected;.*?        selectedPerAsset_\[0\] = selected\.size\(\);",
    lambda _: selection,
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError("v5.5 parcel selection block was not replaced")

# Hide all instances for one frame after a local-origin shift. The following
# frame is rebuilt from world-space parcels in the new X-Plane local frame.
old_rebase = '''        if (latestNormalized_.localOriginRebased) {
            log("X-Plane local origin shifted; engine-world contrail continuity retained.\\n");
        }'''
new_rebase = '''        if (latestNormalized_.localOriginRebased) {
            latestRenderPlan_ = {};
            worldRenderer_.update({});
            log("X-Plane local origin shifted; billboard field flushed and rebuilt from world parcels.\\n");
        }'''
if old_rebase not in plugin:
    raise RuntimeError("Local-origin rebase handler was not found")
plugin = plugin.replace(old_rebase, new_rebase, 1)

RENDERER.write_text(renderer, encoding="utf-8", newline="\n")
PLUGIN.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v5.6 connected cloud and rebase guard")
