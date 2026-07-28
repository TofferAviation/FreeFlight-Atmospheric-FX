#!/usr/bin/env python3
"""Integrate only the v6.3 runtime onset-alignment and diagnostics changes."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def rx(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(
        pattern,
        lambda _match: replacement,
        text,
        count=1,
        flags=re.MULTILINE | re.DOTALL,
    )
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")
    return out


p = PLUGIN.read_text(encoding="utf-8")
p = once(
    p,
    "constexpr float kHeatBlurHandoffSeconds = 0.12f;",
    "constexpr float kHeatBlurHandoffSeconds = 0.08f;",
    "spatial handoff constant",
)

p = rx(
    p,
    r'''const float handoffSeconds = std::clamp\(\s*handoff\.visibleStartSeconds,\s*0\.05f,\s*0\.45f\);\s*render::ContrailRenderInput head;\s*head\.sourceParcelId = kSyntheticHeadIdBase \+ engineIndex;\s*head\.engineIndex = static_cast<std::uint32_t>\(engineIndex\);\s*head\.localPositionM = \{\s*exhausts\[engineIndex\]\.x -\s*static_cast<double>\(snapshot\.linearVelocityLocalMps\.x\) \* handoffSeconds,\s*exhausts\[engineIndex\]\.y -\s*static_cast<double>\(snapshot\.linearVelocityLocalMps\.y\) \* handoffSeconds,\s*exhausts\[engineIndex\]\.z -\s*static_cast<double>\(snapshot\.linearVelocityLocalMps\.z\) \* handoffSeconds\s*\};\s*head\.physicsRadiusM = 0\.18f;\s*head\.opticalDepth = std::max\(parcel->opticalDepth \* 0\.035f, 0\.005f\);\s*head\.normalizedIceMass = std::max\(parcel->normalizedIceMass \* 0\.035f, 0\.005f\);\s*head\.ageSeconds = handoffSeconds;''',
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
    "bounded nozzle-relative onset",
)

p = once(
    p,
    "        maximumExhaustToFirstVisibleM_ = 0.0;\n        latestCondensationStartSeconds_ = 0.0f;",
    "        maximumExhaustToFirstVisibleM_ = 0.0;\n        latestSpatialOnsetTargetM_ = 0.0;\n        latestCondensationStartSeconds_ = 0.0f;",
    "diagnostic reset",
)
p = once(
    p,
    "    double maximumExhaustToFirstVisibleM_ = 0.0;\n    float latestCondensationStartSeconds_ = 0.0f;",
    "    double maximumExhaustToFirstVisibleM_ = 0.0;\n    double latestSpatialOnsetTargetM_ = 0.0;\n    float latestCondensationStartSeconds_ = 0.0f;",
    "diagnostic member",
)

report_anchor = "               << latestCondensationFullSeconds_ << '\\n'\n"
report_insert = (
    report_anchor
    + "               << \"visual_head_spatial_onset_target_m=\"\n"
    + "               << latestSpatialOnsetTargetM_ << '\\n'\n"
)
p = once(p, report_anchor, report_insert, "spatial onset report field")

asset_loop = "        for (std::size_t index = 0; index < render::kContrailRenderAssetCount; ++index) {"
engine_diag = (
    "        for (std::size_t engineIndex = 0; engineIndex < engineExhaustBodyOffsets_.size(); ++engineIndex) {\n"
    "            const auto& offset = engineExhaustBodyOffsets_[engineIndex];\n"
    "            stream << \"engine_exhaust_body_offset_\" << engineIndex << \"_x_m=\" << offset.x << '\\n';\n"
    "            stream << \"engine_exhaust_body_offset_\" << engineIndex << \"_y_m=\" << offset.y << '\\n';\n"
    "            stream << \"engine_exhaust_body_offset_\" << engineIndex << \"_z_m=\" << offset.z << '\\n';\n"
    "        }\n\n"
    + asset_loop
)
p = once(p, asset_loop, engine_diag, "ACF exhaust diagnostics")

p = once(
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

for token in (
    "kHeatBlurHandoffSeconds = 0.08f",
    "spatialOnsetM",
    "visual_head_spatial_onset_target_m",
    "engine_exhaust_body_offset_",
    "NATIVE_3D_MORPHOLOGY_FIELD",
    "3D CLOUD V6.3 READY",
):
    if token not in p:
        raise RuntimeError(f"v6.3 onset integration missing: {token}")

PLUGIN.write_text(p, encoding="utf-8", newline="\n")
print("Integrated v6.3 bounded onset alignment and nozzle diagnostics")
