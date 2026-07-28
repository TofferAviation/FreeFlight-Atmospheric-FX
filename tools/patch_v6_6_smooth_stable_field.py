#!/usr/bin/env python3
"""Integrate v6.6 smooth, stable, restrained native 3-D contrail field."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def rx(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, lambda _m: replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return out


# ---------------------------------------------------------------------------
# Renderer stability + morphology allocation.
# v6.5 generated 1544 swirl candidates against a 1024-instance field and the
# sampled frame showed 641 releases/rebinds. Even-spacing selection changes many
# chosen IDs whenever the source list grows. Select by stable cloud ID instead.
# ---------------------------------------------------------------------------
r = RENDERER.read_text(encoding="utf-8")

r = once(r, "        48, 164, 164, 164, 164, 144, 144, 32",
         "        56, 164, 164, 164, 164, 148, 156, 8", "v6.6 instance allocation")

r = once(
    r,
    '''                const double packingRadius = 0.08 +
                    0.34 * static_cast<double>(smoothstep(1.5f, 20.0f, sample.ageSeconds));''',
    '''                const double packingRadius = 0.035 +
                    0.16 * static_cast<double>(smoothstep(1.5f, 20.0f, sample.ageSeconds));''',
    "primary packing restraint",
)
r = once(
    r,
    '''                    const double fillPackingRadius = 0.12 +
                        0.44 * static_cast<double>(smoothstep(1.0f, 18.0f, fill.ageSeconds));''',
    '''                    const double fillPackingRadius = 0.05 +
                        0.20 * static_cast<double>(smoothstep(1.0f, 18.0f, fill.ageSeconds));''',
    "fill packing restraint",
)

stable_append = '''    static void appendStableHash(const std::vector<CloudNode>& source,
                                 std::size_t budget,
                                 std::vector<CloudNode>& output) {
        if (budget == 0 || source.empty()) return;
        if (source.size() <= budget) {
            output.insert(output.end(), source.begin(), source.end());
            return;
        }

        std::vector<const CloudNode*> ranked;
        ranked.reserve(source.size());
        for (const auto& node : source) ranked.push_back(&node);
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto* a, const auto* b) {
            const std::uint64_t layerA = static_cast<std::uint64_t>(a->layer);
            const std::uint64_t layerB = static_cast<std::uint64_t>(b->layer);
            const std::uint64_t hashA = mix64(a->cloudId ^ (layerA << 61U) ^ 0x6a09e667f3bcc909ULL);
            const std::uint64_t hashB = mix64(b->cloudId ^ (layerB << 61U) ^ 0x6a09e667f3bcc909ULL);
            if (hashA != hashB) return hashA < hashB;
            return a->cloudId < b->cloudId;
        });

        std::vector<CloudNode> chosen;
        chosen.reserve(budget);
        for (std::size_t index = 0; index < budget; ++index) chosen.push_back(*ranked[index]);
        std::stable_sort(chosen.begin(), chosen.end(), [](const auto& a, const auto& b) {
            if (a.ageSeconds != b.ageSeconds) return a.ageSeconds < b.ageSeconds;
            return a.cloudId < b.cloudId;
        });
        output.insert(output.end(), chosen.begin(), chosen.end());
    }

'''
r = rx(
    r,
    r'''    static void appendEven\(const std::vector<CloudNode>& source,.*?\n    \}\n\n(?=    static void selectAssetCandidates)''',
    stable_append,
    "stable hash selector",
)
r = r.replace("appendEven(primaries, primaryBudget, output);", "appendStableHash(primaries, primaryBudget, output);")
r = r.replace("appendEven(fills, fillBudget, output);", "appendStableHash(fills, fillBudget, output);")
r = r.replace("appendEven(swirls, swirlBudget, output);", "appendStableHash(swirls, swirlBudget, output);")

r = once(
    r,
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.50);\n        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.34);",
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.56);\n        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.34);",
    "stable layer budget",
)

# v6.5 companions were too numerous and used full-size opaque morphology.
r = once(
    r,
    '''            if (sample.ageSeconds >= 3.0f && sample.ageSeconds <= 30.0f &&
                ((mix64(sample.renderId ^ 0x77ULL) % 3ULL) != 0ULL)) {''',
    '''            if (sample.ageSeconds >= 4.5f && sample.ageSeconds <= 26.0f &&
                ((mix64(sample.renderId ^ 0x77ULL) % 4ULL) == 0ULL)) {''',
    "sparse vortex sampling",
)
r = once(r, "                swirl.opacityStrength = sample.opacityStrength * 0.52f;",
         "                swirl.opacityStrength = sample.opacityStrength * 0.40f;", "swirl density restraint")
r = once(
    r,
    "                swirl.assetIndex = assetForCloud(swirl.ageSeconds, swirl.opacityStrength, swirl.cloudId);",
    '''                const std::size_t fullSizeAsset = assetForCloud(swirl.ageSeconds, swirl.opacityStrength, swirl.cloudId);
                const std::size_t fullSizeClass = fullSizeAsset / 2u;
                const std::size_t smallerClass = fullSizeClass > 0u ? fullSizeClass - 1u : 0u;
                swirl.assetIndex = smallerClass * 2u + (fullSizeAsset & 1u);''',
    "subscale vortex morphology",
)
r = once(
    r,
    '''                const float develop = smoothstep(3.0f, 12.0f, sample.ageSeconds);
                const float diffuse = 1.0f - 0.62f * smoothstep(22.0f, 30.0f, sample.ageSeconds);''',
    '''                const float develop = smoothstep(4.5f, 13.0f, sample.ageSeconds);
                const float diffuse = 1.0f - 0.70f * smoothstep(19.0f, 26.0f, sample.ageSeconds);''',
    "vortex development envelope",
)
r = once(
    r,
    "                const double widthBound = std::clamp(static_cast<double>(sample.widthM) * 0.46, 0.32, 2.65);",
    "                const double widthBound = std::clamp(static_cast<double>(sample.widthM) * 0.34, 0.18, 1.75);",
    "vortex radius bound",
)
r = once(
    r,
    "                const double phase = direction * static_cast<double>(sample.ageSeconds - 3.0f) * 0.31 + phaseSeed;",
    "                const double phase = direction * static_cast<double>(sample.ageSeconds - 4.5f) * 0.24 + phaseSeed;",
    "vortex phase rate",
)

r = r.replace("Renderer Foundation v6.5", "Renderer Foundation v6.6")
r = r.replace("Renderer v6.5", "Renderer v6.6")
r = r.replace("ice-white cutout vortex 3-D field", "smooth stable micro-cutout vortex 3-D field")
r = r.replace("alpha-tested ice-white 3-D morphology assets", "smooth micro-cutout ice-white 3-D morphology assets")
for token in (
    "appendStableHash", "budget * 0.56", "% 4ULL) == 0ULL", "smallerClass",
    "sample.widthM) * 0.34, 0.18, 1.75", "56, 164, 164, 164, 164, 148, 156, 8",
):
    if token not in r:
        raise RuntimeError(f"v6.6 renderer integration missing: {token}")
RENDERER.write_text(r, encoding="utf-8", newline="\n")


# Runtime/report identity. Keep the successful v6.5 streamline-locked handoff.
p = PLUGIN.read_text(encoding="utf-8")
p = p.replace("FFAtmo World Contrail Visual Debug Report v6.5 ICE_WHITE_VORTEX_FIELD",
              "FFAtmo World Contrail Visual Debug Report v6.6 SMOOTH_STABLE_VORTEX_FIELD")
p = p.replace("3D CLOUD V6.5 VORTEX READY", "3D CLOUD V6.6 READY")
p = p.replace("LOADING V6.5 ICE VORTEX FIELD", "LOADING V6.6 SMOOTH CLOUD FIELD")
p = p.replace("v6 point 5", "v6 point 6")
p = p.replace("v6.5", "v6.6").replace("V6.5", "V6.6")
p = p.replace(
    "renders alpha-tested ice-white 3-D cloudlets with a streamline-locked live handoff and controlled vortex roll-up.",
    "renders smooth micro-cutout ice-white cloudlets with stable ownership, streamline-locked handoff and restrained vortex roll-up.",
)

p = once(
    p,
    "               << \"render_material_mode=ALPHA_TEST_CUTOUT\" << '\\n'",
    "               << \"render_material_mode=ALPHA_TEST_CUTOUT\" << '\\n'\n"
    "               << \"renderer_selection_mode=STABLE_HASH\" << '\\n'\n"
    "               << \"vortex_sampling_mode=SPARSE_SUBSCALE\" << '\\n'",
    "v6.6 report modes",
)
p = once(
    p,
    '''               << "world_renderer_swirl_candidate_count="
               << worldRenderer_.swirlCandidateCount() << '\n'
               << "simulation_enabled="''',
    '''               << "world_renderer_swirl_candidate_count="
               << worldRenderer_.swirlCandidateCount() << '\n'
               << "world_renderer_primary_cloudlet_count="
               << worldRenderer_.primaryCloudletCount() << '\n'
               << "world_renderer_fill_cloudlet_count="
               << worldRenderer_.fillCloudletCount() << '\n'
               << "world_renderer_swirl_cloudlet_count="
               << worldRenderer_.swirlCloudletCount() << '\n'
               << "world_renderer_maximum_swirl_radius_m="
               << worldRenderer_.maximumSwirlRadiusM() << '\n'
               << "world_renderer_mean_swirl_radius_m="
               << worldRenderer_.meanSwirlRadiusM() << '\n'
               << "simulation_enabled="''',
    "v6.6 cloud layer diagnostics",
)

for token in (
    "SMOOTH_STABLE_VORTEX_FIELD", "3D CLOUD V6.6 READY", "renderer_selection_mode=STABLE_HASH",
    "vortex_sampling_mode=SPARSE_SUBSCALE", "world_renderer_maximum_swirl_radius_m",
    "youngestRenderableLocal", "kHeatBlurHandoffSeconds = 0.04f",
):
    if token not in p:
        raise RuntimeError(f"v6.6 runtime integration missing: {token}")
PLUGIN.write_text(p, encoding="utf-8", newline="\n")

print("Integrated Renderer Foundation v6.6 smooth stable micro-cutout field")
