#!/usr/bin/env python3
"""Generate v6.6 smooth micro-cutout native 3-D contrail cloudlets.

v6.5 proved GLOBAL_no_blend removes the dark haze interaction, but the large
coherent alpha islands read as torn foam/Swiss cheese. v6.6 keeps binary alpha
and pure white RGB while moving porosity to much finer spatial scales, smoothing
the shell mesh, and reducing macro surface distortion.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 512
RINGS = 25
SIDES = 24
ALPHA_CUTOFF = 0.5

AGE_CLASSES = {
    "near":   {"seed": 66010, "half_length": 3.25, "radius": 0.60, "soft_occupancy": 0.76, "dense_occupancy": 0.86},
    "young":  {"seed": 66110, "half_length": 5.20, "radius": 1.18, "soft_occupancy": 0.78, "dense_occupancy": 0.88},
    "mature": {"seed": 66210, "half_length": 7.80, "radius": 2.10, "soft_occupancy": 0.80, "dense_occupancy": 0.90},
    "old":    {"seed": 66310, "half_length": 10.80, "radius": 3.25, "soft_occupancy": 0.72, "dense_occupancy": 0.84},
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def hash01(ix: int, iy: int, seed: int) -> float:
    value = (ix * 0x1f123bb5) ^ (iy * 0x5f356495) ^ seed
    value &= 0xFFFFFFFF
    value ^= value >> 16
    value = (value * 0x7feb352d) & 0xFFFFFFFF
    value ^= value >> 15
    value = (value * 0x846ca68b) & 0xFFFFFFFF
    value ^= value >> 16
    return (value & 0x00FFFFFF) / float(0x01000000)


def smooth01(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def value_noise(x: float, y: float, frequency: float, seed: int) -> float:
    gx = x * frequency
    gy = y * frequency
    x0 = math.floor(gx)
    y0 = math.floor(gy)
    tx = smooth01(gx - x0)
    ty = smooth01(gy - y0)
    a = hash01(x0, y0, seed)
    b = hash01(x0 + 1, y0, seed)
    c = hash01(x0, y0 + 1, seed)
    d = hash01(x0 + 1, y0 + 1, seed)
    ab = a + (b - a) * tx
    cd = c + (d - c) * tx
    return ab + (cd - ab) * ty


def write_texture(path: Path, seed: int, target_occupancy: float) -> float:
    """Write a pure-white binary alpha mask with fine multi-scale porosity."""
    scores = []
    for y in range(TEX_SIZE):
        v = (y + 0.5) / TEX_SIZE
        for x in range(TEX_SIZE):
            u = (x + 0.5) / TEX_SIZE
            # Deliberately favour medium/fine structure. v6.5 gave too much weight
            # to broad coherent fields, producing large visible holes.
            n0 = value_noise(u, v, 9.0, seed ^ 0x13579)
            n1 = value_noise(u, v, 27.0, seed ^ 0x2468A)
            n2 = value_noise(u, v, 71.0, seed ^ 0x55AA5)
            micro = 0.5 + 0.5 * math.sin((u * 113.0 + v * 89.0) * math.tau + (seed & 31))
            scores.append(0.14 * n0 + 0.34 * n1 + 0.42 * n2 + 0.10 * micro)

    ordered = sorted(scores)
    transparent_fraction = max(0.0, min(1.0, 1.0 - target_occupancy))
    threshold_index = min(len(ordered) - 1, max(0, int(round(transparent_fraction * (len(ordered) - 1)))))
    threshold = ordered[threshold_index]

    pixels = bytearray()
    opaque = 0
    for score in scores:
        alpha = 255 if score >= threshold else 0
        opaque += 1 if alpha else 0
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


def shell_parameters(spec: dict, variant: str):
    seed = spec["seed"] + (117 if variant == "soft" else 231)
    rng = random.Random(seed)
    half_length = spec["half_length"] * (1.035 if variant == "soft" else 1.0)
    base_radius = spec["radius"] * (0.96 if variant == "soft" else 1.035)
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
        axial = 0.12 + 0.88 * max(0.0, math.sin(math.pi * t)) ** 0.72
        bulge = 1.0 + 0.075 * math.sin(t * math.tau * 2.0 + p1) + 0.035 * math.sin(t * math.tau * 3.0 + p2) + 0.015 * math.sin(t * math.tau * 5.0 + p4)
        ring_radius = base_radius * axial * bulge
        centre_x = base_radius * (0.055 * math.sin(t * math.tau * 1.45 + p2) + 0.020 * math.sin(t * math.tau * 3.6 + p4))
        centre_y = base_radius * (0.045 * math.cos(t * math.tau * 1.7 + p3) + 0.016 * math.sin(t * math.tau * 4.1 + p1))

        for side in range(SIDES):
            phi = math.tau * side / SIDES
            angular = 1.0 + 0.055 * math.sin(phi * 3.0 + p1 + t * 1.8) + 0.030 * math.sin(phi * 5.0 + p3 - t * 1.2) + 0.014 * math.sin(phi * 7.0 + p4)
            angular *= rng.uniform(0.992, 1.008)
            rx = ring_radius * angular
            ry = ring_radius * (0.93 + 0.035 * math.sin(t * math.tau + p2)) * angular
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
        "I", "800", "OBJ",
        f"# FFAtmo Renderer Foundation v6.6 smooth micro-cutout 3-D {name} cloudlet",
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
    lines += ["ATTR_no_shadow", "ATTR_shade_smooth", "ATTR_shiny_rat 0.0", f"TRIS 0 {len(indices)}", ""]
    return "\n".join(lines)


def validate(name: str, obj: str) -> None:
    forbidden = ("PARTICLE_SYSTEM", "EMITTER", "BILLBOARD", "ANIM_billboard", "ATTR_no_cull", "ATTR_blend", "BLEND_GLASS", "TEXTURE_LIT")
    if any(token in obj for token in forbidden):
        raise RuntimeError(f"{name}: forbidden translucent/particle state returned")
    if f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}" not in obj:
        raise RuntimeError(f"{name}: alpha-test state missing")
    vt = sum(1 for line in obj.splitlines() if line.startswith("VT "))
    if vt != RINGS * SIDES + 2:
        raise RuntimeError(f"{name}: unexpected shell vertex count {vt}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    info = [
        "FFAtmo Renderer Foundation v6.6 smooth micro-cutout ice cloud field",
        "Keeps GLOBAL_no_blend 0.5 and pure-white binary alpha from successful v6.5 optical workaround.",
        "Porosity moved to medium/fine scales; shell macro distortion reduced; mesh resolution increased.",
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
                spec[f"{variant}_occupancy"],
            )
            occupancies[name] = occupancy
            vertices, _ = build_shell(spec, variant)
            z_extent = max(v[2] for v in vertices) - min(v[2] for v in vertices)
            x_extent = max(v[0] for v in vertices) - min(v[0] for v in vertices)
            info.append(f"{name}: length={z_extent:.3f} m width={x_extent:.3f} m opaque_occupancy={occupancy:.4f}")

    for age in AGE_CLASSES:
        if not occupancies[f"{age}_soft"] < occupancies[f"{age}_dense"]:
            raise RuntimeError(f"{age}: soft mask must remain more porous than dense mask")
    if not all(0.68 <= value <= 0.92 for value in occupancies.values()):
        raise RuntimeError(f"v6.6 occupancy outside compact-cloud range: {occupancies}")

    (args.output_dir / "ASSET_INFO.txt").write_text("\n".join(info) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
