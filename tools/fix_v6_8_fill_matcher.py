#!/usr/bin/env python3
# v6.8 retry trigger: curved-fill matcher is intentionally applied after v6.7.
from pathlib import Path
import re

path = Path(__file__).with_name("patch_v6_8_visible_primary_rollup.py")
text = path.read_text(encoding="utf-8")
pattern = re.compile(
    r'''# Make fills follow the same vortex sheet.*?r = once\(r, fill_old, fill_new, "v6\.8 curved continuity fills"\)''',
    re.S,
)
replacement = r'''# Make fills follow the same vortex sheet rather than stitching a straight rope
# through the middle of the curled primaries. v6.3 has already applied its small
# stable packing offset, so bend the resulting fill after that line.
fill_anchor = ''' + "'''" + r'''                    applyPackingOffset(fill, fill.cloudId ^ 0x44ULL, fillPackingRadius);''' + "'''" + r'''
fill_replacement = fill_anchor + ''' + "'''" + r'''

                    if (sample.wakeInitialized && next.wakeInitialized &&
                        fill.ageSeconds >= 3.2f && fill.ageSeconds <= 33.0f) {
                        engine::Vec3d side {-fill.tangent.z, 0.0, fill.tangent.x};
                        const double sideM = std::sqrt(side.x * side.x + side.z * side.z);
                        if (sideM < 1.0e-6) side = {1.0, 0.0, 0.0};
                        else { side.x /= sideM; side.z /= sideM; }
                        engine::Vec3d up {
                            fill.tangent.y * side.z - fill.tangent.z * side.y,
                            fill.tangent.z * side.x - fill.tangent.x * side.z,
                            fill.tangent.x * side.y - fill.tangent.y * side.x
                        };
                        normalize(up);

                        const double sx = std::cos(static_cast<double>(sample.wakeVortexPhaseRad)) +
                                          std::cos(static_cast<double>(next.wakeVortexPhaseRad));
                        const double sy = std::sin(static_cast<double>(sample.wakeVortexPhaseRad)) +
                                          std::sin(static_cast<double>(next.wakeVortexPhaseRad));
                        const double phaseBase = std::atan2(sy, sx);
                        const double physicalRadius = 0.5 * (
                            static_cast<double>(sample.wakeVortexRadiusM) +
                            static_cast<double>(next.wakeVortexRadiusM));
                        const double coreRadius = 0.5 * (
                            static_cast<double>(sample.wakeCoreRadiusM) +
                            static_cast<double>(next.wakeCoreRadiusM));
                        const double width = 0.5 * (
                            static_cast<double>(sample.widthM) + static_cast<double>(next.widthM));
                        const float capture = smoothstep(3.2f, 8.5f, fill.ageSeconds);
                        const float breakdown = 1.0f - 0.78f * smoothstep(23.0f, 33.0f, fill.ageSeconds);
                        const double visibleRadius = std::clamp(
                            0.24 * physicalRadius + 0.58 * coreRadius + 0.16 * width,
                            1.35,
                            5.80) * static_cast<double>(capture) *
                            static_cast<double>(breakdown) * vortexVisualGain_;
                        const double direction = engineIndex == 0 ? -1.0 : 1.0;
                        const double phase = phaseBase + direction * 0.16;
                        const double verticalRadius = visibleRadius * 0.82;
                        const auto centre = fill.position;
                        fill.position = {
                            centre.x + side.x * std::cos(phase) * visibleRadius +
                                up.x * std::sin(phase) * verticalRadius,
                            centre.y + side.y * std::cos(phase) * visibleRadius +
                                up.y * std::sin(phase) * verticalRadius,
                            centre.z + side.z * std::cos(phase) * visibleRadius +
                                up.z * std::sin(phase) * verticalRadius
                        };
                    }''' + "'''" + r'''
r = once(r, fill_anchor, fill_replacement, "v6.8 curved continuity fills")'''
new, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise RuntimeError(f"v6.8 fill patch section: expected one match, found {count}")
path.write_text(new, encoding="utf-8", newline="\n")
print("Stabilized v6.8 curved fill matcher")
