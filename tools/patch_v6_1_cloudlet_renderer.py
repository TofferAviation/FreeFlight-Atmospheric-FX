#!/usr/bin/env python3
"""Integrate Renderer Foundation v6.1 native 3-D cloudlet rendering."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"
text = PLUGIN.read_text(encoding="utf-8")

text = text.replace('#include "ContrailParticleRenderer.h"', '#include "ContrailCloudletRenderer.h"')
text = text.replace('ContrailParticleRenderer::', 'ContrailCloudletRenderer::')
text = text.replace('ContrailParticleRenderer worldRenderer_;', 'ContrailCloudletRenderer worldRenderer_;')

for old in ("v5.6", "v5.5.2", "v5.5.1", "v5.5", "v5.4.1", "v5.4", "v5.3", "v5.2"):
    text = text.replace(old, "v6.1")
for old in ("V5.6", "V5.5.2", "V5.5.1", "V5.5", "V5.4.1", "V5.4", "V5.3", "V5.2"):
    text = text.replace(old, "V6.1")
for old in ("v5 point 6", "v5 point 5 point 2", "v5 point 5 point 1", "v5 point 5", "v5 point 4 point 1", "v5 point 4", "v5 point 3", "v5 point 2"):
    text = text.replace(old, "v6 point 1")

text = text.replace(
    '"Started. Renderer Foundation v6.1 uses atmosphere-conditioned "\n'
    '            "cooling and nucleation with continuity-first trail planning.\\n");',
    '"Started. Renderer Foundation v6.1 keeps the wake-fluid simulation and "\n'
    '            "renders native camera-independent 3-D cloudlet volumes.\\n");'
)
text = text.replace(
    '"Could not start Renderer Foundation v6.1. Check the native particle assets folder.\\n");',
    '"Could not start Renderer Foundation v6.1 native 3-D cloudlet renderer.\\n");'
)
text = text.replace(
    'status.rendererStatus = worldRenderer_.ready() ?\n'
    '            "WORLD V6.1 READY" : "LOADING V6.1 ASSETS";',
    'status.rendererStatus = worldRenderer_.ready() ?\n'
    '            "3D CLOUD V6.1 READY" : "LOADING V6.1 CLOUDLETS";'
)
text = text.replace(
    'FFAtmo World Contrail Visual Debug Report v6.1',
    'FFAtmo World Contrail Visual Debug Report v6.1 NATIVE_3D_CLOUDLETS'
)

if '#include "ContrailCloudletRenderer.h"' not in text:
    raise RuntimeError("v6.1 cloudlet renderer include was not integrated")
if 'ContrailCloudletRenderer worldRenderer_;' not in text:
    raise RuntimeError("v6.1 cloudlet renderer member was not integrated")
if '3D CLOUD V6.1 READY' not in text:
    raise RuntimeError("v6.1 overlay status was not integrated")

PLUGIN.write_text(text, encoding="utf-8", newline="\n")
print("Integrated Renderer Foundation v6.1 native 3-D cloudlet renderer")
