#!/usr/bin/env python3
"""Apply Renderer Foundation v5.7 filled axial-core cloud rendering."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailParticleRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

renderer = RENDERER.read_text(encoding="utf-8")
plugin = PLUGIN.read_text(encoding="utf-8")

renderer = renderer.replace("v5.6", "v5.7").replace("V5.6", "V5.7")
plugin = plugin.replace("v5.6", "v5.7").replace("V5.6", "V5.7")
plugin = plugin.replace("v5 point 6", "v5 point 7")

# v5.6 used circular billboard size to bridge longitudinal spacing. With an
# axial core, size once again represents cloud width while LENGTH_CURVE provides
# the longitudinal footprint. This avoids both beads and oversized cotton balls.
renderer, count = re.subn(
    r"            auto ageWidthFloor = \[\]\(float ageSeconds\) \{.*?            \};",
    '''            auto ageWidthFloor = [](float ageSeconds) {
                return 0.85f +
                    0.55f * smoothstep(1.0f, 6.0f, ageSeconds) +
                    1.10f * smoothstep(6.0f, 18.0f, ageSeconds) +
                    1.65f * smoothstep(18.0f, 48.0f, ageSeconds);
            };''',
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError("v5.6 age-width floor was not found")

renderer, count = re.subn(
    r"                const float physicalWidth = anchor\.widthM \*\n                    \(anchor\.nearField \? 1\.18f : 1\.52f\);\n                primary\.sizeM = std::clamp\(\n                    std::max\(physicalWidth, ageWidthFloor\(anchor\.ageSeconds\)\) \*\n                        sizeVariation,\n                    1\.10f,\n                    19\.0f\);",
    '''                const float physicalWidth = anchor.widthM *
                    (anchor.nearField ? 0.72f : 0.94f);
                primary.sizeM = std::clamp(
                    std::max(physicalWidth, ageWidthFloor(anchor.ageSeconds)) *
                        sizeVariation,
                    0.65f,
                    11.5f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 primary size calibration was not found")

renderer, count = re.subn(
    r"                primary\.alpha = std::clamp\(\n                    \(0\.14f \+ std::sqrt\(anchor\.opacityStrength\) \* 0\.58f\) \*\n                        densityVariation \* ageFade \*\n                        \(anchor\.nearField \? 0\.82f : 1\.0f\),\n                    0\.10f,\n                    0\.68f\);",
    '''                // Two particle layers are emitted per instance in v5.7.
                // Keep the shared input alpha moderate: the axial core receives
                // most of it and the round halo receives only a soft fraction.
                primary.alpha = std::clamp(
                    (0.10f + std::sqrt(anchor.opacityStrength) * 0.44f) *
                        densityVariation * ageFade *
                        (anchor.nearField ? 0.78f : 1.0f),
                    0.060f,
                    0.48f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 primary alpha calibration was not found")

renderer, count = re.subn(
    r"                    companion\.sizeM = std::clamp\(\n                        primary\.sizeM \*\n                            \(0\.50f \+ 0\.10f \* unitHash\(\n                                companion\.cloudId \^ 0xbf58476d1ce4e5b9ULL\)\),\n                        1\.0f,\n                        10\.5f\);\n                    companion\.alpha = std::clamp\(\n                        primary\.alpha \*\n                            \(0\.28f \+ 0\.10f \* unitHash\(\n                                companion\.cloudId \^ 0x632be59bd9b4e019ULL\)\),\n                        0\.045f,\n                        0\.24f\);",
    '''                    companion.sizeM = std::clamp(
                        primary.sizeM *
                            (0.48f + 0.10f * unitHash(
                                companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                        0.65f,
                        7.0f);
                    companion.alpha = std::clamp(
                        primary.alpha *
                            (0.22f + 0.08f * unitHash(
                                companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                        0.025f,
                        0.14f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 companion calibration was not found")

renderer, count = re.subn(
    r"                fill\.sizeM = std::clamp\(primary\.sizeM \* 0\.82f, 1\.0f, 15\.0f\);\n                fill\.alpha = std::clamp\(primary\.alpha \* 0\.58f, 0\.065f, 0\.36f\);",
    '''                fill.sizeM = std::clamp(primary.sizeM * 0.92f, 0.65f, 10.5f);
                fill.alpha = std::clamp(primary.alpha * 0.62f, 0.040f, 0.30f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 midpoint fill calibration was not found")

# Orient the OBJ's negative-Z emitter axis along the physical wake tangent. The
# axial particle uses that direction for its long dimension; the second particle
# remains a camera-facing halo. Both retain one persistent XPLM instance owner.
set_billboard = r'''    void setBillboard(
        std::size_t poolIndex,
        const CloudNode& cloud) {
        if (poolIndex >= instances_.size() || !instances_[poolIndex]) return;

        double tx = cloud.tangent.x;
        double ty = cloud.tangent.y;
        double tz = cloud.tangent.z;
        normalizeTangent(tx, ty, tz);
        const double horizontal = std::sqrt(tx * tx + tz * tz);

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(cloud.position.x);
        drawInfo.y = static_cast<float>(cloud.position.y);
        drawInfo.z = static_cast<float>(cloud.position.z);
        drawInfo.heading = static_cast<float>(
            std::atan2(tx, -tz) * kRadiansToDegrees);
        drawInfo.pitch = static_cast<float>(
            std::atan2(ty, std::max(horizontal, 1.0e-6)) * kRadiansToDegrees);
        drawInfo.roll = 0.0f;

        // Match the v5.7 layered particle asset's 0.65-12.0 metre width range.
        // Longitudinal coverage is supplied by the axial LENGTH_CURVE rather
        // than by inflating a circular billboard.
        const float normalizedSize = std::clamp(
            (cloud.sizeM - 0.65f) / 11.35f,
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
    lambda _: set_billboard + "\n    void hideInstance",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError("v5.6 setBillboard function was not replaced")

RENDERER.write_text(renderer, encoding="utf-8", newline="\n")
PLUGIN.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v5.7 filled axial-core cloud renderer")
