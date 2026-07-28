#!/usr/bin/env python3
"""Patch v6.0 into v6.0.1: force Modern3D BEFORE dispatch and expose GPU diagnostics."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VOLUME = ROOT / "src" / "ContrailVolumeRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

volume = VOLUME.read_text(encoding="utf-8")
plugin = PLUGIN.read_text(encoding="utf-8")

# Switch both registration and unregistration to the BEFORE callback.
count = volume.count("xplm_Phase_Modern3D, 0, this")
if count != 2:
    raise RuntimeError(f"Expected two Modern3D AFTER registrations, found {count}")
volume = volume.replace("xplm_Phase_Modern3D, 0, this", "xplm_Phase_Modern3D, 1, this")

# Public diagnostic accessors.
marker = "    std::size_t loadedObjectCount() const { return gpuReady_ ? 1u : 0u; }\n"
if volume.count(marker) != 1:
    raise RuntimeError("Renderer getter insertion marker is not unique")
volume = volume.replace(
    marker,
    marker
    + "    bool gpuReady() const { return gpuReady_; }\n"
    + "    std::uint64_t drawCallbackInvocationCount() const { return drawCallbackInvocationCount_; }\n"
    + "    std::uint64_t regularDrawPassCount() const { return regularDrawPassCount_; }\n"
    + "    std::uint64_t gpuInitAttemptCount() const { return gpuInitAttemptCount_; }\n"
    + "    std::uint64_t gpuInitFailureCount() const { return gpuInitFailureCount_; }\n"
    + "    int lastWorldRenderType() const { return lastWorldRenderType_; }\n",
    1,
)

# Count every attempt to initialise the shader bridge.
gpu_marker = "        if (gpuReady_) return true;\n        if (!loadGlFunctions()) {\n"
if volume.count(gpu_marker) != 1:
    raise RuntimeError("GPU init structural marker was not found uniquely")
volume = volume.replace(
    gpu_marker,
    "        if (gpuReady_) return true;\n"
    "        ++gpuInitAttemptCount_;\n"
    "        if (!loadGlFunctions()) {\n"
    "            ++gpuInitFailureCount_;\n",
    1,
)

# Instrument callback entry and remove the silent render_type != 0 gate.
draw_marker = (
    "    int draw() {\n"
    "        if (!enabled_ || !running_ || cells_.empty()) return 1;\n"
    "        if (XPLMGetDatai(worldRenderTypeRef_) != 0) return 1;\n"
    "        if (!ensureGpuReady()) return 1;\n"
)
if volume.count(draw_marker) != 1:
    raise RuntimeError("Renderer draw structural marker was not found uniquely")
draw_replacement = (
    "    int draw() {\n"
    "        ++drawCallbackInvocationCount_;\n"
    "        lastWorldRenderType_ = worldRenderTypeRef_ ? XPLMGetDatai(worldRenderTypeRef_) : -999;\n"
    "        if (!firstDrawCallbackLogged_) {\n"
    "            firstDrawCallbackLogged_ = true;\n"
    "            log(\"Renderer v6.0.1 Modern3D BEFORE callback entered; render_type=\" +\n"
    "                std::to_string(lastWorldRenderType_) + \".\\n\");\n"
    "        }\n"
    "        if (!enabled_ || !running_ || cells_.empty()) return 1;\n"
    "        if (lastWorldRenderType_ == 1 || lastWorldRenderType_ == 3 || lastWorldRenderType_ == 6) return 1;\n"
    "        ++regularDrawPassCount_;\n"
    "        if (!ensureGpuReady()) return 1;\n"
)
volume = volume.replace(draw_marker, draw_replacement, 1)

member_marker = "    bool drawCallbackRegistered_ = false;\n"
if volume.count(member_marker) != 1:
    raise RuntimeError("Renderer member insertion marker is not unique")
volume = volume.replace(
    member_marker,
    member_marker
    + "    std::uint64_t drawCallbackInvocationCount_ = 0;\n"
    + "    std::uint64_t regularDrawPassCount_ = 0;\n"
    + "    std::uint64_t gpuInitAttemptCount_ = 0;\n"
    + "    std::uint64_t gpuInitFailureCount_ = 0;\n"
    + "    int lastWorldRenderType_ = -999;\n"
    + "    bool firstDrawCallbackLogged_ = false;\n",
    1,
)

status_pattern = re.compile(
    r'\s*status\.rendererStatus = worldRenderer_\.ready\(\) \?\s*\n'
    r'\s*"VOLUME V6\.0 ARMED" : "VOLUME V6\.0 OFFLINE";'
)
status_replacement = '''
        if (!worldRenderer_.ready()) {
            status.rendererStatus = "VOLUME V6.0.1 OFFLINE";
        } else if (worldRenderer_.gpuReady()) {
            status.rendererStatus = "VOLUME V6.0.1 GPU READY";
        } else if (worldRenderer_.drawCallbackInvocationCount() > 0) {
            status.rendererStatus = "VOLUME V6.0.1 GPU INIT";
        } else {
            status.rendererStatus = "VOLUME V6.0.1 WAITING DRAW";
        }'''
plugin, replaced = status_pattern.subn(status_replacement, plugin, count=1)
if replaced != 1:
    raise RuntimeError("v6.0 overlay status assignment was not found")

report_marker = "               << \"world_renderer_loaded_objects=\" << worldRenderer_.loadedObjectCount() << '\\n'\n"
if plugin.count(report_marker) != 1:
    raise RuntimeError("Report renderer insertion marker is not unique")
plugin = plugin.replace(
    report_marker,
    report_marker
    + "               << \"world_renderer_gpu_ready=\" << (worldRenderer_.gpuReady() ? 1 : 0) << '\\n'\n"
    + "               << \"world_renderer_draw_callback_count=\" << worldRenderer_.drawCallbackInvocationCount() << '\\n'\n"
    + "               << \"world_renderer_regular_pass_count=\" << worldRenderer_.regularDrawPassCount() << '\\n'\n"
    + "               << \"world_renderer_gpu_init_attempt_count=\" << worldRenderer_.gpuInitAttemptCount() << '\\n'\n"
    + "               << \"world_renderer_gpu_init_failure_count=\" << worldRenderer_.gpuInitFailureCount() << '\\n'\n"
    + "               << \"world_renderer_last_world_render_type=\" << worldRenderer_.lastWorldRenderType() << '\\n'\n",
    1,
)

# Version text last, but do not rewrite the v6.0.1 diagnostic strings inserted above.
volume = re.sub(r"v6\.0(?!\.1)", "v6.0.1", volume)
volume = re.sub(r"V6\.0(?!\.1)", "V6.0.1", volume)
plugin = re.sub(r"v6\.0(?!\.1)", "v6.0.1", plugin)
plugin = re.sub(r"V6\.0(?!\.1)", "V6.0.1", plugin)
plugin = plugin.replace("v6 point 0", "v6 point 0 point 1")

if volume.count("xplm_Phase_Modern3D, 1, this") != 2:
    raise RuntimeError("Modern3D BEFORE registration/unregistration validation failed")
if "drawCallbackInvocationCount_" not in volume or "regularDrawPassCount_" not in volume:
    raise RuntimeError("Modern3D callback diagnostics are missing")
if "VOLUME V6.0.1 WAITING DRAW" not in plugin:
    raise RuntimeError("v6.0.1 overlay diagnostics are missing")
if "world_renderer_draw_callback_count" not in plugin:
    raise RuntimeError("v6.0.1 report diagnostics are missing")
if "V6.0.1.1" in volume or "V6.0.1.1" in plugin or "v6.0.1.1" in volume or "v6.0.1.1" in plugin:
    raise RuntimeError("v6.0.1 version was rewritten twice")

VOLUME.write_text(volume, encoding="utf-8", newline="\n")
PLUGIN.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v6.0.1 Modern3D dispatch fix and diagnostics")
