#!/usr/bin/env python3
"""Apply the deterministic Renderer Foundation v5.2 source tuning."""

from __future__ import annotations

from pathlib import Path


def replace_required(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        print(f"{label}: already applied")
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def patch_renderer() -> None:
    path = Path("src/ContrailParticleRenderer.h")
    text = path.read_text(encoding="utf-8-sig")
    for old in ("v5.1.2", "v5.1.1", "v5.0.1", "v5.1", "v5.0"):
        text = text.replace(old, "v5.2")
    text = replace_required(
        text,
        "static constexpr std::size_t kVisibleCapacity = 1536;",
        "static constexpr std::size_t kVisibleCapacity = 4096;",
        "diagnostic wake capacity",
    )
    text = replace_required(
        text,
        '''        const float normalizedSize = std::clamp(
                sample.widthM / 4.0f,
                0.08f,
                1.0f);
            const float normalizedAlpha = std::clamp(
                0.38f + sample.opacityStrength * 2.5f,
                0.38f,
                1.0f);
            const float values[] = {
                1.0f,
                normalizedSize,
                normalizedAlpha,
                0.55f
            };
''',
        '''        // v5.2 keeps the proven single native ribbon, but gives the
            // formation zone enough width and optical weight to read as ice
            // cloud rather than a thin wire. All controls stay inside the
            // normalized 0..1 particle-dataref domain.
            const float normalizedSize = std::clamp(
                0.68f + sample.widthM / 9.0f,
                0.68f,
                0.98f);
            const float normalizedAlpha = std::clamp(
                0.70f + sample.opacityStrength * 1.35f,
                0.70f,
                0.96f);
            const float values[] = {
                1.0f,
                normalizedSize,
                normalizedAlpha,
                0.78f
            };
''',
        "v5.2 instance shaping",
    )
    if "XPLMLoadObjectAsync(" in text:
        raise RuntimeError("unsafe asynchronous startup loader returned")
    if "deferredNonEmptyFrameCount_ < 3" not in text:
        raise RuntimeError("safe-start gate was removed")
    path.write_text(text, encoding="utf-8", newline="\n")


def patch_plugin() -> None:
    path = Path("src/ContrailDebugPlugin.cpp")
    text = path.read_text(encoding="utf-8-sig")
    for old, new in (
        ("v5.1.2", "v5.2"),
        ("V5.1.2", "V5.2"),
        ("v5.1.1", "v5.2"),
        ("V5.1.1", "V5.2"),
        ("v5.0.1", "v5.2"),
        ("V5.0.1", "V5.2"),
        ("v5.1", "v5.2"),
        ("V5.1", "V5.2"),
        ("v5.0", "v5.2"),
        ("V5.0", "V5.2"),
        ("v5 point 1 point 2", "v5 point 2"),
        ("v5 point 1", "v5 point 2"),
        ("v5 point 0 point 1", "v5 point 2"),
        ("v5 point 0", "v5 point 2"),
    ):
        text = text.replace(old, new)
    text = replace_required(
        text,
        '''        renderPlannerSettings_.assetCapacities.fill(
                ContrailParticleRenderer::kInstancesPerAsset);
''',
        '''        // The particle renderer consumes the youngest sample per
            // engine, while the full wake plan remains available for v5.3
            // swirl/handoff diagnostics.
            renderPlannerSettings_.assetCapacities.fill(512);
''',
        "wake planner diagnostic budget",
    )
    text = text.replace(
        "Check the eight assets folder.",
        "Check the native particle assets folder.",
    )
    if "WORLD V5.2 READY" not in text:
        raise RuntimeError("v5.2 overlay label was not produced")
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    patch_renderer()
    patch_plugin()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
