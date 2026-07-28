#!/usr/bin/env python3
"""Integrate Renderer Foundation v6.2 native 3-D filled cloud field."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"
text = PLUGIN.read_text(encoding="utf-8")

text = text.replace(
    '#include "ContrailCloudletRenderer.h"',
    '#include "ContrailCloudletRendererV62.h"',
)
text = text.replace(
    "FFAtmo World Contrail Visual Debug Report v6.1 NATIVE_3D_CLOUDLETS",
    "FFAtmo World Contrail Visual Debug Report v6.2 NATIVE_3D_FILLED_FIELD",
)
text = text.replace("3D CLOUD V6.1 READY", "3D CLOUD V6.2 READY")
text = text.replace("LOADING V6.1 CLOUDLETS", "LOADING V6.2 CLOUD FIELD")
text = text.replace("v6 point 1", "v6 point 2")
text = text.replace("v6.1", "v6.2")
text = text.replace("V6.1", "V6.2")
text = text.replace(
    "renders native camera-independent 3-D cloudlet volumes.",
    "renders a multi-scale native 3-D filled cloudlet field.",
)
text = text.replace(
    "native 3-D cloudlet renderer.",
    "native 3-D filled cloud field renderer.",
)

if '#include "ContrailCloudletRendererV62.h"' not in text:
    raise RuntimeError("v6.2 cloud field renderer include was not integrated")
if "ContrailCloudletRenderer worldRenderer_;" not in text:
    raise RuntimeError("v6.2 world renderer member is missing")
if "3D CLOUD V6.2 READY" not in text:
    raise RuntimeError("v6.2 overlay status was not integrated")
if "NATIVE_3D_FILLED_FIELD" not in text:
    raise RuntimeError("v6.2 report identity was not integrated")

PLUGIN.write_text(text, encoding="utf-8", newline="\n")
print("Integrated Renderer Foundation v6.2 native 3-D filled cloud field")
