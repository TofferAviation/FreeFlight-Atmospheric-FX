#!/usr/bin/env python3
"""Integrate v6.5 streamline-locked handoff and controlled vortex roll-up."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"


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


# Runtime handoff: lock the synthetic start directly onto the live line from
# each exhaust to that engine's youngest actually-renderable physical parcel.
p = PLUGIN.read_text(encoding="utf-8")

p = once(p, "constexpr float kHeatBlurHandoffSeconds = 0.08f;",
         "constexpr float kHeatBlurHandoffSeconds = 0.04f;", "short live visual handoff")

p = once(
    p,
    "        std::array<const engine::ContrailParcel*, engine::kMaximumRecordedEngines> newest {};",
    '''        std::array<const engine::ContrailParcel*, engine::kMaximumRecordedEngines> newest {};
        std::array<engine::Vec3d, engine::kMaximumRecordedEngines> youngestRenderableLocal {};
        std::array<float, engine::kMaximumRecordedEngines> youngestRenderableAge {};
        std::array<bool, engine::kMaximumRecordedEngines> youngestRenderableValid {};
        youngestRenderableAge.fill(std::numeric_limits<float>::infinity());''',
    "youngest renderable bridge state",
)

p = once(
    p,
    '''            const auto* currentNewest = newest[parcel.engineIndex];
            if (!currentNewest || parcel.ageSeconds < currentNewest->ageSeconds) {
                newest[parcel.engineIndex] = &parcel;
            }''',
    '''            const auto* currentNewest = newest[parcel.engineIndex];
            if (!currentNewest || parcel.ageSeconds < currentNewest->ageSeconds) {
                newest[parcel.engineIndex] = &parcel;
            }
            if (item.opticalDepth >= renderPlannerSettings_.minimumOpticalDepth &&
                parcel.ageSeconds < youngestRenderableAge[parcel.engineIndex]) {
                youngestRenderableAge[parcel.engineIndex] = parcel.ageSeconds;
                youngestRenderableLocal[parcel.engineIndex] = item.localPositionM;
                youngestRenderableValid[parcel.engineIndex] = true;
            }''',
    "youngest renderable sample tracking",
)

p = rx(
    p,
    r'''            const float thermodynamicStartSeconds = std::clamp\(\s*handoff\.visibleStartSeconds,\s*0\.05f,\s*0\.45f\);\s*const double vx = static_cast<double>\(snapshot\.linearVelocityLocalMps\.x\);\s*const double vy = static_cast<double>\(snapshot\.linearVelocityLocalMps\.y\);\s*const double vz = static_cast<double>\(snapshot\.linearVelocityLocalMps\.z\);\s*const double localSpeedMps = std::sqrt\(vx \* vx \+ vy \* vy \+ vz \* vz\);\s*const double effectiveSpeedMps = std::max\(\s*localSpeedMps,\s*static_cast<double>\(snapshot\.trueAirspeedMps\)\);\s*const double spatialOnsetM = std::clamp\(\s*effectiveSpeedMps \* static_cast<double>\(kHeatBlurHandoffSeconds\),\s*18\.0,\s*28\.0\);\s*const double spatialOnsetSeconds = localSpeedMps > 1\.0\s*\? spatialOnsetM / localSpeedMps\s*: static_cast<double>\(kHeatBlurHandoffSeconds\);\s*latestSpatialOnsetTargetM_ = spatialOnsetM;\s*\s*render::ContrailRenderInput head;\s*head\.sourceParcelId = kSyntheticHeadIdBase \+ engineIndex;\s*head\.engineIndex = static_cast<std::uint32_t>\(engineIndex\);\s*head\.localPositionM = \{\s*exhausts\[engineIndex\]\.x - vx \* spatialOnsetSeconds,\s*exhausts\[engineIndex\]\.y - vy \* spatialOnsetSeconds,\s*exhausts\[engineIndex\]\.z - vz \* spatialOnsetSeconds\s*\};\s*head\.physicsRadiusM = 0\.15f;\s*head\.opticalDepth = std::max\(parcel->opticalDepth \* 0\.026f, 0\.0035f\);\s*head\.normalizedIceMass = std::max\(parcel->normalizedIceMass \* 0\.026f, 0\.0035f\);\s*head\.ageSeconds = static_cast<float>\(std::min\(\s*static_cast<double>\(thermodynamicStartSeconds\), spatialOnsetSeconds\)\);''',
    '''            const float thermodynamicStartSeconds = std::clamp(
                handoff.visibleStartSeconds,
                0.05f,
                0.45f);
            const double vx = static_cast<double>(snapshot.linearVelocityLocalMps.x);
            const double vy = static_cast<double>(snapshot.linearVelocityLocalMps.y);
            const double vz = static_cast<double>(snapshot.linearVelocityLocalMps.z);
            const double localSpeedMps = std::sqrt(vx * vx + vy * vy + vz * vz);
            const double effectiveSpeedMps = std::max(localSpeedMps, static_cast<double>(snapshot.trueAirspeedMps));
            const double requestedOnsetM = std::clamp(
                effectiveSpeedMps * static_cast<double>(kHeatBlurHandoffSeconds), 8.0, 14.0);

            engine::Vec3d bridge {};
            double bridgeLengthM = 0.0;
            if (youngestRenderableValid[engineIndex]) {
                bridge = {
                    youngestRenderableLocal[engineIndex].x - exhausts[engineIndex].x,
                    youngestRenderableLocal[engineIndex].y - exhausts[engineIndex].y,
                    youngestRenderableLocal[engineIndex].z - exhausts[engineIndex].z
                };
                bridgeLengthM = std::sqrt(bridge.x * bridge.x + bridge.y * bridge.y + bridge.z * bridge.z);
            }
            if (!(bridgeLengthM > 1.0) || !std::isfinite(bridgeLengthM)) {
                bridge = {-vx, -vy, -vz};
                bridgeLengthM = std::sqrt(bridge.x * bridge.x + bridge.y * bridge.y + bridge.z * bridge.z);
            }
            if (!(bridgeLengthM > 1.0) || !std::isfinite(bridgeLengthM)) {
                bridge = {0.0, 0.0, -1.0};
                bridgeLengthM = 1.0;
            }
            const double invBridge = 1.0 / bridgeLengthM;
            bridge.x *= invBridge; bridge.y *= invBridge; bridge.z *= invBridge;

            const double spatialOnsetM = youngestRenderableValid[engineIndex]
                ? std::min(requestedOnsetM, std::max(3.0, bridgeLengthM * 0.45))
                : requestedOnsetM;
            const double spatialOnsetSeconds = effectiveSpeedMps > 1.0
                ? spatialOnsetM / effectiveSpeedMps
                : static_cast<double>(kHeatBlurHandoffSeconds);
            latestSpatialOnsetTargetM_ = spatialOnsetM;
            latestSyntheticHeadActualDistanceM_[engineIndex] = spatialOnsetM;
            latestSyntheticHeadBridgeLengthM_[engineIndex] = bridgeLengthM;

            render::ContrailRenderInput head;
            head.sourceParcelId = kSyntheticHeadIdBase + engineIndex;
            head.engineIndex = static_cast<std::uint32_t>(engineIndex);
            head.localPositionM = {
                exhausts[engineIndex].x + bridge.x * spatialOnsetM,
                exhausts[engineIndex].y + bridge.y * spatialOnsetM,
                exhausts[engineIndex].z + bridge.z * spatialOnsetM
            };
            head.physicsRadiusM = 0.13f;
            head.opticalDepth = std::max(parcel->opticalDepth * 0.035f, 0.0070f);
            head.normalizedIceMass = std::max(parcel->normalizedIceMass * 0.035f, 0.0070f);
            head.ageSeconds = static_cast<float>(std::min(
                static_cast<double>(thermodynamicStartSeconds), spatialOnsetSeconds));''',
    "streamline locked visual head",
)

p = once(
    p,
    "        latestSpatialOnsetTargetM_ = 0.0;\n        latestCondensationStartSeconds_ = 0.0f;",
    '''        latestSpatialOnsetTargetM_ = 0.0;
        latestSyntheticHeadActualDistanceM_.fill(0.0);
        latestSyntheticHeadBridgeLengthM_.fill(0.0);
        latestCondensationStartSeconds_ = 0.0f;''',
    "handoff diagnostic reset",
)
p = once(
    p,
    "    double latestSpatialOnsetTargetM_ = 0.0;\n    float latestCondensationStartSeconds_ = 0.0f;",
    '''    double latestSpatialOnsetTargetM_ = 0.0;
    std::array<double, engine::kMaximumRecordedEngines> latestSyntheticHeadActualDistanceM_ {};
    std::array<double, engine::kMaximumRecordedEngines> latestSyntheticHeadBridgeLengthM_ {};
    float latestCondensationStartSeconds_ = 0.0f;''',
    "handoff diagnostic members",
)

# Match the single value output rather than the whole multi-line report block;
# earlier patch stages may change surrounding indentation/continuation text.
p = once(
    p,
    "               << latestSpatialOnsetTargetM_ << '\\n'",
    "               << latestSpatialOnsetTargetM_ << '\\n'\n               << \"render_material_mode=ALPHA_TEST_CUTOUT\" << '\\n'",
    "material mode report",
)

p = once(
    p,
    '''            stream << "engine_exhaust_body_offset_" << engineIndex << "_z_m=" << offset.z << '\n';''',
    '''            stream << "engine_exhaust_body_offset_" << engineIndex << "_z_m=" << offset.z << '\n';
            stream << "synthetic_head_actual_distance_" << engineIndex << "_m="
                   << latestSyntheticHeadActualDistanceM_[engineIndex] << '\n';
            stream << "synthetic_head_bridge_length_" << engineIndex << "_m="
                   << latestSyntheticHeadBridgeLengthM_[engineIndex] << '\n';''',
    "per-engine handoff report",
)

p = p.replace("FFAtmo World Contrail Visual Debug Report v6.4 NATIVE_3D_CONTINUOUS_SHELL",
              "FFAtmo World Contrail Visual Debug Report v6.5 ICE_WHITE_VORTEX_FIELD")
p = p.replace("3D CLOUD V6.4 READY", "3D CLOUD V6.5 VORTEX READY")
p = p.replace("LOADING V6.4 CONTINUOUS SHELL FIELD", "LOADING V6.5 ICE VORTEX FIELD")
p = p.replace("v6 point 4", "v6 point 5")
p = p.replace("v6.4", "v6.5").replace("V6.4", "V6.5")
p = p.replace(
    "renders porous continuous-shell 3-D cloudlets with an explicit nozzle-relative head endpoint.",
    "renders alpha-tested ice-white 3-D cloudlets with a streamline-locked live handoff and controlled vortex roll-up.",
)
for token in (
    "kHeatBlurHandoffSeconds = 0.04f", "youngestRenderableLocal", "requestedOnsetM",
    "latestSyntheticHeadActualDistanceM_", "render_material_mode=ALPHA_TEST_CUTOUT",
    "ICE_WHITE_VORTEX_FIELD", "3D CLOUD V6.5 VORTEX READY",
):
    if token not in p:
        raise RuntimeError(f"v6.5 runtime handoff integration missing: {token}")
PLUGIN.write_text(p, encoding="utf-8", newline="\n")


# Renderer: bounded cross-sectional wake roll-up. Primaries remain on the
# physical centreline; companions counter-rotate and then diffuse.
r = RENDERER.read_text(encoding="utf-8")
r = once(
    r,
    "        swirlCloudletCount_ = 0;\n        deferredNonEmptyFrameCount_ = 0;",
    '''        swirlCloudletCount_ = 0;
        maximumSwirlRadiusM_ = 0.0;
        swirlRadiusSumM_ = 0.0;
        swirlRadiusSampleCount_ = 0;
        deferredNonEmptyFrameCount_ = 0;''',
    "initial swirl diagnostics reset",
)
r = once(
    r,
    "        swirlCloudletCount_ = 0;\n        for (auto& row : usedThisFrame_) row.fill(false);",
    '''        swirlCloudletCount_ = 0;
        maximumSwirlRadiusM_ = 0.0;
        swirlRadiusSumM_ = 0.0;
        swirlRadiusSampleCount_ = 0;
        for (auto& row : usedThisFrame_) row.fill(false);''',
    "per-frame swirl diagnostics reset",
)
r = once(
    r,
    "    std::size_t swirlCloudletCount() const { return swirlCloudletCount_; }\n    double maximumWakeTurnDeg() const { return maximumWakeTurnDeg_; }",
    '''    std::size_t swirlCloudletCount() const { return swirlCloudletCount_; }
    double maximumSwirlRadiusM() const { return maximumSwirlRadiusM_; }
    double meanSwirlRadiusM() const {
        return swirlRadiusSampleCount_ > 0
            ? swirlRadiusSumM_ / static_cast<double>(swirlRadiusSampleCount_)
            : 0.0;
    }
    double maximumWakeTurnDeg() const { return maximumWakeTurnDeg_; }''',
    "swirl diagnostic getters",
)

r = rx(
    r,
    r'''            // A companion volume orbits inside the local wake cross-section\..*?                candidates\[swirl\.assetIndex\]\[static_cast<std::size_t>\(Layer::Swirl\)\]\.push_back\(swirl\);\s*            \}''',
    '''            // v6.5 organized vortex roll-up. The physical primary stays on
            // the wake centreline. A secondary cloud mass counter-rotates inside
            // each engine wake, grows smoothly after 3 s, then loses coherence
            // after ~22 s so the trail never becomes a rigid helix.
            if (sample.ageSeconds >= 3.0f && sample.ageSeconds <= 30.0f &&
                ((mix64(sample.renderId ^ 0x77ULL) % 3ULL) != 0ULL)) {
                ++swirlCandidateCount_;
                CloudNode swirl;
                swirl.cloudId = swirlId(sample);
                swirl.engineIndex = engineIndex;
                swirl.ageSeconds = sample.ageSeconds;
                swirl.layer = Layer::Swirl;
                swirl.tangent = sample.trailTangentLocal;
                normalize(swirl.tangent);
                swirl.opacityStrength = sample.opacityStrength * 0.52f;
                swirl.assetIndex = assetForCloud(swirl.ageSeconds, swirl.opacityStrength, swirl.cloudId);

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

                const float develop = smoothstep(3.0f, 12.0f, sample.ageSeconds);
                const float diffuse = 1.0f - 0.62f * smoothstep(22.0f, 30.0f, sample.ageSeconds);
                const double widthBound = std::clamp(static_cast<double>(sample.widthM) * 0.46, 0.32, 2.65);
                const double stableVariation = 0.84 +
                    0.24 * static_cast<double>(unitHash(sample.renderId ^ 0x6d31ULL));
                const double radius = widthBound * (0.12 + 0.88 * static_cast<double>(develop)) *
                    static_cast<double>(diffuse) * stableVariation;
                const double direction = engineIndex == 0 ? -1.0 : 1.0;
                const double phaseSeed =
                    (static_cast<double>(unitHash(sample.renderId ^ 0xa55aULL)) - 0.5) * 0.34;
                const double phase = direction * static_cast<double>(sample.ageSeconds - 3.0f) * 0.31 + phaseSeed;
                const double lateralRadius = radius;
                const double verticalRadius = radius * 0.72;
                swirl.position = {
                    sample.localPositionM.x + side.x * std::cos(phase) * lateralRadius +
                        up.x * std::sin(phase) * verticalRadius,
                    sample.localPositionM.y + side.y * std::cos(phase) * lateralRadius +
                        up.y * std::sin(phase) * verticalRadius,
                    sample.localPositionM.z + side.z * std::cos(phase) * lateralRadius +
                        up.z * std::sin(phase) * verticalRadius
                };
                maximumSwirlRadiusM_ = std::max(maximumSwirlRadiusM_, radius);
                swirlRadiusSumM_ += radius;
                ++swirlRadiusSampleCount_;
                candidates[swirl.assetIndex][static_cast<std::size_t>(Layer::Swirl)].push_back(swirl);
            }''',
    "organized vortex roll-up",
)
r = once(
    r,
    "    std::size_t swirlCloudletCount_ = 0;\n    double maximumWakeTurnDeg_ = 0.0;",
    '''    std::size_t swirlCloudletCount_ = 0;
    double maximumSwirlRadiusM_ = 0.0;
    double swirlRadiusSumM_ = 0.0;
    std::size_t swirlRadiusSampleCount_ = 0;
    double maximumWakeTurnDeg_ = 0.0;''',
    "swirl diagnostic members",
)
r = r.replace("Renderer Foundation v6.4", "Renderer Foundation v6.5")
r = r.replace("Renderer v6.4", "Renderer v6.5")
r = r.replace("continuous-shell native 3-D morphology field", "ice-white cutout vortex 3-D field")
r = r.replace("porous single-surface 3-D OBJ morphology assets", "alpha-tested ice-white 3-D morphology assets")
for token in (
    "sample.ageSeconds >= 3.0f", "widthBound", "maximumSwirlRadiusM_",
    "meanSwirlRadiusM()", "sample.opacityStrength * 0.52f",
):
    if token not in r:
        raise RuntimeError(f"v6.5 vortex integration missing: {token}")
RENDERER.write_text(r, encoding="utf-8", newline="\n")
print("Integrated Renderer Foundation v6.5 streamline handoff + vortex roll-up")
