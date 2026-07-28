#!/usr/bin/env python3
"""Integrate Renderer Foundation v6.8 visible primary wake roll-up.

v6.7 proved the real WakeFluid state reaches the renderer, but the visual silhouette
barely changed because the main primary/fill field remained on the centreline while
small sub-metre/one-metre companions orbited inside a much larger white rope.

v6.8 uses the physical vortex phase/radius to displace the PRIMARY ice envelope and
its continuity fills during the vortex phase. Edge arms then wrap around that already
curved body. A runtime visibility gain can be cycled without restarting X-Plane.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def rx(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, lambda _m: replacement, text, count=1, flags=re.S | re.M)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")
    return out


r = RENDERER.read_text(encoding="utf-8")

# Give the roll-up enough of the existing 1024-instance budget to change the silhouette.
r = once(
    r,
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.52);\n        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.30);",
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.36);\n        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.22);",
    "v6.8 wake-structure budget",
)

# Runtime gain control. Default is intentionally strong enough to be visible while
# preserving Authentic / Strong / Showcase choices in one simulator session.
r = once(
    r,
    "    bool enabled() const { return enabled_; }\n    bool ready() const {",
    '''    bool enabled() const { return enabled_; }

    void cycleVortexVisualGain() {
        if (vortexVisualGain_ < 1.20) vortexVisualGain_ = 1.45;
        else if (vortexVisualGain_ < 1.70) vortexVisualGain_ = 1.95;
        else vortexVisualGain_ = 1.00;
    }
    double vortexVisualGain() const { return vortexVisualGain_; }
    double maximumPrimaryRollupOffsetM() const { return maximumPrimaryRollupOffsetM_; }
    std::size_t primaryRollupCloudletCount() const { return primaryRollupCloudletCount_; }

    bool ready() const {''',
    "v6.8 runtime vortex gain API",
)

# Reset visible roll-up diagnostics each frame.
r = once(
    r,
    "        maximumWakeDescentM_ = 0.0;\n        swirlCandidateCount_ = 0;",
    "        maximumWakeDescentM_ = 0.0;\n        maximumPrimaryRollupOffsetM_ = 0.0;\n        primaryRollupCloudletCount_ = 0;\n        swirlCandidateCount_ = 0;",
    "v6.8 rollup diagnostic reset",
)

# Move the primary ice envelope itself through the physical vortex cross-section.
primary_old = '''                node.ageSeconds = sample.ageSeconds;
                node.opacityStrength = sample.opacityStrength;
                candidates[asset][static_cast<std::size_t>(Layer::Primary)].push_back(node);'''
primary_new = '''                node.ageSeconds = sample.ageSeconds;
                node.opacityStrength = sample.opacityStrength;

                // v6.8: the primary visible ice envelope now participates in the
                // actual vortex roll-up. The parcel position remains the physical
                // centre supplied by WakeFluidSolver; this offset represents the
                // unresolved plume cross-section wrapped around the vortex core.
                if (sample.wakeInitialized && sample.ageSeconds >= 3.2f && sample.ageSeconds <= 33.0f) {
                    engine::Vec3d side {-node.tangent.z, 0.0, node.tangent.x};
                    const double sideM = std::sqrt(side.x * side.x + side.z * side.z);
                    if (sideM < 1.0e-6) side = {1.0, 0.0, 0.0};
                    else { side.x /= sideM; side.z /= sideM; }
                    engine::Vec3d up {
                        node.tangent.y * side.z - node.tangent.z * side.y,
                        node.tangent.z * side.x - node.tangent.x * side.z,
                        node.tangent.x * side.y - node.tangent.y * side.x
                    };
                    normalize(up);

                    const float capture = smoothstep(3.2f, 8.5f, sample.ageSeconds);
                    const float breakdown = 1.0f - 0.78f * smoothstep(23.0f, 33.0f, sample.ageSeconds);
                    const double physicalRadius = std::clamp(
                        static_cast<double>(sample.wakeVortexRadiusM), 0.0, 24.0);
                    const double coreRadius = std::clamp(
                        static_cast<double>(sample.wakeCoreRadiusM), 0.30, 4.5);
                    const double visibleRadius = std::clamp(
                        0.24 * physicalRadius + 0.58 * coreRadius +
                            0.16 * static_cast<double>(sample.widthM),
                        1.35,
                        5.80) * static_cast<double>(capture) *
                        static_cast<double>(breakdown) * vortexVisualGain_;
                    const double direction = engineIndex == 0 ? -1.0 : 1.0;
                    const double phase = static_cast<double>(sample.wakeVortexPhaseRad) +
                        direction * 0.16;
                    const double verticalRadius = visibleRadius * 0.82;
                    node.position = {
                        sample.localPositionM.x + side.x * std::cos(phase) * visibleRadius +
                            up.x * std::sin(phase) * verticalRadius,
                        sample.localPositionM.y + side.y * std::cos(phase) * visibleRadius +
                            up.y * std::sin(phase) * verticalRadius,
                        sample.localPositionM.z + side.z * std::cos(phase) * visibleRadius +
                            up.z * std::sin(phase) * verticalRadius
                    };
                    maximumPrimaryRollupOffsetM_ = std::max(
                        maximumPrimaryRollupOffsetM_, visibleRadius);
                    ++primaryRollupCloudletCount_;
                }
                candidates[asset][static_cast<std::size_t>(Layer::Primary)].push_back(node);'''
r = once(r, primary_old, primary_new, "v6.8 primary silhouette roll-up")

# Make fills follow the same vortex sheet rather than stitching a straight rope
# through the middle of the curled primaries.
fill_old = '''                    fill.opacityStrength = 0.5f * (sample.opacityStrength + next.opacityStrength);
                    candidates[fill.assetIndex][static_cast<std::size_t>(Layer::Fill)].push_back(fill);'''
fill_new = '''                    fill.opacityStrength = 0.5f * (sample.opacityStrength + next.opacityStrength);

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
                        const auto centre = midpoint(sample.localPositionM, next.localPositionM);
                        fill.position = {
                            centre.x + side.x * std::cos(phase) * visibleRadius +
                                up.x * std::sin(phase) * verticalRadius,
                            centre.y + side.y * std::cos(phase) * visibleRadius +
                                up.y * std::sin(phase) * verticalRadius,
                            centre.z + side.z * std::cos(phase) * visibleRadius +
                                up.z * std::sin(phase) * verticalRadius
                        };
                    }
                    candidates[fill.assetIndex][static_cast<std::size_t>(Layer::Fill)].push_back(fill);'''
r = once(r, fill_old, fill_new, "v6.8 curved continuity fills")

# Replace the v6.7 roll-up radius with an actually visible radius that uses the
# carried physical vortex radius. The previous formula did not use wakeVortexRadiusM.
r = once(
    r,
    '''                const double rollRadius = std::clamp(
                    (coreScale + widthScale) * static_cast<double>(capture) *
                        static_cast<double>(organised) * circulationScale,
                    0.18,
                    2.55);''',
    '''                const double physicalRadiusScale = std::clamp(
                    static_cast<double>(sample.wakeVortexRadiusM) * 0.28,
                    0.45,
                    5.20);
                const double rollRadius = std::clamp(
                    physicalRadiusScale + 0.55 * coreScale + 0.75 * widthScale,
                    1.45,
                    6.60) * static_cast<double>(capture) *
                    static_cast<double>(organised) * circulationScale * vortexVisualGain_;''',
    "v6.8 visible physical vortex radius",
)

# Stronger asymmetric wrapping offsets. They remain phase-coupled, not age-generated.
r = once(
    r,
    "                emitArm(1ULL, 0.48 + 0.32 * static_cast<double>(capture), 1.00, 0.34f);\n                emitArm(2ULL, -0.62 - 0.18 * static_cast<double>(capture), 0.72, 0.23f);",
    "                emitArm(1ULL, 0.78 + 0.26 * static_cast<double>(capture), 1.00, 0.46f);\n                emitArm(2ULL, -0.92 - 0.20 * static_cast<double>(capture), 0.78, 0.32f);",
    "v6.8 visible vortex arms",
)

# More frequent wake arms now that the primary is also rolling, but still stable-ID
# selected and bounded by the fixed pool.
r = once(
    r,
    "            if (sample.wakeInitialized && sample.ageSeconds >= 3.0f && sample.ageSeconds <= 31.0f &&\n                ((mix64(sample.renderId ^ 0x77ULL) % 3ULL) == 0ULL)) {",
    "            if (sample.wakeInitialized && sample.ageSeconds >= 3.0f && sample.ageSeconds <= 31.0f &&\n                ((mix64(sample.renderId ^ 0x77ULL) % 2ULL) == 0ULL)) {",
    "v6.8 arm sampling density",
)

# v6.8 identity and diagnostics members.
r = r.replace("Renderer Foundation v6.7", "Renderer Foundation v6.8")
r = r.replace("Renderer v6.7", "Renderer v6.8")
r = r.replace("physics-coupled vortex roll-up 3-D field", "visible primary-vortex roll-up 3-D field")
r = r.replace("physics-coupled ice-white 3-D morphology assets", "visible wake-rollup ice-white 3-D morphology assets")

r = once(
    r,
    "    double maximumWakeTurnDeg_ = 0.0;\n    double maximumWakeDescentM_ = 0.0;",
    "    double maximumWakeTurnDeg_ = 0.0;\n    double maximumWakeDescentM_ = 0.0;\n    double vortexVisualGain_ = 1.45;\n    double maximumPrimaryRollupOffsetM_ = 0.0;\n    std::size_t primaryRollupCloudletCount_ = 0;",
    "v6.8 rollup diagnostic members",
)

for token in (
    "budget * 0.36", "budget * 0.22", "wakeVortexRadiusM) * 0.28",
    "maximumPrimaryRollupOffsetM_", "vortexVisualGain_ = 1.45",
    "primaryRollupCloudletCount_", "% 2ULL) == 0ULL",
):
    if token not in r:
        raise RuntimeError(f"v6.8 renderer integration missing: {token}")
RENDERER.write_text(r, encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# Plugin: live command/menu to cycle visual gain without restarting the simulator,
# plus report diagnostics proving how far the primary mass was displaced.
# ---------------------------------------------------------------------------
p = PLUGIN.read_text(encoding="utf-8")
p = p.replace("v6.7", "v6.8").replace("V6.7", "V6.8")
p = p.replace("PHYSICS_COUPLED_VORTEX_ROLLUP", "VISIBLE_PRIMARY_VORTEX_ROLLUP")
p = p.replace("3D CLOUD V6.8 WAKE ROLLUP READY", "3D CLOUD V6.8 VISIBLE ROLLUP READY")
p = p.replace("LOADING V6.8 WAKE ROLLUP FIELD", "LOADING V6.8 VISIBLE ROLLUP FIELD")
p = p.replace("vortex_sampling_mode=PHYSICS_COUPLED_ARMS", "vortex_sampling_mode=PRIMARY_MASS_PLUS_EDGE_ARMS")
p = p.replace("wake_visual_model=PRIMARY_VORTEX_PLUS_SECONDARY_WAKE", "wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE")
p = p.replace("v6 point 7", "v6 point 8")

# Command handler branch.
p = once(
    p,
    '''        } else if (command == self->resetTrailCommand_) {
            self->liveEngine_.reset();''',
    '''        } else if (command == self->cycleVortexGainCommand_) {
            self->worldRenderer_.cycleVortexVisualGain();
            log("Vortex visual gain changed to " +
                std::to_string(self->worldRenderer_.vortexVisualGain()) + "x.\\n");
        } else if (command == self->resetTrailCommand_) {
            self->liveEngine_.reset();''',
    "v6.8 gain command handler",
)

# Command creation.
p = once(
    p,
    '''        resetTrailCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/reset_trail",
            "Reset the live contrail trail");''',
    '''        cycleVortexGainCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/cycle_vortex_gain",
            "Cycle FFAtmo vortex visibility gain without restarting X-Plane");
        resetTrailCommand_ = XPLMCreateCommand(
            "ffatmo_contrail_debug/reset_trail",
            "Reset the live contrail trail");''',
    "v6.8 gain command creation",
)

# Registration list.
p = p.replace(
    "            cycleAtmosphereCommand_,\n            resetTrailCommand_,",
    "            cycleAtmosphereCommand_,\n            cycleVortexGainCommand_,\n            resetTrailCommand_,",
)

# Menu entry.
p = once(
    p,
    '''        XPLMAppendMenuItemWithCommand(
            menu_, "Cycle Atmosphere: LIVE / DRY / PERSISTENT", cycleAtmosphereCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Reset Trail", resetTrailCommand_);''',
    '''        XPLMAppendMenuItemWithCommand(
            menu_, "Cycle Atmosphere: LIVE / DRY / PERSISTENT", cycleAtmosphereCommand_);
        XPLMAppendMenuItemWithCommand(
            menu_, "Cycle Vortex Visibility: Authentic / Strong / Showcase", cycleVortexGainCommand_);
        XPLMAppendMenuItemWithCommand(menu_, "Reset Trail", resetTrailCommand_);''',
    "v6.8 gain menu entry",
)

# Member.
p = once(
    p,
    "    XPLMCommandRef cycleAtmosphereCommand_ = nullptr;\n    XPLMCommandRef resetTrailCommand_ = nullptr;",
    "    XPLMCommandRef cycleAtmosphereCommand_ = nullptr;\n    XPLMCommandRef cycleVortexGainCommand_ = nullptr;\n    XPLMCommandRef resetTrailCommand_ = nullptr;",
    "v6.8 gain command member",
)

# Report diagnostics, anchored on renderer mode line added by v6.7.
p = once(
    p,
    '''               << "wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE" << '\n' ''',
    '''               << "wake_visual_model=VISIBLE_PRIMARY_ROLLUP_PLUS_SECONDARY_WAKE" << '\n'
               << "vortex_visual_gain=" << worldRenderer_.vortexVisualGain() << '\n'
               << "world_renderer_primary_rollup_cloudlet_count="
               << worldRenderer_.primaryRollupCloudletCount() << '\n'
               << "world_renderer_maximum_primary_rollup_offset_m="
               << worldRenderer_.maximumPrimaryRollupOffsetM() << '\n' ''',
    "v6.8 report rollup diagnostics",
)

for token in (
    "VISIBLE_PRIMARY_VORTEX_ROLLUP", "3D CLOUD V6.8 VISIBLE ROLLUP READY",
    "cycle_vortex_gain", "cycleVortexVisualGain", "vortex_visual_gain=",
    "world_renderer_maximum_primary_rollup_offset_m=",
):
    if token not in p:
        raise RuntimeError(f"v6.8 runtime integration missing: {token}")
PLUGIN.write_text(p, encoding="utf-8", newline="\n")

print("Integrated Renderer Foundation v6.8 visible primary vortex roll-up")
