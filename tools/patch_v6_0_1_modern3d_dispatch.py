#!/usr/bin/env python3
"""Patch v6.0 into v6.0.1: force the Modern3D before callback and expose GPU dispatch diagnostics."""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VOLUME = ROOT / "src" / "ContrailVolumeRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

volume = VOLUME.read_text(encoding="utf-8")
plugin = PLUGIN.read_text(encoding="utf-8")

# Version labels.
volume = volume.replace("v6.0", "v6.0.1").replace("V6.0", "V6.0.1")
plugin = plugin.replace("v6.0", "v6.0.1").replace("V6.0", "V6.0.1")
plugin = plugin.replace("v6 point 0", "v6 point 0 point 1")

# Modern3D on XP11/12 is conceptually the old before-airplanes insertion point.
# Register the before callback explicitly rather than the after callback used by v6.0.
old_register = "XPLMRegisterDrawCallback(drawCallback, xplm_Phase_Modern3D, 0, this)"
new_register = "XPLMRegisterDrawCallback(drawCallback, xplm_Phase_Modern3D, 1, this)"
if old_register not in volume:
    raise RuntimeError("v6.0 Modern3D registration was not found")
volume = volume.replace(old_register, new_register, 1)

old_unregister = "XPLMUnregisterDrawCallback(drawCallback, xplm_Phase_Modern3D, 0, this)"
new_unregister = "XPLMUnregisterDrawCallback(drawCallback, xplm_Phase_Modern3D, 1, this)"
if old_unregister not in volume:
    raise RuntimeError("v6.0 Modern3D unregistration was not found")
volume = volume.replace(old_unregister, new_unregister, 1)

# Public diagnostic getters.
getter_marker = "    std::size_t loadedObjectCount() const { return gpuReady_ ? 1u : 0u; }\n"
getters = getter_marker + (
    "    bool gpuReady() const { return gpuReady_; }\n"
    "    std::uint64_t drawCallbackInvocationCount() const { return drawCallbackInvocationCount_; }\n"
    "    std::uint64_t regularDrawPassCount() const { return regularDrawPassCount_; }\n"
    "    std::uint64_t gpuInitAttemptCount() const { return gpuInitAttemptCount_; }\n"
    "    std::uint64_t gpuInitFailureCount() const { return gpuInitFailureCount_; }\n"
    "    int lastWorldRenderType() const { return lastWorldRenderType_; }\n"
)
if getter_marker not in volume:
    raise RuntimeError("v6.0 renderer getter marker was not found")
volume = volume.replace(getter_marker, getters, 1)

# Count shader initialisation attempts and make failed attempts explicit.
old_gpu = """    bool ensureGpuReady() {
        if (gpuReady_) return true;
        if (!loadGlFunctions()) {
            log("Renderer v6.0.1 could not resolve required OpenGL shader entry points.\n");
            return false;
        }
"""
new_gpu = """    bool ensureGpuReady() {
        if (gpuReady_) return true;
        ++gpuInitAttemptCount_;
        if (!loadGlFunctions()) {
            ++gpuInitFailureCount_;
            if (gpuInitFailureCount_ <= 3) {
                log("Renderer v6.0.1 could not resolve required OpenGL shader entry points.\n");
            }
            return false;
        }
"""
if old_gpu not in volume:
    raise RuntimeError("v6.0 GPU initialisation block was not found")
volume = volume.replace(old_gpu, new_gpu, 1)

# Replace silent draw-return path with observable Modern3D dispatch. Only the
# documented reflection/shadow/cubemap passes are skipped. Unknown values are
# allowed through once and logged so XP12 changes cannot silently disable us.
old_draw = """    int draw() {
        if (!enabled_ || !running_ || cells_.empty()) return 1;
        if (XPLMGetDatai(worldRenderTypeRef_) != 0) return 1;
        if (!ensureGpuReady()) return 1;

        XPLMCameraPosition_t camera {};
"""
new_draw = """    int draw() {
        ++drawCallbackInvocationCount_;
        lastWorldRenderType_ = worldRenderTypeRef_ ? XPLMGetDatai(worldRenderTypeRef_) : -999;
        if (!firstDrawCallbackLogged_) {
            firstDrawCallbackLogged_ = true;
            log("Renderer v6.0.1 Modern3D BEFORE callback entered; render_type=" +
                std::to_string(lastWorldRenderType_) + ".\n");
        }
        if (!enabled_ || !running_ || cells_.empty()) return 1;
        if (lastWorldRenderType_ == 1 || lastWorldRenderType_ == 3 || lastWorldRenderType_ == 6) {
            return 1;
        }
        ++regularDrawPassCount_;
        if (!ensureGpuReady()) return 1;

        XPLMCameraPosition_t camera {};
"""
if old_draw not in volume:
    raise RuntimeError("v6.0 draw entry block was not found")
volume = volume.replace(old_draw, new_draw, 1)

# Reset diagnostics only on renderer stop/start, not each simulation frame.
member_marker = "    bool drawCallbackRegistered_ = false;\n"
members = member_marker + (
    "    std::uint64_t drawCallbackInvocationCount_ = 0;\n"
    "    std::uint64_t regularDrawPassCount_ = 0;\n"
    "    std::uint64_t gpuInitAttemptCount_ = 0;\n"
    "    std::uint64_t gpuInitFailureCount_ = 0;\n"
    "    int lastWorldRenderType_ = -999;\n"
    "    bool firstDrawCallbackLogged_ = false;\n"
)
if member_marker not in volume:
    raise RuntimeError("v6.0 renderer member marker was not found")
volume = volume.replace(member_marker, members, 1)

# Make overlay distinguish callback dispatch from actual GPU readiness.
old_status = '''        status.rendererStatus = worldRenderer_.ready() ?
            "VOLUME V6.0.1 ARMED" : "VOLUME V6.0.1 OFFLINE";'''
new_status = '''        if (!worldRenderer_.ready()) {
            status.rendererStatus = "VOLUME V6.0.1 OFFLINE";
        } else if (worldRenderer_.gpuReady()) {
            status.rendererStatus = "VOLUME V6.0.1 GPU READY";
        } else if (worldRenderer_.drawCallbackInvocationCount() > 0) {
            status.rendererStatus = "VOLUME V6.0.1 GPU INIT";
        } else {
            status.rendererStatus = "VOLUME V6.0.1 WAITING DRAW";
        }'''
if old_status not in plugin:
    raise RuntimeError("v6.0 overlay status block was not found")
plugin = plugin.replace(old_status, new_status, 1)

# Export the new callback/GPU diagnostics in the existing report.
report_marker = '''               << "world_renderer_loaded_objects=" << worldRenderer_.loadedObjectCount() << '\\n'
'''
report_extra = report_marker + '''               << "world_renderer_gpu_ready=" << (worldRenderer_.gpuReady() ? 1 : 0) << '\\n'
               << "world_renderer_draw_callback_count=" << worldRenderer_.drawCallbackInvocationCount() << '\\n'
               << "world_renderer_regular_pass_count=" << worldRenderer_.regularDrawPassCount() << '\\n'
               << "world_renderer_gpu_init_attempt_count=" << worldRenderer_.gpuInitAttemptCount() << '\\n'
               << "world_renderer_gpu_init_failure_count=" << worldRenderer_.gpuInitFailureCount() << '\\n'
               << "world_renderer_last_world_render_type=" << worldRenderer_.lastWorldRenderType() << '\\n'
'''
if report_marker not in plugin:
    raise RuntimeError("v6.0 report renderer marker was not found")
plugin = plugin.replace(report_marker, report_extra, 1)

# Validation.
if "xplm_Phase_Modern3D, 1, this" not in volume:
    raise RuntimeError("Modern3D BEFORE callback was not enabled")
if "drawCallbackInvocationCount_" not in volume:
    raise RuntimeError("Modern3D callback diagnostics missing")
if "VOLUME V6.0.1 WAITING DRAW" not in plugin:
    raise RuntimeError("v6.0.1 overlay diagnostics missing")
if "world_renderer_draw_callback_count" not in plugin:
    raise RuntimeError("v6.0.1 report diagnostics missing")

VOLUME.write_text(volume, encoding="utf-8", newline="\n")
PLUGIN.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v6.0.1 Modern3D dispatch fix and diagnostics")
