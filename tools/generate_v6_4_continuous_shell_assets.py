#!/usr/bin/env python3
"""Generate v6.4 single-surface porous 3-D contrail cloudlets.

v6.3 used several overlapping closed lobes inside every instance. With alpha
blending that stacked multiple surfaces and produced the dark linked-bead look
seen in the in-sim test. v6.4 uses one irregular closed shell per instance,
keeps normal back-face culling, and lowers shell alpha so neighbouring objects
can overlap into a cloud field without becoming opaque knots.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 256
RINGS = 15
SIDES = 18

AGE_CLASSES = {
    "near":   {"seed": 64010, "half_length": 3.4,  "radius": 0.78, "soft_alpha": (4, 24), "dense_alpha": (9, 34)},
    "young":  {"seed": 64110, "half_length": 5.5,  "radius": 1.48, "soft_alpha": (5, 27), "dense_alpha": (11, 39)},
    "mature": {"seed": 64210, "half_length": 8.2,  "radius": 2.62, "soft_alpha": (5, 26), "dense_alpha": (10, 37)},
    "old":    {"seed": 64310, "half_length": 11.8, "radius": 4.05, "soft_alpha": (4, 23), "dense_alpha": (8, 33)},
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload)) + kind + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_texture(path: Path, seed: int, alpha_base: int, alpha_range: int, soft: bool) -> None:
    rng = random.Random(seed)
    blobs = [
        (rng.uniform(-1.0, 1.0), rng.uniform(-1.0, 1.0),
         rng.uniform(0.18, 0.52), rng.uniform(0.16, 0.48), rng.uniform(0.07, 0.22))
        for _ in range(24)
    ]
    pixels = bytearray()
    for y in range(TEX_SIZE):
        py = 2.0 * (y + 0.5) / TEX_SIZE - 1.0
        for x in range(TEX_SIZE):
            px = 2.0 * (x + 0.5) / TEX_SIZE - 1.0
            density = 0.0
            for cx, cy, sx, sy, weight in blobs:
                dx = (px - cx) / sx
                dy = (py - cy) / sy
                density += weight * math.exp(-0.5 * (dx * dx + dy * dy))
            broad = 0.52 + 0.17 * math.sin(px * 5.7 + py * 4.3) + 0.09 * math.sin(px * 12.0 - py * 9.0)
            fine = 0.5 + 0.5 * math.sin(px * 23.0 + math.sin(py * 8.0) * 1.9)
            response = max(0.0, min(1.0, 0.20 + 0.40 * density + 0.16 * broad + 0.05 * fine))
            if soft:
                response *= 0.70 + 0.30 * max(0.0, math.sin(px * 7.0 - py * 6.0 + 0.4))
            alpha = int(round(alpha_base + alpha_range * response))
            # Ice-cloud albedo. Keep RGB bright; opacity, not dark pigment, forms density.
            pixels.extend((248, 251, 255, max(0, min(255, alpha))))

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


def shell_parameters(spec: dict, variant: str):
    seed = spec["seed"] + (117 if variant == "soft" else 231)
    rng = random.Random(seed)
    half_length = spec["half_length"] * (1.05 if variant == "soft" else 0.98)
    base_radius = spec["radius"] * (0.93 if variant == "soft" else 1.04)
    phase1 = rng.uniform(0.0, math.tau)
    phase2 = rng.uniform(0.0, math.tau)
    phase3 = rng.uniform(0.0, math.tau)
    return rng, half_length, base_radius, phase1, phase2, phase3


def build_shell(spec: dict, variant: str):
    rng, half_length, base_radius, phase1, phase2, phase3 = shell_parameters(spec, variant)
    vertices = []
    indices = []

    # Rings include small but non-zero end radii; dedicated cap vertices close the shell.
    for iz in range(RINGS):
        t = iz / (RINGS - 1)
        z = -half_length + 2.0 * half_length * t
        axial = math.sin(math.pi * t)
        axial = 0.10 + 0.90 * max(0.0, axial) ** 0.62
        bulge = 1.0 + 0.16 * math.sin(t * math.tau * 2.0 + phase1) + 0.08 * math.sin(t * math.tau * 3.0 + phase2)
        ring_radius = base_radius * axial * bulge
        centre_x = base_radius * 0.17 * math.sin(t * math.tau * 1.5 + phase2)
        centre_y = base_radius * 0.13 * math.cos(t * math.tau * 1.8 + phase3)
        for side in range(SIDES):
            phi = math.tau * side / SIDES
            angular = 1.0 + 0.10 * math.sin(phi * 3.0 + phase1 + t * 2.1) + 0.055 * math.sin(phi * 5.0 + phase3)
            angular *= rng.uniform(0.975, 1.025)
            rx = ring_radius * angular
            ry = ring_radius * (0.88 + 0.08 * math.sin(t * math.tau + phase2)) * angular
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

    # Close each end with one cap vertex; default back-face culling is intentional.
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
        f"# FFAtmo Renderer Foundation v6.4 single-surface porous 3-D {name} cloudlet",
        f"TEXTURE contrail_cloudlet_{name}.png",
        "GLOBAL_no_shadow",
        f"POINT_COUNTS {len(vertices)} 0 0 {len(indices)}",
    ]
    for vertex in vertices:
        lines.append("VT " + " ".join(f"{value:.6f}" for value in vertex))
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
        # No ATTR_no_cull: a closed transparent shell must not draw its back face too.
        "ATTR_blend",
        "ATTR_shiny_rat 0.0",
        f"TRIS 0 {len(indices)}",
        "",
    ]
    return "\n".join(lines)


def validate(name: str, obj: str) -> None:
    forbidden = ("PARTICLE_SYSTEM", "EMITTER", "BILLBOARD", "ANIM_billboard", "ATTR_no_cull")
    if any(token in obj for token in forbidden):
        raise RuntimeError(f"{name}: forbidden render primitive/state returned")
    if "ATTR_blend" not in obj or "GLOBAL_no_shadow" not in obj or "TRIS 0 " not in obj:
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
        "FFAtmo Renderer Foundation v6.4 continuous single-surface 3-D cloudlets",
        "One closed irregular shell per instance; default back-face culling retained.",
        "Lower alpha is intentional to avoid v6.3 dark multi-surface knots.",
    ]
    for age, spec in AGE_CLASSES.items():
        for variant in ("soft", "dense"):
            name = f"{age}_{variant}"
            obj = build_obj(age, variant, spec)
            validate(name, obj)
            (args.output_dir / f"contrail_cloudlet_{name}.obj").write_text(obj, encoding="utf-8", newline="\n")
            alpha_base, alpha_range = spec[f"{variant}_alpha"]
            write_texture(
                args.output_dir / f"contrail_cloudlet_{name}.png",
                spec["seed"] + (313 if variant == "soft" else 419),
                alpha_base,
                alpha_range,
                variant == "soft",
            )
            vertices, _ = build_shell(spec, variant)
            z_extent = max(v[2] for v in vertices) - min(v[2] for v in vertices)
            x_extent = max(v[0] for v in vertices) - min(v[0] for v in vertices)
            info.append(f"{name}: length={z_extent:.3f} m width={x_extent:.3f} m")

    (args.output_dir / "ASSET_INFO.txt").write_text("\n".join(info) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
