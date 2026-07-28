#!/usr/bin/env python3
"""Make v6.8 report diagnostics insertion insensitive to Python newline quoting."""
from pathlib import Path

path = Path(__file__).with_name("patch_v6_8_visible_primary_rollup.py")
text = path.read_text(encoding="utf-8")
old = '''p = once(
    p,
    ''' + "'''" + '''               << "wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE" << '\\n' ''' + "'''" + ''',
    ''' + "'''" + '''               << "wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE" << '\\n'
               << "vortex_visual_gain=" << worldRenderer_.vortexVisualGain() << '\\n'
               << "world_renderer_primary_rollup_cloudlet_count="
               << worldRenderer_.primaryRollupCloudletCount() << '\\n'
               << "world_renderer_maximum_primary_rollup_offset_m="
               << worldRenderer_.maximumPrimaryRollupOffsetM() << '\\n' ''' + "'''" + ''',
    "v6.8 report rollup diagnostics",
)'''
new = '''p = once(
    p,
    "               << \\\"wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE\\\" << '\\\\n'",
    "               << \\\"wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE\\\" << '\\\\n'\\n"
    "               << \\\"vortex_visual_gain=\\\" << worldRenderer_.vortexVisualGain() << '\\\\n'\\n"
    "               << \\\"world_renderer_primary_rollup_cloudlet_count=\\\"\\n"
    "               << worldRenderer_.primaryRollupCloudletCount() << '\\\\n'\\n"
    "               << \\\"world_renderer_maximum_primary_rollup_offset_m=\\\"\\n"
    "               << worldRenderer_.maximumPrimaryRollupOffsetM() << '\\\\n'",
    "v6.8 report rollup diagnostics",
)'''
if old not in text:
    raise RuntimeError("v6.8 multiline report matcher block not found")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8", newline="\n")
print("Stabilized v6.8 report diagnostics matcher")
