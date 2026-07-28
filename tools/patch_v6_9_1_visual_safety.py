#!/usr/bin/env python3
"""Apply the v6.9.1 visual-safety hotfix after the v6.9 simulator patch.

Physics and marker advection are intentionally untouched. This patch only reduces
visual overdraw from the secondary lane and corrects report/runtime identity so a
v6.9.1 test cannot be confused with the rejected v6.9-R0 build.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailVortexSheetRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

renderer = RENDERER.read_text(encoding="utf-8")
old = "            selectLaneCoherently(candidates[lane], kInstancesPerAsset, selection[lane]);"
new = """            // v6.9.1 visual safety: secondary wake is deliberately sparser.\n            // Main inner/core/outer lanes keep the full continuity budget.\n            const std::size_t laneBudget =\n                lane == static_cast<std::size_t>(engine::WakeSheetLane::Secondary)\n                    ? 128u\n                    : kInstancesPerAsset;\n            selectLaneCoherently(candidates[lane], laneBudget, selection[lane]);"""
if renderer.count(old) != 1:
    raise RuntimeError(f"v6.9.1 lane-budget anchor count={renderer.count(old)}")
renderer = renderer.replace(old, new, 1)
renderer = renderer.replace(
    "Renderer v6.9 elongated ice-volume assets loaded.",
    "Renderer v6.9.1 curvature-safe ice-streak assets loaded.",
)
RENDERER.write_text(renderer, encoding="utf-8", newline="\n")

plugin = PLUGIN.read_text(encoding="utf-8")
plugin = plugin.replace(
    "FFAtmo World Contrail Visual Debug Report v6.8 LAGRANGIAN_VORTEX_SHEET_XPLM_INSTANCE",
    "FFAtmo World Contrail Visual Debug Report v6.9.1 LAGRANGIAN_VORTEX_SHEET_CURVATURE_SAFE",
)
plugin = plugin.replace(
    "vortex_sampling_mode=PRIMARY_MASS_PLUS_EDGE_ARMS",
    "vortex_sampling_mode=LAGRANGIAN_MARKER_LANES",
)
plugin = plugin.replace(
    "wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE",
    "wake_visual_model=LAGRANGIAN_INNER_CORE_OUTER_SECONDARY",
)
plugin = plugin.replace("Renderer Foundation v6.9", "Renderer Foundation v6.9.1")
plugin = plugin.replace("Renderer v6.9", "Renderer v6.9.1")
PLUGIN.write_text(plugin, encoding="utf-8", newline="\n")

for token in (
    "laneBudget",
    "Secondary)\n                    ? 128u",
    "v6.9.1 curvature-safe ice-streak assets loaded",
):
    if token not in RENDERER.read_text(encoding="utf-8"):
        raise RuntimeError(f"missing v6.9.1 renderer token: {token}")

print("Applied v6.9.1 curvature-safe visual renderer hotfix")
