#!/usr/bin/env python3
"""Apply the split deterministic v6.3 renderer and runtime integrations."""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[1]
# Split integration keeps renderer morphology and runtime onset diagnostics independently testable.
# Revision 2: runtime report insertion now preserves literal C++ newline escapes.
runpy.run_path(str(ROOT / "tools" / "patch_v6_3_renderer_morphology.py"), run_name="__main__")
runpy.run_path(str(ROOT / "tools" / "patch_v6_3_runtime_onset.py"), run_name="__main__")
print("Integrated Renderer Foundation v6.3 via split morphology/onset patches")
