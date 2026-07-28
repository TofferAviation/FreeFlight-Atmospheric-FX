#!/usr/bin/env python3
"""Integrate only the v6.3 native 3-D morphology renderer changes."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def rx(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")
    return out


r = RENDERER.read_text(encoding="utf-8")
r = once(r, "static constexpr std::size_t kAssetCount = 4;",
         "static constexpr std::size_t kAssetCount = 8;", "asset count")
r = once(r, "static constexpr std::size_t kInstancesPerAsset = 256;",
         "static constexpr std::size_t kInstancesPerAsset = 128;", "pool split")
r = once(r, "static constexpr std::size_t kInstanceCreationBatchPerAsset = 24;",
         "static constexpr std::size_t kInstanceCreationBatchPerAsset = 16;", "allocation batch")

r = rx(
    r,
    r'''objectPaths_\s*=\s*\{\s*assetDirectory_ / "contrail_cloudlet_near\.obj",\s*assetDirectory_ / "contrail_cloudlet_young\.obj",\s*assetDirectory_ / "contrail_cloudlet_mature\.obj",\s*assetDirectory_ / "contrail_cloudlet_old\.obj"\s*\};''',
    '''objectPaths_ = {
            assetDirectory_ / "contrail_cloudlet_near_soft.obj",
            assetDirectory_ / "contrail_cloudlet_near_dense.obj",
            assetDirectory_ / "contrail_cloudlet_young_soft.obj",
            assetDirectory_ / "contrail_cloudlet_young_dense.obj",
            assetDirectory_ / "contrail_cloudlet_mature_soft.obj",
            assetDirectory_ / "contrail_cloudlet_mature_dense.obj",
            assetDirectory_ / "contrail_cloudlet_old_soft.obj",
            assetDirectory_ / "contrail_cloudlet_old_dense.obj"
        };''',
    "object paths",
)

r = rx(
    r,
    r'''static std::size_t assetForAge\(float ageSeconds\)\s*\{\s*if \(ageSeconds < 3\.0f\) return 0;\s*if \(ageSeconds < 10\.0f\) return 1;\s*if \(ageSeconds < 27\.0f\) return 2;\s*return 3;\s*\}''',
    '''static std::size_t ageClassForAge(float ageSeconds) {
        if (ageSeconds < 2.5f) return 0;
        if (ageSeconds < 9.0f) return 1;
        if (ageSeconds < 25.0f) return 2;
        return 3;
    }

    static std::size_t assetForCloud(float ageSeconds,
                                     float opacityStrength,
                                     std::uint64_t stableId) {
        const std::size_t ageClass = ageClassForAge(ageSeconds);
        const float threshold = 0.22f +
            0.18f * unitHash(stableId ^ 0x63a7d31f92c4b815ULL);
        return ageClass * 2u + (opacityStrength >= threshold ? 1u : 0u);
    }''',
    "morphology selector",
)

anchor = '''    static void normalize(engine::Vec3d& v) {
        const double m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (std::isfinite(m) && m > 1.0e-7) {
            v.x /= m; v.y /= m; v.z /= m;
        } else {
            v = {0.0, 0.0, -1.0};
        }
    }'''
helper = anchor + '''

    static void applyPackingOffset(CloudNode& node,
                                   std::uint64_t seed,
                                   double maximumRadiusM) {
        if (!(maximumRadiusM > 0.0)) return;
        engine::Vec3d tangent = node.tangent;
        normalize(tangent);
        engine::Vec3d side {-tangent.z, 0.0, tangent.x};
        const double sideLength = std::sqrt(side.x * side.x + side.z * side.z);
        if (sideLength < 1.0e-7) side = {1.0, 0.0, 0.0};
        else { side.x /= sideLength; side.z /= sideLength; }
        engine::Vec3d up {
            tangent.y * side.z - tangent.z * side.y,
            tangent.z * side.x - tangent.x * side.z,
            tangent.x * side.y - tangent.y * side.x
        };
        normalize(up);
        const double phase = kPi * 2.0 * static_cast<double>(unitHash(seed ^ 0x91b3ULL));
        const double radius = maximumRadiusM *
            (0.22 + 0.78 * static_cast<double>(unitHash(seed ^ 0x3fd9ULL)));
        node.position.x += side.x * std::cos(phase) * radius + up.x * std::sin(phase) * radius;
        node.position.y += side.y * std::cos(phase) * radius + up.y * std::sin(phase) * radius;
        node.position.z += side.z * std::cos(phase) * radius + up.z * std::sin(phase) * radius;
    }'''
r = once(r, anchor, helper, "packing helper")

r = once(
    r,
    '''            const auto& sample = *stream[i];
            const std::size_t asset = assetForAge(sample.ageSeconds);''',
    '''            const auto& sample = *stream[i];
            const std::uint64_t primaryCloudId = primaryId(sample);
            const std::size_t asset = assetForCloud(
                sample.ageSeconds, sample.opacityStrength, primaryCloudId);''',
    "primary morphology",
)
r = once(r, "                node.cloudId = primaryId(sample);",
         "                node.cloudId = primaryCloudId;", "primary id")
r = once(
    r,
    '''                node.tangent = sample.trailTangentLocal;
                normalize(node.tangent);
                node.ageSeconds = sample.ageSeconds;''',
    '''                node.tangent = sample.trailTangentLocal;
                normalize(node.tangent);
                const double packingRadius = 0.08 +
                    0.34 * static_cast<double>(smoothstep(1.5f, 20.0f, sample.ageSeconds));
                applyPackingOffset(node, node.cloudId, packingRadius);
                node.ageSeconds = sample.ageSeconds;''',
    "primary packing",
)

r = once(r, "gap >= 3.2 && gap <= 14.0", "gap >= 2.2 && gap <= 16.0", "fill range")
r = rx(
    r,
    r'''fill\.cloudId = fillId\(sample, next\);\s*fill\.engineIndex = engineIndex;\s*fill\.ageSeconds = 0\.5f \* \(sample\.ageSeconds \+ next\.ageSeconds\);\s*fill\.assetIndex = assetForAge\(fill\.ageSeconds\);\s*fill\.layer = Layer::Fill;\s*fill\.position = midpoint\(sample\.localPositionM, next\.localPositionM\);\s*fill\.tangent = \{\s*sample\.trailTangentLocal\.x \+ next\.trailTangentLocal\.x,\s*sample\.trailTangentLocal\.y \+ next\.trailTangentLocal\.y,\s*sample\.trailTangentLocal\.z \+ next\.trailTangentLocal\.z\s*\};\s*normalize\(fill\.tangent\);\s*fill\.opacityStrength = 0\.5f \* \(sample\.opacityStrength \+ next\.opacityStrength\);''',
    '''fill.cloudId = fillId(sample, next);
                    fill.engineIndex = engineIndex;
                    fill.ageSeconds = 0.5f * (sample.ageSeconds + next.ageSeconds);
                    fill.layer = Layer::Fill;
                    fill.position = midpoint(sample.localPositionM, next.localPositionM);
                    fill.tangent = {
                        sample.trailTangentLocal.x + next.trailTangentLocal.x,
                        sample.trailTangentLocal.y + next.trailTangentLocal.y,
                        sample.trailTangentLocal.z + next.trailTangentLocal.z
                    };
                    normalize(fill.tangent);
                    fill.opacityStrength = 0.5f * (sample.opacityStrength + next.opacityStrength);
                    fill.assetIndex = assetForCloud(fill.ageSeconds, fill.opacityStrength, fill.cloudId);
                    const double fillPackingRadius = 0.12 +
                        0.44 * static_cast<double>(smoothstep(1.0f, 18.0f, fill.ageSeconds));
                    applyPackingOffset(fill, fill.cloudId ^ 0x44ULL, fillPackingRadius);''',
    "fill morphology",
)

r = rx(
    r,
    r'''swirl\.cloudId = swirlId\(sample\);\s*swirl\.engineIndex = engineIndex;\s*swirl\.ageSeconds = sample\.ageSeconds;\s*swirl\.assetIndex = assetForAge\(sample\.ageSeconds\);\s*swirl\.layer = Layer::Swirl;\s*swirl\.tangent = sample\.trailTangentLocal;\s*normalize\(swirl\.tangent\);\s*swirl\.opacityStrength = sample\.opacityStrength \* 0\.72f;''',
    '''swirl.cloudId = swirlId(sample);
                swirl.engineIndex = engineIndex;
                swirl.ageSeconds = sample.ageSeconds;
                swirl.layer = Layer::Swirl;
                swirl.tangent = sample.trailTangentLocal;
                normalize(swirl.tangent);
                swirl.opacityStrength = sample.opacityStrength * 0.64f;
                swirl.assetIndex = assetForCloud(swirl.ageSeconds, swirl.opacityStrength, swirl.cloudId);''',
    "swirl morphology",
)

r = once(
    r,
    '''        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.58);
        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.25);''',
    '''        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.50);
        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.34);''',
    "layer budgets",
)

r = r.replace("reached 256 persistent 3-D instances", "reached 128 persistent 3-D instances")
r = r.replace("Renderer Foundation v6.2", "Renderer Foundation v6.3")
r = r.replace("Renderer v6.2", "Renderer v6.3")
r = r.replace("four native 3-D cloudlet classes", "eight native 3-D morphology assets")
r = r.replace("four-class native 3-D cloud field", "eight-asset soft/dense native 3-D morphology field")
r = r.replace("Four closed irregular 3-D OBJ size classes are", "Eight soft/dense irregular 3-D OBJ morphology assets are")

for token in (
    "kAssetCount = 8",
    "kInstancesPerAsset = 128",
    "contrail_cloudlet_old_dense.obj",
    "assetForCloud",
    "applyPackingOffset",
    "budget * 0.34",
):
    if token not in r:
        raise RuntimeError(f"v6.3 morphology integration missing: {token}")

RENDERER.write_text(r, encoding="utf-8", newline="\n")
print("Integrated v6.3 renderer morphology")
