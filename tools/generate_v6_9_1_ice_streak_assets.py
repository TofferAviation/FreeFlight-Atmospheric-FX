#!/usr/bin/env python3
"""Generate v6.9.1 curvature-safe Lagrangian vortex-sheet streak assets.

The first v6.9 simulator build used 58-72 m rigid OBJ bodies. That was invalid for
wake lanes whose local tangent can change by tens of degrees between markers: the
objects overlapped across bends and produced giant opaque walls. v6.9.1 keeps the
same binary-alpha XPLMInstance material path but makes every rendered body local
in scale. Broad roll-up must come from marker separation, never from asset size.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 512
RINGS = 23
SIDES = 20
ALPHA_CUTOFF = 0.5

# Total lengths are 14/16/18/14 m. Maximum diameter stays close to 1 m.
# These bodies may overlap along a straight lane, but cannot bridge an entire
# curved wake section the way the rejected 58-72 m v6.9-R0 assets did.
LANES = {
    "inner": {
        "seed": 69111, "half_length": 7.0, "radius": 0.32,
        "occupancy": 0.32, "vertical_scale": 0.86,
    },
    "core": {
        "seed": 69121, "half_length": 8.0, "radius": 0.42,
        "occupancy": 0.42, "vertical_scale": 0.90,
    },
    "outer": {
        "seed": 69131, "half_length": 9.0, "radius": 0.55,
        "occupancy": 0.24, "vertical_scale": 0.84,
    },
    "secondary": {
        "seed": 69141, "half_length": 7.0, "radius": 0.38,
        "occupancy": 0.18, "vertical_scale": 0.80,
    },
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload)) + kind + payload
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
    gx, gy = x * frequency, y * frequency
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
            low = value_noise(u, v, 15.0, seed ^ 0x1379)
            mid = value_noise(u, v, 47.0, seed ^ 0x2468)
            high = value_noise(u, v, 131.0, seed ^ 0x55AA)
            micro = 0.5 + 0.5 * math.sin(
                (u * 193.0 + v * 173.0) * math.tau + (seed & 31)
            )
            scores.append(0.10 * low + 0.28 * mid + 0.50 * high + 0.12 * micro)

    ordered = sorted(scores)
    transparent_fraction = max(0.0, min(1.0, 1.0 - target_occupancy))
    threshold = ordered[min(
        len(ordered) - 1,
        max(0, int(round(transparent_fraction * (len(ordered) - 1)))),
    )]

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
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", TEX_SIZE, TEX_SIZE, 8, 6, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)
    return opaque / float(TEX_SIZE * TEX_SIZE)


def build_shell(spec: dict):
    rng = random.Random(spec["seed"])
    half_length = spec["half_length"]
    base_radius = spec["radius"]
    vertical_scale = spec["vertical_scale"]
    phases = [rng.uniform(0.0, math.tau) for _ in range(5)]
    p1, p2, p3, p4, p5 = phases

    vertices = []
    indices = []
    for iz in range(RINGS):
        t = iz / (RINGS - 1)
        z = -half_length + 2.0 * half_length * t
        # Compact tapered streak. The object represents only the local tangent;
        # the wake-sheet breadth and curl come exclusively from marker positions.
        axial = 0.08 + 0.92 * max(0.0, math.sin(math.pi * t)) ** 0.58
        axial *= (
            1.0
            + 0.065 * math.sin(t * math.tau * 2.0 + p1)
            + 0.028 * math.sin(t * math.tau * 4.0 + p3)
        )
        radius = base_radius * axial
        centre_x = base_radius * 0.055 * math.sin(t * math.tau * 1.7 + p2)
        centre_y = base_radius * 0.045 * math.cos(t * math.tau * 1.9 + p4)

        for side in range(SIDES):
            phi = math.tau * side / SIDES
            angular = (
                1.0
                + 0.075 * math.sin(phi * 3.0 + p1 + t * 1.6)
                + 0.030 * math.sin(phi * 5.0 + p5 - t * 1.2)
            ) * rng.uniform(0.992, 1.008)
            rx = radius * angular
            ry = radius * vertical_scale * angular
            cp, sp = math.cos(phi), math.sin(phi)
            x = centre_x + rx * cp
            y = centre_y + ry * sp
            nx = cp / max(abs(rx), 1.0e-4)
            ny = sp / max(abs(ry), 1.0e-4)
            nlen = math.sqrt(nx * nx + ny * ny) or 1.0
            vertices.append((x, y, z, nx / nlen, ny / nlen, 0.0, side / SIDES, t))

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
    vertices, indices = build_shell(spec)
    lines = [
        "I", "800", "OBJ",
        f"# FFAtmo v6.9.1 curvature-safe {name} vortex-sheet ice streak",
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
        "ATTR_no_shadow", "ATTR_shade_smooth", "ATTR_shiny_rat 0.0",
        f"TRIS 0 {len(indices)}", "",
    ]
    return "\n".join(lines)


def validate_geometry(name: str, obj: str) -> tuple[float, float, float]:
    forbidden = (
        "PARTICLE_SYSTEM", "EMITTER", "BILLBOARD", "ANIM_billboard",
        "ATTR_no_cull", "ATTR_blend", "BLEND_GLASS", "TEXTURE_LIT",
    )
    if any(token in obj for token in forbidden):
        raise RuntimeError(f"{name}: forbidden renderer/material state")
    if f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}" not in obj:
        raise RuntimeError(f"{name}: binary-alpha state missing")
    points = [line.split() for line in obj.splitlines() if line.startswith("VT ")]
    xs = [float(p[1]) for p in points]
    ys = [float(p[2]) for p in points]
    zs = [float(p[3]) for p in points]
    length = max(zs) - min(zs)
    width = max(xs) - min(xs)
    height = max(ys) - min(ys)
    if not (8.0 <= length <= 20.0):
        raise RuntimeError(f"{name}: unsafe rigid length {length:.2f} m")
    if width > 1.5 or height > 1.5:
        raise RuntimeError(f"{name}: unsafe rigid cross-section {width:.2f} x {height:.2f} m")
    return length, width, height


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    info = [
        "FFAtmo Renderer Foundation v6.9.1 curvature-safe visual hotfix",
        "Broad curl comes from Lagrangian marker positions, never oversized OBJ geometry.",
        "Rigid asset safety gate: length 8-20 m; cross-section <= 1.5 m.",
    ]
    for name, spec in LANES.items():
        obj = build_obj(name, spec)
        length, width, height = validate_geometry(name, obj)
        (args.output_dir / f"contrail_v69_{name}.obj").write_text(obj, encoding="utf-8", newline="\n")
        occupancy = write_texture(
            args.output_dir / f"contrail_v69_{name}.png",
            spec["seed"] ^ 0x6915,
            spec["occupancy"],
        )
        info.append(
            f"{name}: length={length:.2f}m width={width:.2f}m height={height:.2f}m "
            f"opaque_occupancy={occupancy:.4f}"
        )

    (args.output_dir / "V69_ASSET_INFO.txt").write_text("\n".join(info) + "\n", encoding="utf-8")
    print("\n".join(info))


if __name__ == "__main__":
    main()
