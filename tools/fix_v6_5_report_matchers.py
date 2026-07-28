#!/usr/bin/env python3
"""Repair literal C++ newline matching in the v6.5 integration patcher.

Python triple-quoted strings turn ``\\n`` into a newline unless explicitly
escaped. This pre-pass rewrites the one remaining fragile report insertion to a
structural anchor before the actual v6.5 patch is executed.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "tools" / "patch_v6_5_handoff_vortex.py"
text = PATCH.read_text(encoding="utf-8")

start_marker = 'p = once(\n    p,\n    \'\'\'            stream << "engine_exhaust_body_offset_"'
end_marker = '    "per-engine handoff report",\n)\n\np = p.replace('
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError("v6.5 per-engine report matcher block was not found")

replacement = r'''# Structural line anchor avoids Python interpreting C++ '\\n' escapes.
engine_z_anchor = '            stream << "engine_exhaust_body_offset_" << engineIndex << "_z_m=" << offset.z << \'\\n\';'
engine_z_insert = engine_z_anchor + (
    '\n            stream << "synthetic_head_actual_distance_" << engineIndex << "_m="\n'
    '                   << latestSyntheticHeadActualDistanceM_[engineIndex] << \'\\n\';\n'
    '            stream << "synthetic_head_bridge_length_" << engineIndex << "_m="\n'
    '                   << latestSyntheticHeadBridgeLengthM_[engineIndex] << \'\\n\';'
)
p = once(p, engine_z_anchor, engine_z_insert, "per-engine handoff report")

p = p.replace('''

text = text[:start] + replacement + text[end + len(end_marker):]
PATCH.write_text(text, encoding="utf-8", newline="\n")
print("Stabilized v6.5 report newline matchers")
