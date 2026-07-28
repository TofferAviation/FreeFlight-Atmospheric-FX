#!/usr/bin/env python3
"""Generate v6.7 compact, high-occupancy native 3-D ice-cloud assets.

v6.6 fixed the torn-foam look but still read as two long smooth ropes. v6.7 makes
individual cloudlets shorter and more cloud-like so the physics-coupled vortex
arms can visibly deform the wake cross-section instead of being swallowed by
long cigar-shaped geometry. We keep the successful X-Plane alpha-test material:
pure white RGB, binary alpha, GLOBAL_no_blend 0.5.
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 512
RINGS = 27
SIDES = 28
ALPHA_CUTOFF = 0.5

# Shorter longitudinal bodies and restrained cross-section growth. Continuity is
# provided by overlap/fill objects, not by a single giant torpedo mesh.
AGE_CLASSES = {
    "near":   {"seed": 67010, "half_length": 2.35, "radius": 0.40, "soft_occupancy": 0.90, "dense_occupancy": 0.96},
    "young":  {"seed": 67110, "half_length": 3.55, "radius": 0.72, "soft_occupancy": 0.89, "dense_occupancy": 0.96},
    "mature": {"seed": 67210, "half_length": 5.10, "radius": 1.30, "soft_occupancy": 0.87, "dense_occupancy": 0.95},
    "old":    {"seed": 67310, "half_length": 7.15, "radius": 2.15, "soft_occupancy": 0.81, "dense_occupancy": 0.92},
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


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
    x0 = math.floor(gx)
    y0 = math.floor(gy)
    tx = smooth01(gx - x0)
    ty = smooth01(gy - y0)
    a = hash01(x0, y0, seed)
    b = hash01(x0 + 1, y0, seed)
    c = hash01(x0, y0 + 1, seed)
    d = hash01(x0 + 1, y0 + 1, seed)
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty


def write_texture(path: Path, seed: int, target_occupancy: float) -> float:
    """High-occupancy micro-cutout: tiny breakup without large punched holes."""
    scores = []
    for y in range(TEX_SIZE):
        v = (y + 0.5) / TEX_SIZE
        for x in range(TEX_SIZE):
            u = (x + 0.5) / TEX_SIZE
            n0 = value_noise(u, v, 18.0, seed ^ 0x1379)
            n1 = value_noise(u, v, 52.0, seed ^ 0x2468)
            n2 = value_noise(u, v, 119.0, seed ^ 0x55AA)
            micro = 0.5 + 0.5 * math.sin((u * 167.0 + v * 139.0) * math.tau + (seed & 63))
            scores.append(0.18 * n0 + 0.34 * n1 + 0.38 * n2 + 0.10 * micro)

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
    half_length = spec["half_length"] * (1.04 if variant == "soft" else 1.0)
    base_radius = spec["radius"] * (0.97 if variant == "soft" else 1.04)
    phases = [rng.uniform(0.0, math.tau) for _ in range(5)]
    return rng, half_length, base_radius, phases


def build_shell(spec: dict, variant: str):
    rng, half_length, base_radius, phases = shell_parameters(spec, variant)
    p1, p2, p3, p4, p5 = phases
    vertices = []
    indices = []

    for iz in range(RINGS):
        t = iz / (RINGS - 1)
        z = -half_length + 2.0 * half_length * t
        # Rounded compact cloud body; narrower tips preserve overlap without the
        # spear/torpedo silhouette seen in v6.6 broken sections.
        axial = 0.14 + 0.86 * max(0.0, math.sin(math.pi * t)) ** 0.62
        bulge = (
            1.0
            + 0.090 * math.sin(t * math.tau * 2.0 + p1)
            + 0.040 * math.sin(t * math.tau * 3.0 + p2)
            + 0.018 * math.sin(t * math.tau * 5.0 + p4)
        )
        ring_radius = base_radius * axial * bulge
        centre_x = base_radius * (
            0.060 * math.sin(t * math.tau * 1.35 + p2)
            + 0.020 * math.sin(t * math.tau * 3.7 + p5)
        )
        centre_y = base_radius * (
            0.050 * math.cos(t * math.tau * 1.65 + p3)
            + 0.018 * math.sin(t * math.tau * 4.0 + p1)
        )

        for side in range(SIDES):
            phi = math.tau * side / SIDES
            angular = (
                1.0
                + 0.070 * math.sin(phi * 3.0 + p1 + t * 1.7)
                + 0.032 * math.sin(phi * 5.0 + p3 - t * 1.1)
                + 0.014 * math.sin(phi * 7.0 + p4)
            )
            angular *= rng.uniform(0.993, 1.007)
            rx = ring_radius * angular
            ry = ring_radius * (0.91 + 0.055 * math.sin(t * math.tau + p2)) * angular
            cp, sp = math.cos(phi), math.sin(phi)
            x = centre_x + rx * cp
            y = centre_y + ry * sp
            nx, ny = cp / max(rx, 1.0e-4), sp / max(ry, 1.0e-4)
            m = math.sqrt(nx * nx + ny * ny) or 1.0
            vertices.append((x, y, z, nx / m, ny / m, 0.0, side / SIDES, t))

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


def build_obj(age: str, variant: str, spec: dict) -> str:
    vertices, indices = build_shell(spec, variant)
    name = f"{age}_{variant}"
    lines = [
        "I", "800", "OBJ",
        f"# FFAtmo Renderer Foundation v6.7 physics-coupled vortex 3-D {name} cloudlet",
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
    forbidden = (
        "PARTICLE_SYSTEM", "EMITTER", "BILLBOARD", "ANIM_billboard",
        "ATTR_no_cull", "ATTR_blend", "BLEND_GLASS", "TEXTURE_LIT",
    )
    if any(token in obj for token in forbidden):
        raise RuntimeError(f"{name}: unsupported/translucent state returned")
    if f"GLOBAL_no_blend {ALPHA_CUTOFF:.3f}" not in obj:
        raise RuntimeError(f"{name}: alpha-test material state missing")
    vt = sum(1 for line in obj.splitlines() if line.startswith("VT "))
    if vt != RINGS * SIDES + 2:
        raise RuntimeError(f"{name}: unexpected shell vertex count {vt}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    info = [
        "FFAtmo Renderer Foundation v6.7 physics-coupled vortex roll-up assets",
        "Pure white RGB + binary alpha + GLOBAL_no_blend 0.5 retained from successful v6.5/v6.6 material path.",
        "Shorter compact cloudlets let the real wake cross-section deformation remain visible.",
    ]
    occupancies = {}
    extents = {"soft": [], "dense": []}
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
            length = max(v[2] for v in vertices) - min(v[2] for v in vertices)
            width = max(v[0] for v in vertices) - min(v[0] for v in vertices)
            extents[variant].append((length, width))
            info.append(f"{name}: length={length:.3f} m width={width:.3f} m opaque_occupancy={occupancy:.4f}")

    for variant in ("soft", "dense"):
        if not all(extents[variant][i][0] < extents[variant][i + 1][0] and
                   extents[variant][i][1] < extents[variant][i + 1][1]
                   for i in range(3)):
            raise RuntimeError(f"{variant}: non-monotonic age growth {extents[variant]}")
    for age in AGE_CLASSES:
        if not occupancies[f"{age}_soft"] < occupancies[f"{age}_dense"]:
            raise RuntimeError(f"{age}: soft asset must be more porous than dense")
    if not all(0.80 <= value <= 0.97 for value in occupancies.values()):
        raise RuntimeError(f"v6.7 occupancy outside realism range: {occupancies}")

    (args.output_dir / "ASSET_INFO.txt").write_text("\n".join(info) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
