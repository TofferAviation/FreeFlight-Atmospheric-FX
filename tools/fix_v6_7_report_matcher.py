#!/usr/bin/env python3
"""Make the v6.7 report-mode patch insensitive to neighbouring report formatting."""
from pathlib import Path

path = Path("tools/patch_v6_7_physics_coupled_rollup.py")
text = path.read_text(encoding="utf-8")
old = '''p = once(
    p,
    ''' + "'''" + '''               << "renderer_selection_mode=STABLE_HASH" << '\\n'
               << "vortex_sampling_mode=SPARSE_SUBSCALE" << '\\n' ''' + "'''" + ''',
    ''' + "'''" + '''               << "renderer_selection_mode=STABLE_HASH" << '\\n'
               << "vortex_sampling_mode=PHYSICS_COUPLED_ARMS" << '\\n'
               << "wake_visual_model=PRIMARY_VORTEX_PLUS_SECONDARY_WAKE" << '\\n' ''' + "'''" + ''',
    "v6.7 report identity modes",
)'''
new = '''p = once(
    p,
    "               << \\\"vortex_sampling_mode=SPARSE_SUBSCALE\\\" << '\\\\n'",
    "               << \\\"vortex_sampling_mode=PHYSICS_COUPLED_ARMS\\\" << '\\\\n'\\n"
    "               << \\\"wake_visual_model=PRIMARY_VORTEX_PLUS_SECONDARY_WAKE\\\" << '\\\\n'",
    "v6.7 report identity modes",
)'''
if old not in text:
    raise RuntimeError("v6.7 two-line report matcher block not found")
text = text.replace(old, new, 1)
path.write_text(text, encoding="utf-8", newline="\n")
print("Stabilized v6.7 report mode matcher")
