#!/usr/bin/env python3
"""Generate v6.5 ice-white alpha-tested native 3-D contrail cloudlets.

The v6.4 in-sim test proved that translucent OBJ shells still interact badly
with X-Plane haze/depth ordering. v6.5 deliberately avoids translucent blending:
the texture is a binary porous ice mask and GLOBAL_no_blend performs alpha
cutout. Surviving fragments are opaque ice-white; low-density holes are fully
discarded.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 256
RINGS = 17
SIDES = 20
ALPHA_CUTOFF = 0.5

AGE_CLASSES = {
    "near":   {"seed": 65010, "half_length": 3.6,  "radius": 0.72, "soft_threshold": 0.61, "dense_threshold": 0.53},
    "young":  {"seed": 65110, "half_length": 5.8,  "radius": 1.42, "soft_threshold": 0.58, "dense_threshold": 0.49},
    "mature": {"seed": 65210, "half_length": 8.6,  "radius": 2.58, "soft_threshold": 0.57, "dense_threshold": 0.47},
    "old":    {"seed": 65310, "half_length": 12.2, "radius": 4.00, "soft_threshold": 0.60, "dense_threshold": 0.50},
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload)) + kind + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_texture(path: Path, seed: int, threshold: float, soft: bool) -> float:
    """Write coherent 0/255 alpha islands; return opaque occupancy ratio."""
    rng = random.Random(seed)
    blobs = [
        (
            rng.uniform(-1.05, 1.05), rng.uniform(-1.05, 1.05),
            rng.uniform(0.18, 0.55), rng.uniform(0.16, 0.50),
            rng.uniform(0.12, 0.32),
        )
        for _ in range(30)
    ]
    phase_a = rng.uniform(0.0, math.tau)
    phase_b = rng.uniform(0.0, math.tau)
    pixels = bytearray()
    opaque = 0
    total = TEX_SIZE * TEX_SIZE

    for y in range(TEX_SIZE):
        py = 2.0 * (y + 0.5) / TEX_SIZE - 1.0
        for x in range(TEX_SIZE):
            px = 2.0 * (x + 0.5) / TEX_SIZE - 1.0
            density = 0.0
            for cx, cy, sx, sy, weight in blobs:
                dx = (px - cx) / sx
                dy = (py - cy) / sy
                density += weight * math.exp(-0.5 * (dx * dx + dy * dy))
            density = min(density / 1.45, 1.0)
            broad = (
                0.50
                + 0.22 * math.sin(px * 4.8 + py * 4.0 + phase_a)
                + 0.14 * math.sin(px * 9.2 - py * 7.3 + phase_b)
            )
            medium = 0.5 + 0.5 * math.sin(px * 17.0 + math.sin(py * 8.5 + phase_a) * 1.7)
            value = 0.48 * density + 0.34 * broad + 0.18 * medium

            # Soft variants expose more empty space and a slightly ragged mask.
            if soft:
                value -= 0.045 * (0.5 + 0.5 * math.sin(px * 13.0 - py * 11.0 + phase_b))
            alpha = 255 if value >= threshold else 0
            if alpha:
                opaque += 1
            # Pure ice-white albedo. Lighting may shade the physical surface,
            # but no dark RGB pigment or semi-transparent haze contribution exists.
            pixels.extend((255, 255, 255, alpha))

    raw = bytearray()
    stride = TEX_SIZE * 4
    for y in range(TEX_SIZE):
        raw.append(0)
        raw.extend(pixels[y * stride:(y + 1) * stride])
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", TEX_SIZE, TEX_SIZE, 8, 6, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)
    return opaque / float(total)


def shell_parameters(spec: dict, variant: str):
    seed = spec["seed"] + (117 if variant == "soft" else 231)
    rng = random.Random(seed)
    half_length = spec["half_length"] * (1.06 if variant == "soft" else 0.99)
    base_radius = spec["radius"] * (0.94 if variant == "soft" else 1.04)
    phases = [rng.uniform(0.0, math.tau) for _ in range(4)]
    return rng, half_length, base_radius, phases


def build_shell(spec: dict, variant: str):
    rng, half_length, base_radius, phases = shell_parameters(spec, variant)
    p1, p2, p3, p4 = phases
    vertices = []
    indices = []

    for iz in range(RINGS):
        t = iz / (RINGS - 1)
        z = -half_length + 2.0 * half_length * t
        axial = 0.09 + 0.91 * max(0.0, math.sin(math.pi * t)) ** 0.60
        bulge = (
            1.0
            + 0.14 * math.sin(t * math.tau * 2.0 + p1)
            + 0.075 * math.sin(t * math.tau * 3.0 + p2)
            + 0.035 * math.sin(t * math.tau * 5.0 + p4)
        )
        ring_radius = base_radius * axial * bulge
        centre_x = base_radius * (
            0.13 * math.sin(t * math.tau * 1.45 + p2)
            + 0.045 * math.sin(t * math.tau * 3.6 + p4)
        )
        centre_y = base_radius * (
            0.10 * math.cos(t * math.tau * 1.7 + p3)
            + 0.035 * math.sin(t * math.tau * 4.1 + p1)
        )
        for side in range(SIDES):
            phi = math.tau * side / SIDES
            angular = (
                1.0
                + 0.105 * math.sin(phi * 3.0 + p1 + t * 2.1)
                + 0.060 * math.sin(phi * 5.0 + p3 - t * 1.4)
                + 0.030 * math.sin(phi * 7.0 + p4)
            )
            angular *= rng.uniform(0.98, 1.02)
            rx = ring_radius * angular
            ry = ring_radius * (0.89 + 0.075 * math.sin(t * math.tau + p2)) * angular
            cp, sp = math.cos(phi), math.sin(phi)
            x = centre_x + rx * cp
            y = centre_y + ry * sp
            nx, ny = cp / max(rx, 1.0e-4), sp / max(ry, 1.0e-4)
            m = math.sqrt(nx * nx + ny * ny) or 1.0
            vertices.append((x, y, z, nx / m, ny / m, 0.0, side / SIDES, t))

    for iz in range(RINGS - 1):
        for side in range(SIDES):
            a = iz * SIDES + side
            b = iz * SIDES + ((side + 1) % SIDES)
            c = (iz + 1) * SIDES + side
            d = (iz + 1) * SIDES + ((side + 1) % SIDES)
            indices.extend((a, c, b, b, c, d))

    start_cap = len(vertices)
    vertices.append((0.0, 0.0, -half_length, 0.0, 0.0, -1.0, 0.5, 0.0))
    end_cap = len(vertices)
    vertices.append((0.0, 0.0, half_length, 0.0, 0.0, 1.0, 0.5, 1.0))
    for side in range(SIDES):
        nxt = (side + 1) % SIDES
        indices.extend((start_cap, nxt, side))
        a = (RINGS - 1) * SIDES + side
        b = (RINGS - 1) * SIDES + nxt
        indices.extend((end_cap, a, b))
    return vertices, indices


def build_obj(age: str, variant: str, spec: dict) -> str:
    vertices, indices = build_shell(spec, variant)
    name = f"{age}_{variant}"
    lines = [
        "I",
        "800",
        "OBJ",
        f"# FFAtmo Renderer Foundation v6.5 ice-white alpha-test 3-D {name} cloudlet",
        f"TEXTURE contrail_cloudlet_{name}.png",
        "GLOBAL_no_shadow",
        f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}",
        f"POINT_COUNTS {len(vertices)} 0 0 {len(indices)}",
    ]
    for vertex in vertices:
        lines.append("VT " + " ".join(f"{v:.6f}" for v in vertex))
    cursor = 0
    while cursor + 10 <= len(indices):
        lines.append("IDX10 " + " ".join(str(v) for v in indices[cursor:cursor + 10]))
        cursor += 10
    while cursor < len(indices):
        lines.append(f"IDX {indices[cursor]}")
        cursor += 1
    lines += [
        "ATTR_no_shadow",
        "ATTR_shade_smooth",
        "ATTR_shiny_rat 0.0",
        f"TRIS 0 {len(indices)}",
        "",
    ]
    return "\n".join(lines)


def validate(name: str, obj: str) -> None:
    forbidden = (
        "PARTICLE_SYSTEM", "EMITTER", "BILLBOARD", "ANIM_billboard",
        "ATTR_no_cull", "ATTR_blend", "BLEND_GLASS", "TEXTURE_LIT",
    )
    if any(token in obj for token in forbidden):
        raise RuntimeError(f"{name}: forbidden translucent/particle state returned")
    if f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}" not in obj:
        raise RuntimeError(f"{name}: alpha-test material state missing")
    if "GLOBAL_no_shadow" not in obj or "TRIS 0 " not in obj:
        raise RuntimeError(f"{name}: native 3-D state missing")
    vt = sum(1 for line in obj.splitlines() if line.startswith("VT "))
    if vt != RINGS * SIDES + 2:
        raise RuntimeError(f"{name}: unexpected single-shell vertex count {vt}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    info = [
        "FFAtmo Renderer Foundation v6.5 ice-white alpha-test vortex cloud field",
        "GLOBAL_no_blend 0.5: no translucent OBJ blending; binary porous alpha mask.",
        "Primary volumes stay on the fluid centreline; vortex displacement is runtime-only.",
    ]
    occupancies = {}
    for age, spec in AGE_CLASSES.items():
        for variant in ("soft", "dense"):
            name = f"{age}_{variant}"
            obj = build_obj(age, variant, spec)
            validate(name, obj)
            (args.output_dir / f"contrail_cloudlet_{name}.obj").write_text(obj, encoding="utf-8", newline="\n")
            occupancy = write_texture(
                args.output_dir / f"contrail_cloudlet_{name}.png",
                spec["seed"] + (313 if variant == "soft" else 419),
                spec[f"{variant}_threshold"],
                variant == "soft",
            )
            occupancies[name] = occupancy
            vertices, _ = build_shell(spec, variant)
            z_extent = max(v[2] for v in vertices) - min(v[2] for v in vertices)
            x_extent = max(v[0] for v in vertices) - min(v[0] for v in vertices)
            info.append(
                f"{name}: length={z_extent:.3f} m width={x_extent:.3f} m opaque_occupancy={occupancy:.4f}"
            )

    for age in AGE_CLASSES:
        if not occupancies[f"{age}_soft"] < occupancies[f"{age}_dense"]:
            raise RuntimeError(f"{age}: soft mask must be more porous than dense mask")
    if not all(0.12 <= value <= 0.88 for value in occupancies.values()):
        raise RuntimeError(f"Cutout occupancy outside useful range: {occupancies}")

    (args.output_dir / "ASSET_INFO.txt").write_text("\n".join(info) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
