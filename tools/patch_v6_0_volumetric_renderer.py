#!/usr/bin/env python3
"""Integrate Renderer Foundation v6.0 into the live contrail debug plugin."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

text = PLUGIN.read_text(encoding="utf-8")
text = text.replace('#include "ContrailParticleRenderer.h"', '#include "ContrailVolumeRenderer.h"')
text = text.replace('ContrailParticleRenderer::', 'ContrailVolumeRenderer::')
text = text.replace('ContrailParticleRenderer worldRenderer_;', 'ContrailVolumeRenderer worldRenderer_;')

for old in ("v5.6", "v5.5.2", "v5.5.1", "v5.5", "v5.4.1", "v5.4", "v5.3", "v5.2"):
    text = text.replace(old, "v6.0")
for old in ("V5.6", "V5.5.2", "V5.5.1", "V5.5", "V5.4.1", "V5.4", "V5.3", "V5.2"):
    text = text.replace(old, "V6.0")
text = text.replace("v5 point 6", "v6 point 0")
text = text.replace("v5 point 5 point 2", "v6 point 0")
text = text.replace("v5 point 5 point 1", "v6 point 0")
text = text.replace("v5 point 5", "v6 point 0")
text = text.replace("v5 point 4 point 1", "v6 point 0")
text = text.replace("v5 point 4", "v6 point 0")
text = text.replace("v5 point 3", "v6 point 0")
text = text.replace("v5 point 2", "v6 point 0")

text = text.replace(
    '"Started. Renderer Foundation v6.0 uses atmosphere-conditioned "\n'
    '            "cooling and nucleation with continuity-first trail planning.\\n");',
    '"Started. Renderer Foundation v6.0 keeps the wake-fluid simulation and "\n'
    '            "renders procedural three-dimensional ice density in Modern3D.\\n");',
)
text = text.replace(
    '"Could not start Renderer Foundation v6.0. Check the native particle assets folder.\\n");',
    '"Could not start Renderer Foundation v6.0 Modern3D volumetric renderer.\\n");',
)
text = text.replace(
    'status.rendererStatus = worldRenderer_.ready() ?\n'
    '            "WORLD V6.0 READY" : "LOADING V6.0 ASSETS";',
    'status.rendererStatus = worldRenderer_.ready() ?\n'
    '            "VOLUME V6.0 ARMED" : "VOLUME V6.0 OFFLINE";',
)

# The renderer no longer owns particle assets; start() keeps the same signature
# so the stable plugin lifecycle does not need special cases.
text = text.replace(
    'worldRenderer_.start(pluginRoot_ / "assets")',
    'worldRenderer_.start(pluginRoot_ / "assets")',
)

# Report terminology is preserved for parser compatibility but version and
# capacity are now supplied by ContrailVolumeRenderer.
text = text.replace(
    'FFAtmo World Contrail Visual Debug Report v6.0',
    'FFAtmo World Contrail Visual Debug Report v6.0 VOLUMETRIC',
)

if '#include "ContrailVolumeRenderer.h"' not in text:
    raise RuntimeError("Volume renderer include was not integrated")
if 'ContrailVolumeRenderer worldRenderer_;' not in text:
    raise RuntimeError("Volume renderer member was not integrated")
if 'VOLUME V6.0 ARMED' not in text:
    raise RuntimeError("Volumetric overlay label was not integrated")

PLUGIN.write_text(text, encoding="utf-8", newline="\n")
print("Integrated Renderer Foundation v6.0 volumetric contrails")
