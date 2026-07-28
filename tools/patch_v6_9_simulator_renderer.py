#!/usr/bin/env python3
"""Wire the validated v6.9 Lagrangian vortex sheet into the X-Plane renderer.

Run after the stable renderer patch chain through v6.8 and after
patch_v6_9_vortex_sheet_runtime.py. This deliberately bypasses the v6.8
primary/fill/swirl candidate field and submits the actual material-marker lanes
through the supported XPLMInstance path.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def rx_once(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, replacement, text, count=1, flags=re.S | re.M)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one regex match, found {count}")
    return out


text = PLUGIN.read_text(encoding="utf-8")

# Replace the old blob/cloudlet renderer include with the v6.9 lane renderer.
text, include_count = re.subn(
    r'#include "ContrailCloudletRenderer(?:V62)?\.h"',
    '#include "ContrailVortexSheetRenderer.h"',
    text,
    count=1,
)
if include_count != 1:
    raise RuntimeError(f"v6.9 renderer include: expected one match, found {include_count}")

text = replace_once(
    text,
    "    ContrailCloudletRenderer worldRenderer_;",
    "    ContrailVortexSheetRenderer worldRenderer_;",
    "v6.9 renderer member",
)

# The render planner remains alive for near-field/legacy diagnostics, but the
# actual native 3-D instances now consume the final solver-produced marker field.
text = replace_once(
    text,
    "        worldRenderer_.update(latestRenderPlan_.samples);",
    '''        const auto vortexSheetField =
            render::buildVortexSheetRenderField(liveEngine_.parcels());
        const engine::Vec3d vortexWorldOrigin {
            simulationNormalized.worldEastM,
            simulationNormalized.worldUpM,
            simulationNormalized.worldNorthM
        };
        worldRenderer_.update(
            vortexSheetField,
            simulationSnapshot.localPositionM,
            vortexWorldOrigin);''',
    "v6.9 live marker-field submission",
)

# Make the visible identity unambiguous in-sim and in the exported report.
for old, new in (
    ("Renderer Foundation v6.8", "Renderer Foundation v6.9"),
    ("Renderer v6.8", "Renderer v6.9"),
    ("3D CLOUD V6.8 VISIBLE ROLLUP READY", "V6.9 LAGRANGIAN VORTEX SHEET READY"),
    ("LOADING V6.8 CLOUD FIELD", "LOADING V6.9 VORTEX SHEET"),
    ("VISIBLE_PRIMARY_VORTEX_ROLLUP", "LAGRANGIAN_VORTEX_SHEET_XPLM_INSTANCE"),
):
    text = text.replace(old, new)

# Some older identity strings can survive the incremental patch chain.
text = text.replace("visible primary-vortex roll-up 3-D field", "Lagrangian vortex-sheet 3-D field")
text = text.replace("visible wake-rollup ice-white 3-D morphology assets", "elongated low-occupancy vortex-sheet ice volumes")

# Add rendered-lane diagnostics to the text report without making the report a
# build dependency. The legacy fields remain so older comparison scripts work.
report_anchor = '''               << "maximum_length_compression_ratio="
               << worldRenderer_.maximumLengthCompressionRatio() << '\\n'
'''
if report_anchor in text:
    report_extra = report_anchor + '''               << "v69_rendered_inner_lane="
               << worldRenderer_.renderedPerLane()[0] << '\\n'
               << "v69_rendered_core_lane="
               << worldRenderer_.renderedPerLane()[1] << '\\n'
               << "v69_rendered_outer_lane="
               << worldRenderer_.renderedPerLane()[2] << '\\n'
               << "v69_rendered_secondary_lane="
               << worldRenderer_.renderedPerLane()[3] << '\\n'
               << "v69_maximum_lane_gap_m="
               << worldRenderer_.maximumLaneGapM() << '\\n'
               << "v69_maximum_lane_curvature_deg="
               << worldRenderer_.maximumLaneCurvatureDeg() << '\\n'
'''
    text = text.replace(report_anchor, report_extra, 1)

# The v6.8 gain command may remain in the menu. It is intentionally harmless in
# v6.9: the compatibility method returns 1.0 and does not deform physical data.
text = text.replace(
    "Cycle Vortex Visibility: Authentic / Strong / Showcase",
    "V6.9 Physical Vortex Sheet (visual gain disabled)",
)

required = (
    '#include "ContrailVortexSheetRenderer.h"',
    "ContrailVortexSheetRenderer worldRenderer_;",
    "buildVortexSheetRenderField(liveEngine_.parcels())",
    "vortexWorldOrigin",
)
for token in required:
    if token not in text:
        raise RuntimeError(f"v6.9 simulator integration missing token: {token}")

if "worldRenderer_.update(latestRenderPlan_.samples);" in text:
    raise RuntimeError("legacy centreline render-plan submission survived v6.9 patch")

PLUGIN.write_text(text, encoding="utf-8", newline="\n")
print("Integrated Renderer Foundation v6.9 Lagrangian Vortex Sheet into X-Plane simulator path")
