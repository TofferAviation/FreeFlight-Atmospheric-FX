#!/usr/bin/env python3
"""Apply Renderer Foundation v5.5.1 visibility and tangent-continuity fixes."""
from pathlib import Path

renderer_path = Path("src/ContrailParticleRenderer.h")
plugin_path = Path("src/ContrailDebugPlugin.cpp")

renderer = renderer_path.read_text(encoding="utf-8")
plugin = plugin_path.read_text(encoding="utf-8")

renderer = renderer.replace("v5.5", "v5.5.1").replace("V5.5", "V5.5.1")
plugin = plugin.replace("v5.5", "v5.5.1").replace("V5.5", "V5.5.1")
plugin = plugin.replace("v5 point 5", "v5 point 5 point 1")

old_primary = '''                primary.sizeM = std::clamp(
                    anchor.widthM * (anchor.nearField ? 0.76f : 0.96f) *
                        sizeVariation,
                    0.40f,
                    12.0f);
                primary.alpha = std::clamp(
                    (0.030f + std::sqrt(anchor.opacityStrength) * 0.24f) *
                        densityVariation * ageFade *
                        (anchor.nearField ? 0.75f : 1.0f),
                    0.014f,
                    0.24f);'''
new_primary = '''                // v5.5 grouped parcels correctly but reduced each cloud below
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
if old_primary not in renderer:
    raise RuntimeError("v5.5 primary cloud calibration block was not found")
renderer = renderer.replace(old_primary, new_primary, 1)

old_companion = '''                    companion.sizeM = std::clamp(
                        anchor.widthM *
                            (0.56f + 0.12f * unitHash(
                                companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                        0.35f,
                        8.0f);
                    companion.alpha = std::clamp(
                        (0.012f + std::sqrt(anchor.opacityStrength) * 0.12f) *
                            (0.78f + 0.18f * unitHash(
                                companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                        0.008f,
                        0.11f);'''
new_companion = '''                    companion.sizeM = std::clamp(
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
if old_companion not in renderer:
    raise RuntimeError("v5.5 companion cloud calibration block was not found")
renderer = renderer.replace(old_companion, new_companion, 1)

marker = '''        const std::size_t available = availableInstanceCount();'''
continuity = '''        // Rebuild tangents from the smoothed physical parcel positions.
        // The planner tangent can reverse when samples from opposite sides of
        // one parcel are collapsed together. Enforce a consistent direction
        // along each engine stream so the swirl frame cannot flip by 180 deg.
        for (auto& stream : anchors) {
            for (std::size_t index = 0; index < stream.size(); ++index) {
                engine::Vec3d direction {0.0, 0.0, -1.0};
                if (stream.size() > 1) {
                    const std::size_t previous = index > 0 ? index - 1 : index;
                    const std::size_t next = index + 1 < stream.size() ? index + 1 : index;
                    direction = {
                        stream[next].position.x - stream[previous].position.x,
                        stream[next].position.y - stream[previous].position.y,
                        stream[next].position.z - stream[previous].position.z
                    };
                    direction = normalize(direction);
                }
                if (index > 0) {
                    const double agreement =
                        stream[index - 1].tangent.x * direction.x +
                        stream[index - 1].tangent.y * direction.y +
                        stream[index - 1].tangent.z * direction.z;
                    if (agreement < 0.0) {
                        direction.x = -direction.x;
                        direction.y = -direction.y;
                        direction.z = -direction.z;
                    }
                }
                stream[index].tangent = direction;
            }
        }

        const std::size_t available = availableInstanceCount();'''
if marker not in renderer:
    raise RuntimeError("v5.5 available-instance marker was not found")
renderer = renderer.replace(marker, continuity, 1)

renderer_path.write_text(renderer, encoding="utf-8", newline="\n")
plugin_path.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v5.5.1 visibility and tangent-continuity hotfix")
