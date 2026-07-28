#!/usr/bin/env python3
"""Deterministically integrate Renderer Foundation v6.3.

This replaces the first v6.3 text patcher with deliberately small anchors and
checked regex substitutions. The renderer remains XPLMInstance-only.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one regex match, found {count}")
    return result


# -----------------------------------------------------------------------------
# Native 3-D renderer morphology pass.
# -----------------------------------------------------------------------------
r = RENDERER.read_text(encoding="utf-8")
r = replace_once(
    r,
    "static constexpr std::size_t kAssetCount = 4;",
    "static constexpr std::size_t kAssetCount = 8;",
    "asset count",
)
r = replace_once(
    r,
    "static constexpr std::size_t kInstancesPerAsset = 256;",
    "static constexpr std::size_t kInstancesPerAsset = 128;",
    "instance split",
)
r = replace_once(
    r,
    "static constexpr std::size_t kInstanceCreationBatchPerAsset = 24;",
    "static constexpr std::size_t kInstanceCreationBatchPerAsset = 16;",
    "allocation batch",
)

r = regex_once(
    r,
    r'''objectPaths_\s*=\s*\{\s*
\s*assetDirectory_ / "contrail_cloudlet_near\.obj",\s*
\s*assetDirectory_ / "contrail_cloudlet_young\.obj",\s*
\s*assetDirectory_ / "contrail_cloudlet_mature\.obj",\s*
\s*assetDirectory_ / "contrail_cloudlet_old\.obj"\s*
\s*\};''',
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
    "morphology object paths",
)

r = regex_once(
    r,
    r'''static std::size_t assetForAge\(float ageSeconds\)\s*\{\s*
\s*if \(ageSeconds < 3\.0f\) return 0;\s*
\s*if \(ageSeconds < 10\.0f\) return 1;\s*
\s*if \(ageSeconds < 27\.0f\) return 2;\s*
\s*return 3;\s*
\s*\}''',
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
        // Stable per-cloud threshold prevents a whole age band from switching
        // optical morphology on the same frame.
        const float threshold = 0.22f +
            0.18f * unitHash(stableId ^ 0x63a7d31f92c4b815ULL);
        const bool dense = opacityStrength >= threshold;
        return ageClass * 2u + (dense ? 1u : 0u);
    }''',
    "age/morphology selector",
)

normalize_anchor = '''    static void normalize(engine::Vec3d& v) {
        const double m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (std::isfinite(m) && m > 1.0e-7) {
            v.x /= m; v.y /= m; v.z /= m;
        } else {
            v = {0.0, 0.0, -1.0};
        }
    }'''
packing_helper = normalize_anchor + '''

    static void applyPackingOffset(CloudNode& node,
                                   std::uint64_t seed,
                                   double maximumRadiusM) {
        if (!(maximumRadiusM > 0.0)) return;
        engine::Vec3d tangent = node.tangent;
        normalize(tangent);

        engine::Vec3d side {-tangent.z, 0.0, tangent.x};
        const double sideLength = std::sqrt(side.x * side.x + side.z * side.z);
        if (sideLength < 1.0e-7) {
            side = {1.0, 0.0, 0.0};
        } else {
            side.x /= sideLength;
            side.z /= sideLength;
        }
        engine::Vec3d up {
            tangent.y * side.z - tangent.z * side.y,
            tangent.z * side.x - tangent.x * side.z,
            tangent.x * side.y - tangent.y * side.x
        };
        normalize(up);

        const double phase = kPi * 2.0 *
            static_cast<double>(unitHash(seed ^ 0x91b3ULL));
        const double radius = maximumRadiusM *
            (0.22 + 0.78 * static_cast<double>(unitHash(seed ^ 0x3fd9ULL)));
        node.position.x += side.x * std::cos(phase) * radius +
                           up.x * std::sin(phase) * radius;
        node.position.y += side.y * std::cos(phase) * radius +
                           up.y * std::sin(phase) * radius;
        node.position.z += side.z * std::cos(phase) * radius +
                           up.z * std::sin(phase) * radius;
    }'''
r = replace_once(r, normalize_anchor, packing_helper, "packing helper insertion")

r = replace_once(
    r,
    '''            const auto& sample = *stream[i];
            const std::size_t asset = assetForAge(sample.ageSeconds);''',
    '''            const auto& sample = *stream[i];
            const std::uint64_t primaryCloudId = primaryId(sample);
            const std::size_t asset = assetForCloud(
                sample.ageSeconds, sample.opacityStrength, primaryCloudId);''',
    "primary asset selection",
)
r = replace_once(
    r,
    "                node.cloudId = primaryId(sample);",
    "                node.cloudId = primaryCloudId;",
    "primary stable id",
)
r = replace_once(
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
    "primary micro-packing",
)

r = replace_once(
    r,
    "gap >= 3.2 && gap <= 14.0",
    "gap >= 2.2 && gap <= 16.0",
    "fill gap window",
)
r = regex_once(
    r,
    r'''fill\.cloudId = fillId\(sample, next\);\s*
\s*fill\.engineIndex = engineIndex;\s*
\s*fill\.ageSeconds = 0\.5f \* \(sample\.ageSeconds \+ next\.ageSeconds\);\s*
\s*fill\.assetIndex = assetForAge\(fill\.ageSeconds\);\s*
\s*fill\.layer = Layer::Fill;\s*
\s*fill\.position = midpoint\(sample\.localPositionM, next\.localPositionM\);\s*
\s*fill\.tangent = \{\s*
\s*sample\.trailTangentLocal\.x \+ next\.trailTangentLocal\.x,\s*
\s*sample\.trailTangentLocal\.y \+ next\.trailTangentLocal\.y,\s*
\s*sample\.trailTangentLocal\.z \+ next\.trailTangentLocal\.z\s*
\s*\};\s*
\s*normalize\(fill\.tangent\);\s*
\s*fill\.opacityStrength = 0\.5f \* \(sample\.opacityStrength \+ next\.opacityStrength\);''',
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
                    fill.assetIndex = assetForCloud(
                        fill.ageSeconds, fill.opacityStrength, fill.cloudId);
                    const double fillPackingRadius = 0.12 +
                        0.44 * static_cast<double>(smoothstep(1.0f, 18.0f, fill.ageSeconds));
                    applyPackingOffset(fill, fill.cloudId ^ 0x44ULL, fillPackingRadius);''',
    "fill morphology/packing",
)

r = regex_once(
    r,
    r'''swirl\.cloudId = swirlId\(sample\);\s*
\s*swirl\.engineIndex = engineIndex;\s*
\s*swirl\.ageSeconds = sample\.ageSeconds;\s*
\s*swirl\.assetIndex = assetForAge\(sample\.ageSeconds\);\s*
\s*swirl\.layer = Layer::Swirl;\s*
\s*swirl\.tangent = sample\.trailTangentLocal;\s*
\s*normalize\(swirl\.tangent\);\s*
\s*swirl\.opacityStrength = sample\.opacityStrength \* 0\.72f;''',
    '''swirl.cloudId = swirlId(sample);
                swirl.engineIndex = engineIndex;
                swirl.ageSeconds = sample.ageSeconds;
                swirl.layer = Layer::Swirl;
                swirl.tangent = sample.trailTangentLocal;
                normalize(swirl.tangent);
                swirl.opacityStrength = sample.opacityStrength * 0.64f;
                swirl.assetIndex = assetForCloud(
                    swirl.ageSeconds, swirl.opacityStrength, swirl.cloudId);''',
    "swirl morphology selection",
)

r = replace_once(
    r,
    '''        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.58);
        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.25);''',
    '''        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.50);
        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.34);''',
    "layer budget rebalance",
)
r = r.replace("reached 256 persistent 3-D instances", "reached 128 persistent 3-D instances")
r = r.replace("Renderer Foundation v6.2", "Renderer Foundation v6.3")
r = r.replace("Renderer v6.2", "Renderer v6.3")
r = r.replace("four native 3-D cloudlet classes", "eight native 3-D morphology assets")
r = r.replace("four-class native 3-D cloud field", "eight-asset soft/dense native 3-D morphology field")
r = r.replace("Four closed irregular 3-D OBJ size classes are", "Eight soft/dense irregular 3-D OBJ morphology assets are")

for token, label in (
    ("kAssetCount = 8", "8 asset pool"),
    ("kInstancesPerAsset = 128", "128 slots per morphology asset"),
    ("assetForCloud", "morphology selector"),
    ("applyPackingOffset", "micro-packing"),
    ("contrail_cloudlet_old_dense.obj", "old dense asset path"),
):
    if token not in r:
        raise RuntimeError(f"v6.3 renderer missing {label}")
RENDERER.write_text(r, encoding="utf-8", newline="\n")


# -----------------------------------------------------------------------------
# Runtime onset-alignment pass.
# -----------------------------------------------------------------------------
p = PLUGIN.read_text(encoding="utf-8")
p = replace_once(
    p,
    "constexpr float kHeatBlurHandoffSeconds = 0.12f;",
    "constexpr float kHeatBlurHandoffSeconds = 0.08f;",
    "heat-blur spatial handoff constant",
)

p = regex_once(
    p,
    r'''const float handoffSeconds = std::clamp\(\s*
\s*handoff\.visibleStartSeconds,\s*
\s*0\.05f,\s*
\s*0\.45f\);\s*
\s*render::ContrailRenderInput head;\s*
\s*head\.sourceParcelId = kSyntheticHeadIdBase \+ engineIndex;\s*
\s*head\.engineIndex = static_cast<std::uint32_t>\(engineIndex\);\s*
\s*head\.localPositionM = \{\s*
\s*exhausts\[engineIndex\]\.x -\s*
\s*static_cast<double>\(snapshot\.linearVelocityLocalMps\.x\) \* handoffSeconds,\s*
\s*exhausts\[engineIndex\]\.y -\s*
\s*static_cast<double>\(snapshot\.linearVelocityLocalMps\.y\) \* handoffSeconds,\s*
\s*exhausts\[engineIndex\]\.z -\s*
\s*static_cast<double>\(snapshot\.linearVelocityLocalMps\.z\) \* handoffSeconds\s*
\s*\};\s*
\s*head\.physicsRadiusM = 0\.18f;\s*
\s*head\.opticalDepth = std::max\(parcel->opticalDepth \* 0\.035f, 0\.005f\);\s*
\s*head\.normalizedIceMass = std::max\(parcel->normalizedIceMass \* 0\.035f, 0\.005f\);\s*
\s*head\.ageSeconds = handoffSeconds;''',
    '''const float thermodynamicStartSeconds = std::clamp(
                handoff.visibleStartSeconds,
                0.05f,
                0.45f);
            const double vx = static_cast<double>(snapshot.linearVelocityLocalMps.x);
            const double vy = static_cast<double>(snapshot.linearVelocityLocalMps.y);
            const double vz = static_cast<double>(snapshot.linearVelocityLocalMps.z);
            const double localSpeedMps = std::sqrt(vx * vx + vy * vy + vz * vz);
            const double effectiveSpeedMps = std::max(
                localSpeedMps,
                static_cast<double>(snapshot.trueAirspeedMps));
            const double spatialOnsetM = std::clamp(
                effectiveSpeedMps * static_cast<double>(kHeatBlurHandoffSeconds),
                18.0,
                28.0);
            const double spatialOnsetSeconds = localSpeedMps > 1.0
                ? spatialOnsetM / localSpeedMps
                : static_cast<double>(kHeatBlurHandoffSeconds);
            latestSpatialOnsetTargetM_ = spatialOnsetM;

            render::ContrailRenderInput head;
            head.sourceParcelId = kSyntheticHeadIdBase + engineIndex;
            head.engineIndex = static_cast<std::uint32_t>(engineIndex);
            head.localPositionM = {
                exhausts[engineIndex].x - vx * spatialOnsetSeconds,
                exhausts[engineIndex].y - vy * spatialOnsetSeconds,
                exhausts[engineIndex].z - vz * spatialOnsetSeconds
            };
            head.physicsRadiusM = 0.15f;
            head.opticalDepth = std::max(parcel->opticalDepth * 0.026f, 0.0035f);
            head.normalizedIceMass = std::max(parcel->normalizedIceMass * 0.026f, 0.0035f);
            head.ageSeconds = static_cast<float>(std::min(
                static_cast<double>(thermodynamicStartSeconds), spatialOnsetSeconds));''',
    "bounded spatial onset",
)

p = replace_once(
    p,
    '''        maximumExhaustToFirstVisibleM_ = 0.0;
        latestCondensationStartSeconds_ = 0.0f;''',
    '''        maximumExhaustToFirstVisibleM_ = 0.0;
        latestSpatialOnsetTargetM_ = 0.0;
        latestCondensationStartSeconds_ = 0.0f;''',
    "onset diagnostic reset",
)
p = replace_once(
    p,
    '''    double maximumExhaustToFirstVisibleM_ = 0.0;
    float latestCondensationStartSeconds_ = 0.0f;''',
    '''    double maximumExhaustToFirstVisibleM_ = 0.0;
    double latestSpatialOnsetTargetM_ = 0.0;
    float latestCondensationStartSeconds_ = 0.0f;''',
    "onset diagnostic member",
)

p = replace_once(
    p,
    '''               << "current_condensation_full_seconds="
               << latestCondensationFullSeconds_ << '\n'
               << "geometry_status=" << geometryStatus_ << '\n' ''',
    '''               << "current_condensation_full_seconds="
               << latestCondensationFullSeconds_ << '\n'
               << "visual_head_spatial_onset_target_m="
               << latestSpatialOnsetTargetM_ << '\n'
               << "geometry_status=" << geometryStatus_ << '\n' ''',
    "onset report field",
)

asset_loop = "        for (std::size_t index = 0; index < render::kContrailRenderAssetCount; ++index) {"
engine_diagnostics = '''        for (std::size_t engineIndex = 0; engineIndex < engineExhaustBodyOffsets_.size(); ++engineIndex) {
            const auto& offset = engineExhaustBodyOffsets_[engineIndex];
            stream << "engine_exhaust_body_offset_" << engineIndex << "_x_m=" << offset.x << '\n';
            stream << "engine_exhaust_body_offset_" << engineIndex << "_y_m=" << offset.y << '\n';
            stream << "engine_exhaust_body_offset_" << engineIndex << "_z_m=" << offset.z << '\n';
        }

''' + asset_loop
p = replace_once(p, asset_loop, engine_diagnostics, "engine exhaust report diagnostics")

p = replace_once(
    p,
    "FFAtmo World Contrail Visual Debug Report v6.2 NATIVE_3D_FILLED_FIELD",
    "FFAtmo World Contrail Visual Debug Report v6.3 NATIVE_3D_MORPHOLOGY_FIELD",
    "report identity",
)
p = p.replace("3D CLOUD V6.2 READY", "3D CLOUD V6.3 READY")
p = p.replace("LOADING V6.2 CLOUD FIELD", "LOADING V6.3 MORPHOLOGY FIELD")
p = p.replace("v6 point 2", "v6 point 3")
p = p.replace("v6.2", "v6.3")
p = p.replace("V6.2", "V6.3")
p = p.replace(
    "renders a multi-scale native 3-D filled cloudlet field.",
    "renders a soft/dense native 3-D morphology field with bounded near-field onset.",
)
p = p.replace(
    "native 3-D filled cloud field renderer.",
    "native 3-D morphology field renderer.",
)

for token, label in (
    ("visual_head_spatial_onset_target_m", "spatial onset report"),
    ("engine_exhaust_body_offset_", "ACF exhaust diagnostics"),
    ("NATIVE_3D_MORPHOLOGY_FIELD", "v6.3 report identity"),
    ("3D CLOUD V6.3 READY", "v6.3 overlay identity"),
):
    if token not in p:
        raise RuntimeError(f"v6.3 runtime missing {label}")
PLUGIN.write_text(p, encoding="utf-8", newline="\n")

print("Integrated Renderer Foundation v6.3 morphology + bounded onset alignment")
