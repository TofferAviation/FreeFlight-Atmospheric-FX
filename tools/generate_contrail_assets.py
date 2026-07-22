#!/usr/bin/env python3
"""Generate deterministic OBJ8 contrail puff assets without third-party modules."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    rows = bytearray()
    stride = width * 4
    for row in range(height):
        rows.append(0)
        start = row * stride
        rows.extend(pixels[start : start + stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    payload += png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    payload += png_chunk(b"IEND", b"")
    path.write_bytes(payload)


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    if edge0 == edge1:
        return 1.0 if value >= edge1 else 0.0
    ratio = min(max((value - edge0) / (edge1 - edge0), 0.0), 1.0)
    return ratio * ratio * (3.0 - 2.0 * ratio)


def make_texture(path: Path, maximum_alpha: float, seed: int) -> None:
    width = 128
    height = 128
    rng = random.Random(seed)
    lobes = []
    for _ in range(18):
        lobes.append(
            (
                rng.uniform(-0.42, 0.42),
                rng.uniform(-0.42, 0.42),
                rng.uniform(0.09, 0.28),
                rng.uniform(0.25, 0.75),
            )
        )

    pixels = bytearray()
    for y in range(height):
        py = (2.0 * (y + 0.5) / height) - 1.0
        for x in range(width):
            px = (2.0 * (x + 0.5) / width) - 1.0
            radius = math.sqrt(px * px + py * py)
            envelope = max(0.0, 1.0 - smoothstep(0.28, 1.0, radius))
            cloud = 0.0
            for lx, ly, sigma, weight in lobes:
                distance2 = (px - lx) ** 2 + (py - ly) ** 2
                cloud += weight * math.exp(-distance2 / (2.0 * sigma * sigma))
            cloud = min(cloud / 3.8, 1.0)
            wisps = 0.88 + 0.12 * math.sin(px * 15.0 + math.sin(py * 7.0))
            alpha = maximum_alpha * envelope * (0.48 + 0.52 * cloud) * wisps
            alpha = min(max(alpha, 0.0), maximum_alpha)
            brightness = int(238 + 14 * cloud)
            pixels.extend(
                (
                    min(255, brightness),
                    min(255, brightness + 4),
                    255,
                    int(round(alpha * 255.0)),
                )
            )
    write_png(path, width, height, bytes(pixels))


def make_obj(path: Path, texture_name: str) -> None:
    # Three intersecting quads provide a low-cost volumetric puff from arbitrary
    # camera angles while retaining X-Plane's normal depth testing.
    vertices = [
        (-1, -1, 0, 0, 0, 1, 0, 0),
        (1, -1, 0, 0, 0, 1, 1, 0),
        (1, 1, 0, 0, 0, 1, 1, 1),
        (-1, 1, 0, 0, 0, 1, 0, 1),
        (0, -1, -1, 1, 0, 0, 0, 0),
        (0, -1, 1, 1, 0, 0, 1, 0),
        (0, 1, 1, 1, 0, 0, 1, 1),
        (0, 1, -1, 1, 0, 0, 0, 1),
        (-1, 0, -1, 0, 1, 0, 0, 0),
        (1, 0, -1, 0, 1, 0, 1, 0),
        (1, 0, 1, 0, 1, 0, 1, 1),
        (-1, 0, 1, 0, 1, 0, 0, 1),
    ]
    indices = [
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
    ]
    lines = [
        "I",
        "800",
        "OBJ",
        f"TEXTURE {texture_name}",
        "GLOBAL_no_shadow",
        "GLOBAL_specular 0.0",
        f"POINT_COUNTS {len(vertices)} 0 0 {len(indices)}",
    ]
    for vertex in vertices:
        lines.append("VT " + " ".join(str(value) for value in vertex))
    for index in indices:
        lines.append(f"IDX {index}")
    lines.extend(
        [
            "ANIM_begin",
            "ANIM_scale 0.001 0.001 0.001 32 32 32 0 32 ffatmo/contrail_debug/scale",
            "ATTR_LOD 0 120000",
            "ATTR_no_cull",
            "ATTR_blend",
            "ATTR_no_shadow",
            f"TRIS 0 {len(indices)}",
            "ANIM_end",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    alpha_levels = (0.10, 0.17, 0.25, 0.34)
    for index, maximum_alpha in enumerate(alpha_levels):
        texture_name = f"contrail_puff_{index}.png"
        object_name = f"contrail_puff_{index}.obj"
        make_texture(args.output / texture_name, maximum_alpha, 9100 + index)
        make_obj(args.output / object_name, texture_name)

    (args.output / "ASSET_INFO.txt").write_text(
        "Deterministically generated depth-aware contrail puff assets.\n"
        "Four alpha buckets, each using three intersecting blended quads.\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
