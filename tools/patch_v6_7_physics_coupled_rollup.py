#!/usr/bin/env python3
"""Integrate Renderer Foundation v6.7 physics-coupled contrail vortex roll-up.

v6.6 proved the supported native 3-D renderer can hold a stable 1024-object field
with essentially perfect ownership reuse. v6.7 stops inventing a decorative
age-based helix in the renderer and carries the live Wake Fluid state through the
render planner. Vortex arm placement is then based on the parcel's actual
vortex-relative phase/radius/circulation. A sparse secondary-wake residue is
left toward the parcel's pre-vortex position to expose the characteristic
primary/secondary wake split during the vortex phase.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLANNER_H = ROOT / "src" / "render" / "ContrailRenderPlanner.h"
PLANNER_CPP = ROOT / "src" / "render" / "ContrailRenderPlanner.cpp"
PLUGIN = ROOT / "src" / "ContrailDebugPlugin.cpp"
RENDERER = ROOT / "src" / "ContrailCloudletRendererV62.h"


def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def rx(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, lambda _m: replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")
    return out


# ---------------------------------------------------------------------------
# Carry live wake state through the render planner instead of reconstructing a
# decorative swirl from age alone.
# ---------------------------------------------------------------------------
h = PLANNER_H.read_text(encoding="utf-8")
h = once(
    h,
    '''    float normalizedIceMass = 0.0f;
    float ageSeconds = 0.0f;
    bool syntheticHead = false;''',
    '''    float normalizedIceMass = 0.0f;
    float ageSeconds = 0.0f;

    // v6.7 live Wake Fluid coupling. These are scalar coordinates in the
    // parcel's own stored vortex cross-section, so they survive world/local
    // origin changes without requiring a camera-space reconstruction.
    float wakeVortexPhaseRad = 0.0f;
    float wakeVortexRadiusM = 0.0f;
    float wakeCoreRadiusM = 0.0f;
    float wakeCirculationM2ps = 0.0f;
    float wakeDescentMps = 0.0f;
    float wakeDisplacementLateralM = 0.0f;
    float wakeDisplacementVerticalM = 0.0f;
    bool wakeInitialized = false;
    bool syntheticHead = false;''',
    "render input wake fields",
)
h = once(
    h,
    '''    float priority = 0.0f;
    std::uint8_t opacityBucket = 0;''',
    '''    float priority = 0.0f;

    // Interpolated v6.7 Wake Fluid state used by the native 3-D renderer.
    float wakeVortexPhaseRad = 0.0f;
    float wakeVortexRadiusM = 0.0f;
    float wakeCoreRadiusM = 0.0f;
    float wakeCirculationM2ps = 0.0f;
    float wakeDescentMps = 0.0f;
    float wakeDisplacementLateralM = 0.0f;
    float wakeDisplacementVerticalM = 0.0f;
    bool wakeInitialized = false;
    std::uint8_t opacityBucket = 0;''',
    "render sample wake fields",
)
PLANNER_H.write_text(h, encoding="utf-8", newline="\n")


c = PLANNER_CPP.read_text(encoding="utf-8")
c = once(
    c,
    '''float mix(float a, float b, float ratio) {
    return a + (b - a) * ratio;
}''',
    '''float mix(float a, float b, float ratio) {
    return a + (b - a) * ratio;
}

float mixAngleRadians(float a, float b, float ratio) {
    const float delta = std::atan2(std::sin(b - a), std::cos(b - a));
    return a + delta * ratio;
}''',
    "circular wake phase interpolation",
)

c = once(
    c,
    '''    hashValue(hash, sample.opacityStrength);
    hashValue(hash, sample.ageSeconds);
    hashValue(hash, sample.opacityBucket);''',
    '''    hashValue(hash, sample.opacityStrength);
    hashValue(hash, sample.ageSeconds);
    hashValue(hash, sample.wakeVortexPhaseRad);
    hashValue(hash, sample.wakeVortexRadiusM);
    hashValue(hash, sample.wakeCoreRadiusM);
    hashValue(hash, sample.wakeCirculationM2ps);
    hashValue(hash, sample.wakeDescentMps);
    hashValue(hash, sample.wakeDisplacementLateralM);
    hashValue(hash, sample.wakeDisplacementVerticalM);
    hashValue(hash, sample.wakeInitialized);
    hashValue(hash, sample.opacityBucket);''',
    "wake sample deterministic hash",
)

c = once(
    c,
    '''    const float physicsRadius = mix(first.physicsRadiusM, second.physicsRadiusM, ratio);
    const float optical = mix(first.opticalDepth, second.opticalDepth, ratio);''',
    '''    const float physicsRadius = mix(first.physicsRadiusM, second.physicsRadiusM, ratio);
    const float optical = mix(first.opticalDepth, second.opticalDepth, ratio);

    const bool wakeInitialized = first.wakeInitialized || second.wakeInitialized;
    float wakePhase = 0.0f;
    float wakeRadius = 0.0f;
    float wakeCoreRadius = 0.0f;
    float wakeCirculation = 0.0f;
    float wakeDescent = 0.0f;
    float wakeLateral = 0.0f;
    float wakeVertical = 0.0f;
    if (first.wakeInitialized && second.wakeInitialized) {
        wakePhase = mixAngleRadians(first.wakeVortexPhaseRad, second.wakeVortexPhaseRad, ratio);
        wakeRadius = mix(first.wakeVortexRadiusM, second.wakeVortexRadiusM, ratio);
        wakeCoreRadius = mix(first.wakeCoreRadiusM, second.wakeCoreRadiusM, ratio);
        wakeCirculation = mix(first.wakeCirculationM2ps, second.wakeCirculationM2ps, ratio);
        wakeDescent = mix(first.wakeDescentMps, second.wakeDescentMps, ratio);
        wakeLateral = mix(first.wakeDisplacementLateralM, second.wakeDisplacementLateralM, ratio);
        wakeVertical = mix(first.wakeDisplacementVerticalM, second.wakeDisplacementVerticalM, ratio);
    } else {
        const auto& wakeSource = first.wakeInitialized ? first : second;
        wakePhase = wakeSource.wakeVortexPhaseRad;
        wakeRadius = wakeSource.wakeVortexRadiusM;
        wakeCoreRadius = wakeSource.wakeCoreRadiusM;
        wakeCirculation = wakeSource.wakeCirculationM2ps;
        wakeDescent = wakeSource.wakeDescentMps;
        wakeLateral = wakeSource.wakeDisplacementLateralM;
        wakeVertical = wakeSource.wakeDisplacementVerticalM;
    }''',
    "interpolated wake state",
)

c = once(
    c,
    '''    sample.opacityStrength = strength;
    sample.ageSeconds = age;
    sample.opacityBucket = opacityBucket(strength);''',
    '''    sample.opacityStrength = strength;
    sample.ageSeconds = age;
    sample.wakeVortexPhaseRad = wakePhase;
    sample.wakeVortexRadiusM = wakeRadius;
    sample.wakeCoreRadiusM = wakeCoreRadius;
    sample.wakeCirculationM2ps = wakeCirculation;
    sample.wakeDescentMps = wakeDescent;
    sample.wakeDisplacementLateralM = wakeLateral;
    sample.wakeDisplacementVerticalM = wakeVertical;
    sample.wakeInitialized = wakeInitialized;
    sample.opacityBucket = opacityBucket(strength);''',
    "render sample wake assignment",
)
PLANNER_CPP.write_text(c, encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# Export real per-parcel vortex coordinates from LiveContrailEngine into the
# planner inputs. The parcel has already been advected by the same wake field.
# ---------------------------------------------------------------------------
p = PLUGIN.read_text(encoding="utf-8")
p = once(
    p,
    '''            item.normalizedIceMass = parcel.normalizedIceMass * nucleationOpacity;
            item.ageSeconds = parcel.ageSeconds;
            inputs.push_back(item);''',
    '''            item.normalizedIceMass = parcel.normalizedIceMass * nucleationOpacity;
            item.ageSeconds = parcel.ageSeconds;
            if (parcel.wakeFluid.initialized) {
                const float halfSeparation = 0.5f * parcel.wakeFluid.vortexSeparationM;
                const float coreLateral = parcel.wakeFluid.initialLateralM < 0.0f
                    ? -halfSeparation
                    : (parcel.wakeFluid.initialLateralM > 0.0f ? halfSeparation : 0.0f);
                const float dx = parcel.wakeFluid.lateralM - coreLateral;
                const float dy = parcel.wakeFluid.verticalM - parcel.wakeFluid.wakeCentreVerticalM;
                item.wakeVortexPhaseRad = std::atan2(dy, dx);
                item.wakeVortexRadiusM = std::sqrt(dx * dx + dy * dy);
                item.wakeCoreRadiusM = parcel.wakeFluid.coreRadiusM;
                item.wakeCirculationM2ps = parcel.wakeFluid.circulationM2ps;
                item.wakeDescentMps = parcel.wakeFluid.wakeDescentMps;
                item.wakeDisplacementLateralM = parcel.appliedVortexLateralM;
                item.wakeDisplacementVerticalM = parcel.appliedVortexVerticalM;
                item.wakeInitialized = true;
            }
            inputs.push_back(item);''',
    "live Wake Fluid render input",
)

p = p.replace(
    "FFAtmo World Contrail Visual Debug Report v6.6 SMOOTH_STABLE_VORTEX_FIELD",
    "FFAtmo World Contrail Visual Debug Report v6.7 PHYSICS_COUPLED_VORTEX_ROLLUP",
)
p = p.replace("3D CLOUD V6.6 READY", "3D CLOUD V6.7 WAKE ROLLUP READY")
p = p.replace("LOADING V6.6 SMOOTH CLOUD FIELD", "LOADING V6.7 WAKE ROLLUP FIELD")
p = p.replace("v6 point 6", "v6 point 7")
p = p.replace("v6.6", "v6.7").replace("V6.6", "V6.7")
p = p.replace(
    "renders smooth micro-cutout ice-white cloudlets with stable ownership, streamline-locked handoff and restrained vortex roll-up.",
    "renders physics-coupled ice cloud volumes whose roll-up phase comes from the live finite-core wake solver.",
)

p = once(
    p,
    '''               << "renderer_selection_mode=STABLE_HASH" << '\n'
               << "vortex_sampling_mode=SPARSE_SUBSCALE" << '\n' ''',
    '''               << "renderer_selection_mode=STABLE_HASH" << '\n'
               << "vortex_sampling_mode=PHYSICS_COUPLED_ARMS" << '\n'
               << "wake_visual_model=PRIMARY_VORTEX_PLUS_SECONDARY_WAKE" << '\n' ''',
    "v6.7 report identity modes",
)
PLUGIN.write_text(p, encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# Native 3-D renderer. Replace the decorative age-phase companion with two
# small physics-coupled roll-up arms and a very sparse secondary-wake residue.
# Primaries remain exactly on the actual parcel trajectory produced by the
# Wake Fluid solver.
# ---------------------------------------------------------------------------
r = RENDERER.read_text(encoding="utf-8")

# Give the visual cloud more longitudinal continuity while leaving ~20% of the
# useful capacity for wake structure. Stable-hash selection remains intact.
r = once(
    r,
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.56);\n        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.34);",
    "        std::size_t primaryBudget = static_cast<std::size_t>(budget * 0.52);\n        std::size_t fillBudget = static_cast<std::size_t>(budget * 0.30);",
    "v6.7 layer budget",
)

# Reduce arbitrary packing further: wake deformation should now come from the
# solver state, not noise offsets.
r = once(
    r,
    '''                const double packingRadius = 0.035 +
                    0.16 * static_cast<double>(smoothstep(1.5f, 20.0f, sample.ageSeconds));''',
    '''                const double packingRadius = 0.018 +
                    0.075 * static_cast<double>(smoothstep(1.5f, 20.0f, sample.ageSeconds));''',
    "v6.7 primary packing",
)
r = once(
    r,
    '''                    const double fillPackingRadius = 0.05 +
                        0.20 * static_cast<double>(smoothstep(1.0f, 18.0f, fill.ageSeconds));''',
    '''                    const double fillPackingRadius = 0.025 +
                        0.095 * static_cast<double>(smoothstep(1.0f, 18.0f, fill.ageSeconds));''',
    "v6.7 fill packing",
)

# More slots for the young/mature soft classes where vortex arms live; retain
# exactly 1024 native objects.
r = once(
    r,
    "        56, 164, 164, 164, 164, 148, 156, 8",
    "        52, 152, 176, 160, 190, 142, 144, 8",
    "v6.7 pool allocation",
)

# Stable IDs for two roll-up arms and the secondary wake residue.
r = once(
    r,
    '''    static std::uint64_t swirlId(const render::ContrailRenderSample& sample) {
        return mix64(sample.renderId ^ 0x5a17a11c10d5eedULL);
    }''',
    '''    static std::uint64_t swirlId(const render::ContrailRenderSample& sample) {
        return mix64(sample.renderId ^ 0x5a17a11c10d5eedULL);
    }

    static std::uint64_t vortexArmId(const render::ContrailRenderSample& sample,
                                     std::uint64_t arm) {
        return mix64(sample.renderId ^ 0x5a17a11c10d5eedULL ^
                     (arm * 0x9e3779b97f4a7c15ULL));
    }

    static std::uint64_t secondaryWakeId(const render::ContrailRenderSample& sample) {
        return mix64(sample.renderId ^ 0x51ec0da4b3f12977ULL);
    }''',
    "v6.7 stable wake IDs",
)

# Replace the complete v6.5/v6.6 organised vortex block after the v6.6 patch
# has adjusted its sampling/radius constants.
physics_rollup = r'''            // v6.7 physics-coupled vortex phase. The primary cloud remains on
            // the parcel trajectory already advanced by WakeFluidSolver. Two sparse,
            // smaller cloud arms expose the local roll-up around that trajectory using
            // the parcel's actual vortex-relative phase, core radius and circulation.
            if (sample.wakeInitialized && sample.ageSeconds >= 3.0f && sample.ageSeconds <= 31.0f &&
                ((mix64(sample.renderId ^ 0x77ULL) % 3ULL) == 0ULL)) {
                ++swirlCandidateCount_;

                engine::Vec3d tangent = sample.trailTangentLocal;
                normalize(tangent);
                engine::Vec3d side {-tangent.z, 0.0, tangent.x};
                const double sideM = std::sqrt(side.x * side.x + side.z * side.z);
                if (sideM < 1.0e-6) side = {1.0, 0.0, 0.0};
                else { side.x /= sideM; side.z /= sideM; }
                engine::Vec3d up {
                    tangent.y * side.z - tangent.z * side.y,
                    tangent.z * side.x - tangent.x * side.z,
                    tangent.x * side.y - tangent.y * side.x
                };
                normalize(up);

                const float capture = smoothstep(3.0f, 9.0f, sample.ageSeconds);
                const float organised = 1.0f - smoothstep(21.0f, 31.0f, sample.ageSeconds);
                const double circulationScale = std::clamp(
                    static_cast<double>(sample.wakeCirculationM2ps) / 260.0, 0.45, 1.25);
                const double coreScale = std::clamp(
                    0.35 + static_cast<double>(sample.wakeCoreRadiusM) * 0.52,
                    0.45,
                    1.55);
                const double widthScale = std::clamp(
                    static_cast<double>(sample.widthM) * 0.15,
                    0.22,
                    1.20);
                const double rollRadius = std::clamp(
                    (coreScale + widthScale) * static_cast<double>(capture) *
                        static_cast<double>(organised) * circulationScale,
                    0.18,
                    2.55);
                const double direction = engineIndex == 0 ? -1.0 : 1.0;
                const double physicalPhase = static_cast<double>(sample.wakeVortexPhaseRad);

                auto emitArm = [&](std::uint64_t arm,
                                   double phaseOffset,
                                   double radiusScale,
                                   float opacityScale) {
                    CloudNode roll;
                    roll.cloudId = vortexArmId(sample, arm);
                    roll.engineIndex = engineIndex;
                    roll.ageSeconds = sample.ageSeconds;
                    roll.layer = Layer::Swirl;
                    roll.tangent = tangent;
                    roll.opacityStrength = sample.opacityStrength * opacityScale;
                    const std::size_t fullSizeAsset = assetForCloud(
                        roll.ageSeconds, roll.opacityStrength, roll.cloudId);
                    const std::size_t fullSizeClass = fullSizeAsset / 2u;
                    const std::size_t smallerClass = fullSizeClass > 0u ? fullSizeClass - 1u : 0u;
                    // Always use the soft morphology for the outer wrapping arms.
                    roll.assetIndex = smallerClass * 2u;

                    const double phase = physicalPhase + direction * phaseOffset;
                    const double radius = rollRadius * radiusScale;
                    const double verticalRadius = radius * (0.72 + 0.10 * static_cast<double>(capture));
                    roll.position = {
                        sample.localPositionM.x + side.x * std::cos(phase) * radius +
                            up.x * std::sin(phase) * verticalRadius,
                        sample.localPositionM.y + side.y * std::cos(phase) * radius +
                            up.y * std::sin(phase) * verticalRadius,
                        sample.localPositionM.z + side.z * std::cos(phase) * radius +
                            up.z * std::sin(phase) * verticalRadius
                    };
                    candidates[roll.assetIndex][static_cast<std::size_t>(Layer::Swirl)].push_back(roll);
                    maximumSwirlRadiusM_ = std::max(maximumSwirlRadiusM_, radius);
                    swirlRadiusSumM_ += radius;
                    ++swirlRadiusSampleCount_;
                };

                // The leading arm is denser; the trailing arm is smaller and softer.
                // Together they form a curved/crescent cross-section rather than a
                // second full-size rope or a perfectly periodic corkscrew.
                emitArm(1ULL, 0.48 + 0.32 * static_cast<double>(capture), 1.00, 0.34f);
                emitArm(2ULL, -0.62 - 0.18 * static_cast<double>(capture), 0.72, 0.23f);

                // A small fraction of ice is left toward the pre-vortex trajectory.
                // This exposes the secondary wake above/inside the sinking primary
                // vortex without creating a third persistent contrail tube.
                if (sample.ageSeconds >= 9.0f && sample.ageSeconds <= 29.0f &&
                    ((mix64(sample.renderId ^ 0x5ec0ULL) % 7ULL) == 0ULL)) {
                    CloudNode secondary;
                    secondary.cloudId = secondaryWakeId(sample);
                    secondary.engineIndex = engineIndex;
                    secondary.ageSeconds = sample.ageSeconds;
                    secondary.layer = Layer::Swirl;
                    secondary.tangent = tangent;
                    secondary.opacityStrength = sample.opacityStrength *
                        (0.16f + 0.08f * smoothstep(9.0f, 18.0f, sample.ageSeconds));
                    const std::size_t fullSizeAsset = assetForCloud(
                        secondary.ageSeconds, secondary.opacityStrength, secondary.cloudId);
                    const std::size_t fullSizeClass = fullSizeAsset / 2u;
                    const std::size_t smallerClass = fullSizeClass > 0u ? fullSizeClass - 1u : 0u;
                    secondary.assetIndex = smallerClass * 2u;
                    const double recoverLateral = std::clamp(
                        -0.28 * static_cast<double>(sample.wakeDisplacementLateralM), -2.0, 2.0);
                    const double recoverVertical = std::clamp(
                        -0.62 * static_cast<double>(sample.wakeDisplacementVerticalM), -4.0, 4.0);
                    secondary.position = {
                        sample.localPositionM.x + side.x * recoverLateral + up.x * recoverVertical,
                        sample.localPositionM.y + side.y * recoverLateral + up.y * recoverVertical,
                        sample.localPositionM.z + side.z * recoverLateral + up.z * recoverVertical
                    };
                    candidates[secondary.assetIndex][static_cast<std::size_t>(Layer::Swirl)].push_back(secondary);
                }
            }'''

r = rx(
    r,
    r'''            // v6\.5 organized vortex roll-up\..*?candidates\[swirl\.assetIndex\]\[static_cast<std::size_t>\(Layer::Swirl\)\]\.push_back\(swirl\);\s*            \}''',
    physics_rollup,
    "physics-coupled roll-up replacement",
)

r = r.replace("Renderer Foundation v6.6", "Renderer Foundation v6.7")
r = r.replace("Renderer v6.6", "Renderer v6.7")
r = r.replace("smooth stable micro-cutout vortex 3-D field", "physics-coupled vortex roll-up 3-D field")
r = r.replace("smooth micro-cutout ice-white 3-D morphology assets", "physics-coupled ice-white 3-D morphology assets")

for token in (
    "sample.wakeInitialized", "sample.wakeVortexPhaseRad", "sample.wakeCirculationM2ps",
    "vortexArmId", "secondaryWakeId", "emitArm(1ULL", "recoverVertical",
    "budget * 0.52", "budget * 0.30", "52, 152, 176, 160, 190, 142, 144, 8",
):
    if token not in r:
        raise RuntimeError(f"v6.7 renderer integration missing: {token}")
RENDERER.write_text(r, encoding="utf-8", newline="\n")


# Final cross-file contract checks.
for path, tokens in (
    (PLANNER_H, ("wakeVortexPhaseRad", "wakeDisplacementVerticalM", "wakeInitialized")),
    (PLANNER_CPP, ("mixAngleRadians", "sample.wakeVortexPhaseRad", "sample.wakeInitialized")),
    (PLUGIN, ("PHYSICS_COUPLED_VORTEX_ROLLUP", "vortex_sampling_mode=PHYSICS_COUPLED_ARMS", "item.wakeVortexPhaseRad")),
    (RENDERER, ("vortexArmId", "secondaryWakeId", "sample.wakeVortexPhaseRad")),
):
    text = path.read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            raise RuntimeError(f"{path.name}: missing final v6.7 token {token}")

print("Integrated Renderer Foundation v6.7 physics-coupled vortex roll-up")
