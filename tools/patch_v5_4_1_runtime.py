#!/usr/bin/env python3
"""Apply v5.4.1 runtime labels and report diagnostics deterministically."""

from pathlib import Path

path = Path("src/ContrailDebugPlugin.cpp")
text = path.read_text(encoding="utf-8")
text = text.replace("v5.2", "v5.4.1")
text = text.replace("V5.2", "V5.4.1")
text = text.replace("v5 point 2", "v5 point 4 point 1")

old_counts = '''               << "world_renderer_pool_capacity_drop_count="
               << worldRenderer_.poolCapacityDropCount() << '\\n'
               << "simulation_enabled="'''
new_counts = '''               << "world_renderer_pool_capacity_drop_count="
               << worldRenderer_.poolCapacityDropCount() << '\\n'
               << "world_renderer_ownership_reuse_count="
               << worldRenderer_.ownershipReuseCount() << '\\n'
               << "world_renderer_ownership_new_binding_count="
               << worldRenderer_.ownershipNewBindingCount() << '\\n'
               << "world_renderer_ownership_release_count="
               << worldRenderer_.ownershipReleaseCount() << '\\n'
               << "world_renderer_swirl_candidate_count="
               << worldRenderer_.swirlCandidateCount() << '\\n'
               << "simulation_enabled="'''
if old_counts not in text:
    raise RuntimeError("Renderer ownership report insertion point was not found")
text = text.replace(old_counts, new_counts, 1)

old_motion = '''               << "maximum_length_compression_ratio="
               << worldRenderer_.maximumLengthCompressionRatio() << '\\n'
               << std::hex << std::setfill('0')'''
new_motion = '''               << "maximum_length_compression_ratio="
               << worldRenderer_.maximumLengthCompressionRatio() << '\\n'
               << "world_renderer_maximum_wake_turn_deg="
               << worldRenderer_.maximumWakeTurnDeg() << '\\n'
               << "world_renderer_maximum_wake_descent_m="
               << worldRenderer_.maximumWakeDescentM() << '\\n'
               << std::hex << std::setfill('0')'''
if old_motion not in text:
    raise RuntimeError("Swirl diagnostics report insertion point was not found")
text = text.replace(old_motion, new_motion, 1)

path.write_text(text, encoding="utf-8", newline="\n")
