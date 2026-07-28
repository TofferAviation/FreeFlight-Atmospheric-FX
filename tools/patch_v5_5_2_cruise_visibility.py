#!/usr/bin/env python3
"""Apply Renderer Foundation v5.5.2 cruise visibility and size-range fixes."""
from pathlib import Path
import re

renderer_path = Path("src/ContrailParticleRenderer.h")
plugin_path = Path("src/ContrailDebugPlugin.cpp")

renderer = renderer_path.read_text(encoding="utf-8")
plugin = plugin_path.read_text(encoding="utf-8")

renderer = renderer.replace("v5.5.1", "v5.5.2").replace("V5.5.1", "V5.5.2")
plugin = plugin.replace("v5.5.1", "v5.5.2").replace("V5.5.1", "V5.5.2")
plugin = plugin.replace("v5 point 5 point 1", "v5 point 5 point 2")

new_primary = '''                // v5.5.2 keeps the physical-parcel layout but restores
                 // cruise-scale optical mass. Near-field puffs remain narrow;
                 // developed wake puffs grow enough to survive distant views and
                 // X-Plane texture downscaling without becoming spacing-driven blobs.
                 primary.sizeM = std::clamp(
                     anchor.widthM * (anchor.nearField ? 1.24f : 1.62f) *
                         sizeVariation,
                     1.10f,
                     18.0f);
                 primary.alpha = std::clamp(
                     (0.165f + std::sqrt(anchor.opacityStrength) * 0.68f) *
                         densityVariation * ageFade *
                         (anchor.nearField ? 0.90f : 1.0f),
                     0.125f,
                     0.78f);'''
primary_pattern = re.compile(
    r"(?:                // v5\.5 grouped parcels correctly.*?\n)?"
    r"\s*primary\.sizeM = std::clamp\(.*?"
    r"\s*primary\.alpha = std::clamp\(.*?"
    r"\s*0\.55f\);",
    re.S,
)
renderer, primary_count = primary_pattern.subn(lambda _: new_primary, renderer, count=1)
if primary_count != 1:
    raise RuntimeError("v5.5.1 primary cruise calibration block was not found")

new_companion = '''                    companion.sizeM = std::clamp(
                         anchor.widthM *
                             (0.86f + 0.18f * unitHash(
                                 companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                         0.85f,
                         12.0f);
                     companion.alpha = std::clamp(
                         (0.070f + std::sqrt(anchor.opacityStrength) * 0.34f) *
                             (0.84f + 0.16f * unitHash(
                                 companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                         0.050f,
                         0.32f);'''
companion_pattern = re.compile(
    r"\s*companion\.sizeM = std::clamp\(.*?"
    r"\s*companion\.alpha = std::clamp\(.*?"
    r"\s*0\.22f\);",
    re.S,
)
renderer, companion_count = companion_pattern.subn(
    lambda _: new_companion, renderer, count=1
)
if companion_count != 1:
    raise RuntimeError("v5.5.1 companion cruise calibration block was not found")

new_mapping = '''        // Match the v5.5.2 particle asset's 0.75-20.0 metre size range.
         // The previous 0.30-12.0 mapping silently clamped larger cruise clouds.
         const float normalizedSize = std::clamp(
             (cloud.sizeM - 0.75f) / 19.25f,
             0.0f,
             1.0f);'''
mapping_pattern = re.compile(
    r"\s*const float normalizedSize = std::clamp\(\s*"
    r"\(cloud\.sizeM - 0\.30f\) / 11\.70f,\s*"
    r"0\.0f,\s*1\.0f\);"
)
renderer, mapping_count = mapping_pattern.subn(lambda _: new_mapping, renderer, count=1)
if mapping_count != 1:
    raise RuntimeError("v5.5.1 normalized size mapping was not found")

renderer_path.write_text(renderer, encoding="utf-8", newline="\n")
plugin_path.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v5.5.2 cruise visibility calibration")
