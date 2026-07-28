#!/usr/bin/env python3
"""Integrate v6.3 soft morphology and repair the near-field onset offset."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"

# -----------------------------------------------------------------------------
# Renderer: keep the supported XPLMInstance boundary, but split each age class
# into soft/dense morphology variants. Total capacity remains exactly 1024.
# -----------------------------------------------------------------------------
r = RENDERER.read_text(encoding="utf-8")
r = r.replace("static constexpr std::size_t kAssetCount = 4;", "static constexpr std::size_t kAssetCount = 8;")
r = r.replace("static constexpr std::size_t kInstancesPerAsset = 256;", "static constexpr std::size_t kInstancesPerAsset = 128;")
r = r.replace("static constexpr std::size_t kInstanceCreationBatchPerAsset = 24;", "static constexpr std::size_t kInstanceCreationBatchPerAsset = 16;")

old_paths = '''        objectPaths_ = {
            assetDirectory_ / "contrail_cloudlet_near.obj",
            assetDirectory_ / "contrail_cloudlet_young.obj",
            assetDirectory_ / "contrail_cloudlet_mature.obj",
            assetDirectory_ / "contrail_cloudlet_old.obj"
        };'''
new_paths = '''        objectPaths_ = {
            assetDirectory_ / "contrail_cloudlet_near_soft.obj",
            assetDirectory_ / "contrail_cloudlet_near_dense.obj",
            assetDirectory_ / "contrail_cloudlet_young_soft.obj",
            assetDirectory_ / "contrail_cloudlet_young_dense.obj",
            assetDirectory_ / "contrail_cloudlet_mature_soft.obj",
            assetDirectory_ / "contrail_cloudlet_mature_dense.obj",
            assetDirectory_ / "contrail_cloudlet_old_soft.obj",
            assetDirectory_ / "contrail_cloudlet_old_dense.obj"
        };'''
if old_paths not in r:
    raise RuntimeError("v6.2 object path block not found")
r = r.replace(old_paths, new_paths)

old_age = '''    static std::size_t assetForAge(float ageSeconds) {
        if (ageSeconds < 3.0f) return 0;
        if (ageSeconds < 10.0f) return 1;
        if (ageSeconds < 27.0f) return 2;
        return 3;
    }'''
new_age = '''    static std::size_t ageClassForAge(float ageSeconds) {
        if (ageSeconds < 2.5f) return 0;
        if (ageSeconds < 9.0f) return 1;
        if (ageSeconds < 25.0f) return 2;
        return 3;
    }

    static std::size_t assetForCloud(float ageSeconds,
                                     float opacityStrength,
                                     std::uint64_t stableId) {
        const std::size_t ageClass = ageClassForAge(ageSeconds);
        // Per-cloud threshold avoids a synchronized hard band where every body
        // changes density on exactly the same frame. A very faint synthetic
        // near-field head therefore stays on the soft asset automatically.
        const float threshold = 0.22f +
            0.18f * unitHash(stableId ^ 0x63a7d31f92c4b815ULL);
        const bool dense = opacityStrength >= threshold;
        return ageClass * 2 + (dense ? 1u : 0u);
    }'''
if old_age not in r:
    raise RuntimeError("v6.2 age-class selector not found")
r = r.replace(old_age, new_age)

normalize_marker = '''    static void normalize(engine::Vec3d& v) {
        const double m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (std::isfinite(m) && m > 1.0e-7) {
            v.x /= m; v.y /= m; v.z /= m;
        } else {
            v = {0.0, 0.0, -1.0};
        }
    }
'''
packing_helper = normalize_marker + '''
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
    }
'''
if normalize_marker not in r:
    raise RuntimeError("v6.2 normalize helper not found")
r = r.replace(normalize_marker, packing_helper, 1)

r = r.replace(
    '''            const auto& sample = *stream[i];
            const std::size_t asset = assetForAge(sample.ageSeconds);''',
    '''            const auto& sample = *stream[i];
            const std::uint64_t primaryCloudId = primaryId(sample);
            const std::size_t asset = assetForCloud(
                sample.ageSeconds, sample.opacityStrength, primaryCloudId);'''
)
r = r.replace("                node.cloudId = primaryId(sample);", "                node.cloudId = primaryCloudId;")
r = r.replace(
    '''                node.tangent = sample.trailTangentLocal;
                normalize(node.tangent);
                node.ageSeconds = sample.ageSeconds;''',
    '''                node.tangent = sample.trailTangentLocal;
                normalize(node.tangent);
                const double packingRadius = 0.08 +
                    0.34 * static_cast<double>(smoothstep(1.5f, 20.0f, sample.ageSeconds));
                applyPackingOffset(node, node.cloudId, packingRadius);
                node.ageSeconds = sample.ageSeconds;'''
)

old_fill = '''                    fill.cloudId = fillId(sample, next);
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
                    candidates[fill.assetIndex][static_cast<std::size_t>(Layer::Fill)].push_back(fill);'''
new_fill = '''                    fill.cloudId = fillId(sample, next);
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
                    applyPackingOffset(fill, fill.cloudId ^ 0x44ULL, fillPackingRadius);
                    candidates[fill.assetIndex][static_cast<std::size_t>(Layer::Fill)].push_back(fill);'''
if old_fill not in r:
    raise RuntimeError("v6.2 fill block not found")
r = r.replace(old_fill, new_fill)
r = r.replace("gap >= 3.2 && gap <= 14.0", "gap >= 2.2 && gap <= 16.0")

old_swirl = '''                swirl.cloudId = swirlId(sample);
                swirl.engineIndex = engineIndex;
                swirl.ageSeconds = sample.ageSeconds;
                swirl.assetIndex = assetForAge(sample.ageSeconds);
                swirl.layer = Layer::Swirl;
                swirl.tangent = sample.trailTangentLocal;
                normalize(swirl.tangent);
                swirl.opacityStrength = sample.opacityStrength * 0.72f;'''
new_swirl = '''                swirl.cloudId = swirlId(sample);
                swirl.engineIndex = engineIndex;
                swirl.ageSeconds = sample.ageSeconds;
                swirl.layer = Layer::Swirl;
                swirl.tangent = sample.trailTangentLocal;
                normalize(swirl.tangent);
                swirl.opacityStrength = sample.opacityStrength * 0.64f;
                swirl.assetIndex = assetForCloud(
                    swirl.ageSeconds, swirl.opacityStrength, swirl.cloudId);'''
if old_swirl not in r:
    raise RuntimeError("v6.2 swirl block not found")
r = r.replace(old_swirl, new_swirl)

r = r.replace(
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.58);\n"
    "        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.25);",
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.50);\n"
    "        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.34);"
)
r = r.replace("reached 256 persistent 3-D instances", "reached 128 persistent 3-D instances")
r = r.replace("Renderer Foundation v6.2", "Renderer Foundation v6.3")
r = r.replace("Renderer v6.2", "Renderer v6.3")
r = r.replace("four native 3-D cloudlet classes", "eight native 3-D morphology assets")
r = r.replace("four-class native 3-D cloud field", "eight-asset soft/dense native 3-D morphology field")
r = r.replace("Four closed irregular 3-D OBJ size classes are", "Eight soft/dense irregular 3-D OBJ morphology assets are")

if "kAssetCount = 8" not in r or "kInstancesPerAsset = 128" not in r:
    raise RuntimeError("v6.3 8x128 pool split missing")
if "assetForCloud" not in r or "applyPackingOffset" not in r:
    raise RuntimeError("v6.3 morphology selection/packing missing")
if "contrail_cloudlet_old_dense.obj" not in r:
    raise RuntimeError("v6.3 morphology asset paths missing")
RENDERER.write_text(r, encoding="utf-8", newline="\n")

# -----------------------------------------------------------------------------
# Runtime: decouple thermodynamic nucleation timing from the visual head's
# spatial distance. The v6.2 report measured ~61.7 m because the old rule used
# aircraft velocity * 0.22 s directly. v6.3 keeps the thermodynamic handoff but
# places the faint synthetic head at a short, bounded nozzle-relative distance.
# -----------------------------------------------------------------------------
p = PLUGIN.read_text(encoding="utf-8")
p = p.replace("constexpr float kHeatBlurHandoffSeconds = 0.12f;", "constexpr float kHeatBlurHandoffSeconds = 0.08f;")

old_head = '''            const float handoffSeconds = std::clamp(
                handoff.visibleStartSeconds,
                0.05f,
                0.45f);

            render::ContrailRenderInput head;
            head.sourceParcelId = kSyntheticHeadIdBase + engineIndex;
            head.engineIndex = static_cast<std::uint32_t>(engineIndex);
            head.localPositionM = {
                exhausts[engineIndex].x -
                    static_cast<double>(snapshot.linearVelocityLocalMps.x) * handoffSeconds,
                exhausts[engineIndex].y -
                    static_cast<double>(snapshot.linearVelocityLocalMps.y) * handoffSeconds,
                exhausts[engineIndex].z -
                    static_cast<double>(snapshot.linearVelocityLocalMps.z) * handoffSeconds
            };
            head.physicsRadiusM = 0.18f;
            head.opticalDepth = std::max(parcel->opticalDepth * 0.035f, 0.005f);
            head.normalizedIceMass = std::max(parcel->normalizedIceMass * 0.035f, 0.005f);
            head.ageSeconds = handoffSeconds;'''
new_head = '''            const float thermodynamicStartSeconds = std::clamp(
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
                static_cast<double>(thermodynamicStartSeconds), spatialOnsetSeconds));'''
if old_head not in p:
    raise RuntimeError("near-field handoff block not found")
p = p.replace(old_head, new_head)

p = p.replace(
    "        maximumExhaustToFirstVisibleM_ = 0.0;\n        latestCondensationStartSeconds_ = 0.0f;",
    "        maximumExhaustToFirstVisibleM_ = 0.0;\n        latestSpatialOnsetTargetM_ = 0.0;\n        latestCondensationStartSeconds_ = 0.0f;"
)
p = p.replace(
    "    double maximumExhaustToFirstVisibleM_ = 0.0;\n    float latestCondensationStartSeconds_ = 0.0f;",
    "    double maximumExhaustToFirstVisibleM_ = 0.0;\n    double latestSpatialOnsetTargetM_ = 0.0;\n    float latestCondensationStartSeconds_ = 0.0f;"
)

report_marker = '''               << "current_condensation_full_seconds="
               << latestCondensationFullSeconds_ << '\\n'
               << "geometry_status=" << geometryStatus_ << '\\n' '''
report_replacement = '''               << "current_condensation_full_seconds="
               << latestCondensationFullSeconds_ << '\\n'
               << "visual_head_spatial_onset_target_m="
               << latestSpatialOnsetTargetM_ << '\\n'
               << "geometry_status=" << geometryStatus_ << '\\n' '''
if report_marker not in p:
    raise RuntimeError("report condensation marker not found")
p = p.replace(report_marker, report_replacement)

asset_loop_marker = '''        for (std::size_t index = 0; index < render::kContrailRenderAssetCount; ++index) {'''
engine_diag = '''        for (std::size_t engineIndex = 0; engineIndex < engineExhaustBodyOffsets_.size(); ++engineIndex) {
            const auto& offset = engineExhaustBodyOffsets_[engineIndex];
            stream << "engine_exhaust_body_offset_" << engineIndex << "_x_m=" << offset.x << '\\n';
            stream << "engine_exhaust_body_offset_" << engineIndex << "_y_m=" << offset.y << '\\n';
            stream << "engine_exhaust_body_offset_" << engineIndex << "_z_m=" << offset.z << '\\n';
        }

''' + asset_loop_marker
if asset_loop_marker not in p:
    raise RuntimeError("report asset loop marker not found")
p = p.replace(asset_loop_marker, engine_diag, 1)

p = p.replace(
    "FFAtmo World Contrail Visual Debug Report v6.2 NATIVE_3D_FILLED_FIELD",
    "FFAtmo World Contrail Visual Debug Report v6.3 NATIVE_3D_MORPHOLOGY_FIELD",
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

if "visual_head_spatial_onset_target_m" not in p:
    raise RuntimeError("v6.3 onset diagnostic missing")
if "engine_exhaust_body_offset_" not in p:
    raise RuntimeError("v6.3 exhaust offset diagnostics missing")
if "NATIVE_3D_MORPHOLOGY_FIELD" not in p or "3D CLOUD V6.3 READY" not in p:
    raise RuntimeError("v6.3 runtime identity missing")
PLUGIN.write_text(p, encoding="utf-8", newline="\n")
print("Integrated Renderer Foundation v6.3 soft morphology + bounded onset alignment")
