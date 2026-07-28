#!/usr/bin/env python3
"""Generate Renderer Foundation v6.9 elongated low-occupancy ice volumes.

The v6.9 geometry is intentionally long enough to overlap the lane-coherent
instance selection. It remains on the proven X-Plane native OBJ + binary-alpha
material path: pure-white RGB, micro-dither coverage, GLOBAL_no_blend 0.5.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 512
RINGS = 31
SIDES = 24
ALPHA_CUTOFF = 0.5

LANES = {
    "inner": {
        "seed": 69011, "half_length": 29.0, "radius": 1.65,
        "occupancy": 0.72, "vertical_scale": 0.88,
    },
    "core": {
        "seed": 69021, "half_length": 31.0, "radius": 2.00,
        "occupancy": 0.80, "vertical_scale": 0.92,
    },
    "outer": {
        "seed": 69031, "half_length": 34.0, "radius": 2.75,
        "occupancy": 0.61, "vertical_scale": 0.86,
    },
    "secondary": {
        "seed": 69041, "half_length": 36.0, "radius": 2.25,
        "occupancy": 0.54, "vertical_scale": 0.82,
    },
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def hash01(ix: int, iy: int, seed: int) -> float:
    value = (ix * 0x1F123BB5) ^ (iy * 0x5F356495) ^ seed
    value &= 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7FEB352D) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846CA68B) & 0xFFFFFFFF
    value ^= value >> 16
    return (value & 0x00FFFFFF) / float(0x01000000)


def smooth01(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def value_noise(x: float, y: float, frequency: float, seed: int) -> float:
    gx = x * frequency
    gy = y * frequency
    x0, y0 = math.floor(gx), math.floor(gy)
    tx, ty = smooth01(gx - x0), smooth01(gy - y0)
    a = hash01(x0, y0, seed)
    b = hash01(x0 + 1, y0, seed)
    c = hash01(x0, y0 + 1, seed)
    d = hash01(x0 + 1, y0 + 1, seed)
    ab = a + (b - a) * tx
    cd = c + (d - c) * tx
    return ab + (cd - ab) * ty


def write_texture(path: Path, seed: int, target_occupancy: float) -> float:
    scores = []
    for y in range(TEX_SIZE):
        v = (y + 0.5) / TEX_SIZE
        for x in range(TEX_SIZE):
            u = (x + 0.5) / TEX_SIZE
            low = value_noise(u, v, 11.0, seed ^ 0x1379)
            mid = value_noise(u, v, 39.0, seed ^ 0x2468)
            high = value_noise(u, v, 113.0, seed ^ 0x55AA)
            micro = 0.5 + 0.5 * math.sin(
                (u * 181.0 + v * 157.0) * math.tau + (seed & 31)
            )
            scores.append(0.12 * low + 0.30 * mid + 0.46 * high + 0.12 * micro)

    ordered = sorted(scores)
    transparent_fraction = max(0.0, min(1.0, 1.0 - target_occupancy))
    threshold_index = min(
        len(ordered) - 1,
        max(0, int(round(transparent_fraction * (len(ordered) - 1)))),
    )
    threshold = ordered[threshold_index]

    pixels = bytearray()
    opaque = 0
    for score in scores:
        alpha = 255 if score >= threshold else 0
        opaque += int(bool(alpha))
        pixels.extend((255, 255, 255, alpha))

    raw = bytearray()
    stride = TEX_SIZE * 4
    for y in range(TEX_SIZE):
        raw.append(0)
        raw.extend(pixels[y * stride:(y + 1) * stride])

    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(
        b"IHDR", struct.pack(">IIBBBBB", TEX_SIZE, TEX_SIZE, 8, 6, 0, 0, 0)
    )
    data += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)
    return opaque / float(TEX_SIZE * TEX_SIZE)


def build_shell(name: str, spec: dict):
    rng = random.Random(spec["seed"])
    half_length = spec["half_length"]
    base_radius = spec["radius"]
    vertical_scale = spec["vertical_scale"]
    phases = [rng.uniform(0.0, math.tau) for _ in range(6)]
    p1, p2, p3, p4, p5, p6 = phases

    vertices = []
    indices = []
    for iz in range(RINGS):
        t = iz / (RINGS - 1)
        z = -half_length + 2.0 * half_length * t
        # Feathered capsule: long overlap with soft tapered ends, but enough
        # axial irregularity to avoid a perfect rigid tube silhouette.
        axial = 0.09 + 0.91 * max(0.0, math.sin(math.pi * t)) ** 0.42
        axial *= (
            1.0
            + 0.085 * math.sin(t * math.tau * 2.0 + p1)
            + 0.040 * math.sin(t * math.tau * 4.0 + p3)
            + 0.020 * math.sin(t * math.tau * 7.0 + p5)
        )
        radius = base_radius * axial
        centre_x = base_radius * (
            0.12 * math.sin(t * math.tau * 1.3 + p2)
            + 0.045 * math.sin(t * math.tau * 3.1 + p6)
        )
        centre_y = base_radius * (
            0.09 * math.cos(t * math.tau * 1.7 + p4)
            + 0.035 * math.sin(t * math.tau * 3.8 + p1)
        )

        for side in range(SIDES):
            phi = math.tau * side / SIDES
            angular = (
                1.0
                + 0.11 * math.sin(phi * 3.0 + p1 + t * 2.1)
                + 0.055 * math.sin(phi * 5.0 + p3 - t * 1.5)
                + 0.025 * math.sin(phi * 8.0 + p5 + t * 3.2)
            )
            angular *= rng.uniform(0.985, 1.015)
            rx = radius * angular
            ry = radius * vertical_scale * angular
            cp, sp = math.cos(phi), math.sin(phi)
            x = centre_x + rx * cp
            y = centre_y + ry * sp
            nx = cp / max(abs(rx), 1.0e-4)
            ny = sp / max(abs(ry), 1.0e-4)
            normal_length = math.sqrt(nx * nx + ny * ny) or 1.0
            vertices.append((
                x, y, z,
                nx / normal_length, ny / normal_length, 0.0,
                side / SIDES, t,
            ))

    start_cap = len(vertices)
    vertices.append((0.0, 0.0, -half_length, 0.0, 0.0, -1.0, 0.5, 0.0))
    end_cap = len(vertices)
    vertices.append((0.0, 0.0, half_length, 0.0, 0.0, 1.0, 0.5, 1.0))

    for iz in range(RINGS - 1):
        for side in range(SIDES):
            a = iz * SIDES + side
            b = iz * SIDES + ((side + 1) % SIDES)
            c = (iz + 1) * SIDES + side
            d = (iz + 1) * SIDES + ((side + 1) % SIDES)
            indices.extend((a, c, b, b, c, d))
    for side in range(SIDES):
        nxt = (side + 1) % SIDES
        indices.extend((start_cap, nxt, side))
        a = (RINGS - 1) * SIDES + side
        b = (RINGS - 1) * SIDES + nxt
        indices.extend((end_cap, a, b))

    return vertices, indices


def build_obj(name: str, spec: dict) -> str:
    vertices, indices = build_shell(name, spec)
    lines = [
        "I", "800", "OBJ",
        f"# FFAtmo Renderer Foundation v6.9 Lagrangian vortex-sheet {name} ice volume",
        f"TEXTURE contrail_v69_{name}.png",
        "GLOBAL_no_shadow",
        f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}",
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
        raise RuntimeError(f"{name}: forbidden renderer/material state")
    if f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}" not in obj:
        raise RuntimeError(f"{name}: binary-alpha material state missing")
    vertex_count = sum(1 for line in obj.splitlines() if line.startswith("VT "))
    if vertex_count != RINGS * SIDES + 2:
        raise RuntimeError(f"{name}: unexpected shell vertex count {vertex_count}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    info = [
        "FFAtmo Renderer Foundation v6.9 Lagrangian Vortex Sheet assets",
        "Four lane-specific elongated native 3-D ice volumes.",
        "Pure white RGB + binary alpha + stochastic micro-dither coverage.",
    ]
    for name, spec in LANES.items():
        obj = build_obj(name, spec)
        validate(name, obj)
        obj_path = args.output_dir / f"contrail_v69_{name}.obj"
        png_path = args.output_dir / f"contrail_v69_{name}.png"
        obj_path.write_text(obj, encoding="utf-8", newline="\n")
        occupancy = write_texture(png_path, spec["seed"] ^ 0x69A5, spec["occupancy"])
        vertices, _ = build_shell(name, spec)
        length = max(v[2] for v in vertices) - min(v[2] for v in vertices)
        width = max(v[0] for v in vertices) - min(v[0] for v in vertices)
        height = max(v[1] for v in vertices) - min(v[1] for v in vertices)
        info.append(
            f"{name}: length={length:.2f}m width={width:.2f}m height={height:.2f}m "
            f"opaque_occupancy={occupancy:.4f}"
        )

    (args.output_dir / "V69_ASSET_INFO.txt").write_text(
        "\n".join(info) + "\n", encoding="utf-8"
    )
    print("\n".join(info))


if __name__ == "__main__":
    main()
