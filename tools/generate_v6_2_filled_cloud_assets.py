#!/usr/bin/env python3
"""Generate v6.2 native 3-D cloudlet size-class assets."""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 256

CLASSES = {
    "near": {
        "seed": 62010,
        "alpha_base": 40,
        "alpha_range": 80,
        "lobes": [
            ((-0.20,  0.05, -1.75), (0.62, 0.50, 1.25)),
            (( 0.24, -0.12, -0.62), (0.78, 0.61, 1.38)),
            ((-0.27,  0.16,  0.62), (0.82, 0.65, 1.40)),
            (( 0.16, -0.05,  1.76), (0.60, 0.50, 1.18)),
        ],
    },
    "young": {
        "seed": 62110,
        "alpha_base": 36,
        "alpha_range": 76,
        "lobes": [
            ((-0.35,  0.02, -2.95), (1.05, 0.83, 1.72)),
            (( 0.45,  0.24, -1.70), (1.25, 1.02, 1.78)),
            ((-0.48, -0.28, -0.35), (1.35, 1.08, 1.90)),
            (( 0.40,  0.14,  1.15), (1.30, 1.00, 1.86)),
            ((-0.28,  0.30,  2.65), (1.02, 0.82, 1.62)),
        ],
    },
    "mature": {
        "seed": 62210,
        "alpha_base": 30,
        "alpha_range": 72,
        "lobes": [
            ((-0.55,  0.00, -4.45), (1.65, 1.32, 2.15)),
            (( 0.65,  0.42, -3.05), (2.02, 1.65, 2.30)),
            ((-0.82, -0.52, -1.45), (2.25, 1.82, 2.40)),
            (( 0.78,  0.18,  0.15), (2.35, 1.90, 2.42)),
            ((-0.68,  0.58,  1.80), (2.25, 1.78, 2.38)),
            (( 0.55, -0.32,  3.30), (1.92, 1.56, 2.18)),
            ((-0.30,  0.26,  4.55), (1.48, 1.22, 1.72)),
        ],
    },
    "old": {
        "seed": 62310,
        "alpha_base": 22,
        "alpha_range": 64,
        "lobes": [
            ((-0.85,  0.16, -5.75), (2.15, 1.78, 2.45)),
            (( 1.05,  0.66, -4.10), (2.72, 2.18, 2.78)),
            ((-1.18, -0.72, -2.15), (3.08, 2.46, 2.95)),
            (( 1.12,  0.24, -0.05), (3.28, 2.65, 3.05)),
            ((-0.98,  0.84,  2.10), (3.12, 2.52, 2.98)),
            (( 0.82, -0.56,  4.10), (2.72, 2.18, 2.70)),
            ((-0.48,  0.36,  5.90), (2.05, 1.72, 2.32)),
        ],
    },
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_texture(path: Path, seed: int, alpha_base: int, alpha_range: int) -> None:
    rng = random.Random(seed)
    coarse = [
        (
            rng.uniform(-1.0, 1.0),
            rng.uniform(-1.0, 1.0),
            rng.uniform(0.16, 0.46),
            rng.uniform(0.14, 0.42),
            rng.uniform(0.09, 0.28),
        )
        for _ in range(24)
    ]
    pixels = bytearray()
    for y in range(TEX_SIZE):
        py = 2.0 * (y + 0.5) / TEX_SIZE - 1.0
        for x in range(TEX_SIZE):
            px = 2.0 * (x + 0.5) / TEX_SIZE - 1.0
            density = 0.0
            for cx, cy, sx, sy, weight in coarse:
                dx = (px - cx) / sx
                dy = (py - cy) / sy
                density += weight * math.exp(-0.5 * (dx * dx + dy * dy))
            low = 0.52 + 0.18 * math.sin(px * 7.0 + py * 5.0) + 0.10 * math.sin(px * 17.0 - py * 13.0)
            fine = 0.5 + 0.5 * math.sin(px * 31.0 + math.sin(py * 11.0) * 2.1)
            response = max(0.0, min(1.0, 0.24 + 0.42 * density + 0.18 * low + 0.07 * fine))
            alpha = int(round(alpha_base + alpha_range * response))
            pixels.extend((245, 250, 255, max(0, min(255, alpha))))

    raw = bytearray()
    stride = TEX_SIZE * 4
    for y in range(TEX_SIZE):
        raw.append(0)
        raw.extend(pixels[y * stride : (y + 1) * stride])
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", TEX_SIZE, TEX_SIZE, 8, 6, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def add_lobe(vertices, indices, centre, radii, seed, lat_steps=7, lon_steps=14):
    rng = random.Random(seed)
    ring_noise = [rng.uniform(0.88, 1.14) for _ in range(lat_steps + 1)]
    phase_a = rng.uniform(0.0, math.tau)
    phase_b = rng.uniform(0.0, math.tau)
    base = len(vertices)

    for iy in range(lat_steps + 1):
        theta = math.pi * iy / lat_steps
        st = math.sin(theta)
        ct = math.cos(theta)
        for ix in range(lon_steps):
            phi = math.tau * ix / lon_steps
            cp = math.cos(phi)
            sp = math.sin(phi)
            surface = ring_noise[iy]
            surface *= 0.93 + 0.055 * math.sin(phi * 3.0 + phase_a)
            surface *= 0.96 + 0.040 * math.sin(phi * 5.0 + theta * 2.0 + phase_b)
            nx, ny, nz = st * cp, ct, st * sp
            x = centre[0] + radii[0] * nx * surface
            y = centre[1] + radii[1] * ny * surface
            z = centre[2] + radii[2] * nz * surface

            ex = nx / max(radii[0], 1.0e-4)
            ey = ny / max(radii[1], 1.0e-4)
            ez = nz / max(radii[2], 1.0e-4)
            m = math.sqrt(ex * ex + ey * ey + ez * ez) or 1.0
            vertices.append((x, y, z, ex / m, ey / m, ez / m, ix / lon_steps, iy / lat_steps))

    for iy in range(lat_steps):
        for ix in range(lon_steps):
            a = base + iy * lon_steps + ix
            b = base + iy * lon_steps + ((ix + 1) % lon_steps)
            c = base + (iy + 1) * lon_steps + ix
            d = base + (iy + 1) * lon_steps + ((ix + 1) % lon_steps)
            indices.extend((a, c, b, b, c, d))


def build_obj(name: str, spec: dict) -> str:
    vertices = []
    indices = []
    for index, (centre, radii) in enumerate(spec["lobes"]):
        add_lobe(vertices, indices, centre, radii, spec["seed"] + index + 1)

    lines = [
        "I",
        "800",
        "OBJ",
        f"# FFAtmo Renderer Foundation v6.2 native 3-D {name} cloudlet",
        f"TEXTURE contrail_cloudlet_{name}.png",
        "GLOBAL_no_shadow",
        f"POINT_COUNTS {len(vertices)} 0 0 {len(indices)}",
    ]
    for vertex in vertices:
        lines.append("VT " + " ".join(f"{value:.6f}" for value in vertex))

    cursor = 0
    while cursor + 10 <= len(indices):
        lines.append("IDX10 " + " ".join(str(value) for value in indices[cursor : cursor + 10]))
        cursor += 10
    while cursor < len(indices):
        lines.append(f"IDX {indices[cursor]}")
        cursor += 1

    lines += [
        "ATTR_no_shadow",
        "ATTR_shade_smooth",
        "ATTR_no_cull",
        "ATTR_blend",
        "ATTR_shiny_rat 0.0",
        f"TRIS 0 {len(indices)}",
        "",
    ]
    return "\n".join(lines)


def validate(name: str, obj: str) -> None:
    forbidden = ("PARTICLE_SYSTEM", "EMITTER", "BILLBOARD", "ANIM_billboard")
    if any(token in obj for token in forbidden):
        raise RuntimeError(f"{name}: particle or billboard primitive returned")
    if "ATTR_blend" not in obj or "GLOBAL_no_shadow" not in obj or "TRIS 0 " not in obj:
        raise RuntimeError(f"{name}: required native 3-D render state missing")
    vertex_count = sum(1 for line in obj.splitlines() if line.startswith("VT "))
    if vertex_count < 350:
        raise RuntimeError(f"{name}: 3-D cloudlet mesh too sparse ({vertex_count})")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    info = ["FFAtmo Renderer Foundation v6.2 native 3-D filled cloud field"]
    for name, spec in CLASSES.items():
        obj = build_obj(name, spec)
        validate(name, obj)
        (args.output_dir / f"contrail_cloudlet_{name}.obj").write_text(obj, encoding="utf-8", newline="\n")
        write_texture(
            args.output_dir / f"contrail_cloudlet_{name}.png",
            spec["seed"],
            spec["alpha_base"],
            spec["alpha_range"],
        )
        info.append(f"{name}: {len(spec['lobes'])} overlapping closed 3-D lobes")

    info += [
        "No particles, billboards, ribbons, Modern3D callbacks, or camera-facing planes.",
        "Classes are aligned to the physical wake by the plugin and overlap into a filled field.",
    ]
    (args.output_dir / "ASSET_INFO.txt").write_text("\n".join(info) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
