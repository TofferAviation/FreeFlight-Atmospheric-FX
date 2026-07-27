#!/usr/bin/env python3
"""Apply Renderer Foundation v5.5.2 cruise visibility and size-range fixes."""
from pathlib import Path

renderer_path = Path("src/ContrailParticleRenderer.h")
plugin_path = Path("src/ContrailDebugPlugin.cpp")

renderer = renderer_path.read_text(encoding="utf-8")
plugin = plugin_path.read_text(encoding="utf-8")

renderer = renderer.replace("v5.5.1", "v5.5.2").replace("V5.5.1", "V5.5.2")
plugin = plugin.replace("v5.5.1", "v5.5.2").replace("V5.5.1", "V5.5.2")
plugin = plugin.replace("v5 point 5 point 1", "v5 point 5 point 2")

old_primary = '''                // v5.5 grouped parcels correctly but reduced each cloud below
                 // practical cruise-view visibility. Restore a physically broad,
                 // soft primary puff while retaining the low-alpha texture.
                 primary.sizeM = std::clamp(
                     anchor.widthM * (anchor.nearField ? 1.18f : 1.36f) *
                         sizeVariation,
                     0.90f,
                     16.0f);
                 primary.alpha = std::clamp(
                     (0.095f + std::sqrt(anchor.opacityStrength) * 0.50f) *
                         densityVariation * ageFade *
                         (anchor.nearField ? 0.86f : 1.0f),
                     0.070f,
                     0.55f);'''
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
if old_primary not in renderer:
    raise RuntimeError("v5.5.1 primary cruise calibration block was not found")
renderer = renderer.replace(old_primary, new_primary, 1)

old_companion = '''                    companion.sizeM = std::clamp(
                         anchor.widthM *
                             (0.76f + 0.16f * unitHash(
                                 companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                         0.65f,
                         10.0f);
                     companion.alpha = std::clamp(
                         (0.040f + std::sqrt(anchor.opacityStrength) * 0.24f) *
                             (0.82f + 0.18f * unitHash(
                                 companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                         0.025f,
                         0.22f);'''
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
if old_companion not in renderer:
    raise RuntimeError("v5.5.1 companion cruise calibration block was not found")
renderer = renderer.replace(old_companion, new_companion, 1)

old_mapping = '''        const float normalizedSize = std::clamp(
             (cloud.sizeM - 0.30f) / 11.70f,
             0.0f,
             1.0f);'''
new_mapping = '''        // Match the v5.5.2 particle asset's 0.75-20.0 metre size range.
         // The previous 0.30-12.0 mapping silently clamped larger cruise clouds.
         const float normalizedSize = std::clamp(
             (cloud.sizeM - 0.75f) / 19.25f,
             0.0f,
             1.0f);'''
if old_mapping not in renderer:
    raise RuntimeError("v5.5.1 normalized size mapping was not found")
renderer = renderer.replace(old_mapping, new_mapping, 1)

renderer_path.write_text(renderer, encoding="utf-8", newline="\n")
plugin_path.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v5.5.2 cruise visibility calibration")
