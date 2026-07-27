#!/usr/bin/env python3
"""Apply Renderer Foundation v5.7.1 projected billboard cloud rendering."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailParticleRenderer.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"

renderer = RENDERER.read_text(encoding="utf-8")
plugin = PLUGIN.read_text(encoding="utf-8")

renderer = renderer.replace("v5.6", "v5.7.1").replace("V5.6", "V5.7.1")
plugin = plugin.replace("v5.6", "v5.7.1").replace("V5.6", "V5.7.1")
plugin = plugin.replace("v5 point 6", "v5 point 7 point 1")

if '#include "XPLMCamera.h"' not in renderer:
    renderer = renderer.replace(
        '#include "XPLMDataAccess.h"\n',
        '#include "XPLMCamera.h"\n#include "XPLMDataAccess.h"\n',
        1,
    )

# Use stable longitudinal coverage rather than physical-width-sized round puffs.
old_loop = """            for (const auto& anchor : stream) {
                CloudNode primary = anchor;"""
new_loop = """            for (std::size_t anchorIndex = 0; anchorIndex < stream.size(); ++anchorIndex) {
                const auto& anchor = stream[anchorIndex];
                CloudNode primary = anchor;"""
if old_loop not in renderer:
    raise RuntimeError("v5.6 primary anchor loop was not found")
renderer = renderer.replace(old_loop, new_loop, 1)

renderer, count = re.subn(
    r"                const float physicalWidth = anchor\.widthM \*\n"
    r"                    \(anchor\.nearField \? 1\.18f : 1\.52f\);\n"
    r"                primary\.sizeM = std::clamp\(\n"
    r"                    std::max\(physicalWidth, ageWidthFloor\(anchor\.ageSeconds\)\) \*\n"
    r"                        sizeVariation,\n"
    r"                    1\.10f,\n"
    r"                    19\.0f\);",
    '''                double localGapM = 0.0;
                if (anchorIndex > 0) {
                    const double gap = distanceM(stream[anchorIndex - 1].position, anchor.position);
                    if (std::isfinite(gap) && gap <= 140.0) localGapM = std::max(localGapM, gap);
                }
                if (anchorIndex + 1 < stream.size()) {
                    const double gap = distanceM(anchor.position, stream[anchorIndex + 1].position);
                    if (std::isfinite(gap) && gap <= 140.0) localGapM = std::max(localGapM, gap);
                }
                const float ageLengthFloor = 4.5f +
                    2.5f * smoothstep(1.0f, 6.0f, anchor.ageSeconds) +
                    4.0f * smoothstep(6.0f, 18.0f, anchor.ageSeconds) +
                    3.0f * smoothstep(18.0f, 48.0f, anchor.ageSeconds);
                const float gapCoverage = static_cast<float>(localGapM) *
                    (anchor.ageSeconds <= 32.0f ? 0.74f : 1.12f);
                primary.sizeM = std::clamp(
                    std::max(ageLengthFloor, gapCoverage) * sizeVariation,
                    4.0f,
                    32.0f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 primary size block was not found")

renderer, count = re.subn(
    r"                primary\.alpha = std::clamp\(\n"
    r"                    \(0\.14f \+ std::sqrt\(anchor\.opacityStrength\) \* 0\.58f\) \*\n"
    r"                        densityVariation \* ageFade \*\n"
    r"                        \(anchor\.nearField \? 0\.82f : 1\.0f\),\n"
    r"                    0\.10f,\n"
    r"                    0\.68f\);",
    '''                primary.alpha = std::clamp(
                    (0.18f + std::sqrt(anchor.opacityStrength) * 0.62f) *
                        densityVariation * ageFade *
                        (anchor.nearField ? 0.80f : 1.0f),
                    0.12f,
                    0.72f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 primary alpha block was not found")

renderer, count = re.subn(
    r"                    companion\.sizeM = std::clamp\(\n"
    r"                        primary\.sizeM \*\n"
    r"                            \(0\.50f \+ 0\.10f \* unitHash\(\n"
    r"                                companion\.cloudId \^ 0xbf58476d1ce4e5b9ULL\)\),\n"
    r"                        1\.0f,\n"
    r"                        10\.5f\);\n"
    r"                    companion\.alpha = std::clamp\(\n"
    r"                        primary\.alpha \*\n"
    r"                            \(0\.28f \+ 0\.10f \* unitHash\(\n"
    r"                                companion\.cloudId \^ 0x632be59bd9b4e019ULL\)\),\n"
    r"                        0\.045f,\n"
    r"                        0\.24f\);",
    '''                    companion.sizeM = std::clamp(
                        primary.sizeM *
                            (0.34f + 0.08f * unitHash(
                                companion.cloudId ^ 0xbf58476d1ce4e5b9ULL)),
                        3.0f,
                        11.0f);
                    companion.alpha = std::clamp(
                        primary.alpha *
                            (0.20f + 0.08f * unitHash(
                                companion.cloudId ^ 0x632be59bd9b4e019ULL)),
                        0.035f,
                        0.18f);''',
    renderer,
    count=1,
)
if count != 1:
    raise RuntimeError("v5.6 companion calibration was not found")

old_fill = """                fill.sizeM = std::clamp(primary.sizeM * 0.82f, 1.0f, 15.0f);
                fill.alpha = std::clamp(primary.alpha * 0.58f, 0.065f, 0.36f);"""
new_fill = """                fill.sizeM = std::clamp(primary.sizeM * 0.92f, 4.0f, 28.0f);
                fill.alpha = std::clamp(primary.alpha * 0.72f, 0.090f, 0.46f);"""
if old_fill not in renderer:
    raise RuntimeError("v5.6 fill calibration was not found")
renderer = renderer.replace(old_fill, new_fill, 1)

# Read the active X-Plane camera once per update. A normal billboard can then be
# rotated in screen space so its long texture axis follows the projected wake.
empty_block = '''        if (samples.empty()) {
            hideAllInstances();
            return;
        }
'''
if empty_block not in renderer:
    raise RuntimeError("Renderer empty-sample block was not found")
renderer = renderer.replace(empty_block, empty_block + "\n        updateCameraBasis();\n", 1)

helpers = r'''    void updateCameraBasis() {
        XPLMCameraPosition_t camera {};
        XPLMReadCameraPosition(&camera);
        const double degreesToRadians = 0.017453292519943295;
        const double heading = static_cast<double>(camera.heading) * degreesToRadians;
        const double pitch = static_cast<double>(camera.pitch) * degreesToRadians;
        const double roll = static_cast<double>(camera.roll) * degreesToRadians;
        const double ch = std::cos(heading);
        const double sh = std::sin(heading);
        const double cp = std::cos(pitch);
        const double sp = std::sin(pitch);
        const double cr = std::cos(roll);
        const double sr = std::sin(roll);

        const engine::Vec3d rightBase {ch, 0.0, sh};
        const engine::Vec3d upBase {-sh * sp, cp, ch * sp};
        cameraRight_ = {
            rightBase.x * cr + upBase.x * sr,
            rightBase.y * cr + upBase.y * sr,
            rightBase.z * cr + upBase.z * sr
        };
        cameraUp_ = {
            -rightBase.x * sr + upBase.x * cr,
            -rightBase.y * sr + upBase.y * cr,
            -rightBase.z * sr + upBase.z * cr
        };
    }

    float projectedRotation(const CloudNode& cloud) const {
        double tx = cloud.tangent.x;
        double ty = cloud.tangent.y;
        double tz = cloud.tangent.z;
        normalizeTangent(tx, ty, tz);
        const double screenX = tx * cameraRight_.x +
                               ty * cameraRight_.y +
                               tz * cameraRight_.z;
        const double screenY = tx * cameraUp_.x +
                               ty * cameraUp_.y +
                               tz * cameraUp_.z;
        const double projection = std::sqrt(screenX * screenX + screenY * screenY);
        if (!std::isfinite(projection) || projection < 1.0e-5) {
            return unitHash(cloud.cloudId ^ 0x9e3779b97f4a7c15ULL);
        }
        double angleDegrees = std::atan2(screenY, screenX) * kRadiansToDegrees;
        while (angleDegrees < 0.0) angleDegrees += 360.0;
        while (angleDegrees >= 360.0) angleDegrees -= 360.0;
        return static_cast<float>(angleDegrees / 360.0);
    }

'''
if "void updateCameraBasis()" not in renderer:
    renderer = renderer.replace("    void setBillboard(\n", helpers + "    void setBillboard(\n", 1)

set_billboard = r'''    void setBillboard(
        std::size_t poolIndex,
        const CloudNode& cloud) {
        if (poolIndex >= instances_.size() || !instances_[poolIndex]) return;

        XPLMDrawInfo_t drawInfo {};
        drawInfo.structSize = sizeof(drawInfo);
        drawInfo.x = static_cast<float>(cloud.position.x);
        drawInfo.y = static_cast<float>(cloud.position.y);
        drawInfo.z = static_cast<float>(cloud.position.z);
        drawInfo.pitch = 0.0f;
        drawInfo.heading = 0.0f;
        drawInfo.roll = 0.0f;

        // The standard billboard's square is the longitudinal footprint. The
        // texture contains a narrow fluffy cloud body, so visible width remains
        // a fraction of the segment length rather than becoming a cotton ball.
        const float normalizedSize = std::clamp(
            (cloud.sizeM - 4.0f) / 28.0f,
            0.0f,
            1.0f);
        const float values[] = {
            1.0f,
            normalizedSize,
            cloud.alpha,
            projectedRotation(cloud)
        };
        XPLMInstanceSetPosition(
            instances_[poolIndex],
            &drawInfo,
            values);
        active_[poolIndex] = true;
    }
'''
renderer, count = re.subn(
    r"    void setBillboard\(.*?\n    \}\n\n    void hideInstance",
    lambda _: set_billboard + "\n    void hideInstance",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError("v5.6 setBillboard function was not replaced")

if "engine::Vec3d cameraRight_" not in renderer:
    renderer = renderer.replace(
        "    std::filesystem::path assetDirectory_;\n",
        "    engine::Vec3d cameraRight_ {1.0, 0.0, 0.0};\n"
        "    engine::Vec3d cameraUp_ {0.0, 1.0, 0.0};\n"
        "    std::filesystem::path assetDirectory_;\n",
        1,
    )

RENDERER.write_text(renderer, encoding="utf-8", newline="\n")
PLUGIN.write_text(plugin, encoding="utf-8", newline="\n")
print("Applied Renderer Foundation v5.7.1 projected billboard cloud renderer")
