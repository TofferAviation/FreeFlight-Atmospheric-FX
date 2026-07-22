#!/usr/bin/env python3
"""Generate deterministic OBJ8 contrail billboard assets without third-party modules."""

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
    width = 256
    height = 256
    rng = random.Random(seed)
    lobes = []
    for _ in range(34):
        lobes.append(
            (
                rng.uniform(-0.48, 0.48),
                rng.uniform(-0.42, 0.42),
                rng.uniform(0.10, 0.30),
                rng.uniform(0.18, 0.56),
            )
        )

    pixels = bytearray()
    for y in range(height):
        py = (2.0 * (y + 0.5) / height) - 1.0
        for x in range(width):
            px = (2.0 * (x + 0.5) / width) - 1.0

            # Slightly elliptical envelope avoids a perfect circular smoke-ball shape.
            elliptical_radius = math.sqrt((px / 1.0) ** 2 + (py / 0.82) ** 2)
            envelope = max(0.0, 1.0 - smoothstep(0.18, 1.0, elliptical_radius))

            cloud = 0.0
            for lx, ly, sigma, weight in lobes:
                distance2 = (px - lx) ** 2 + (py - ly) ** 2
                cloud += weight * math.exp(-distance2 / (2.0 * sigma * sigma))
            cloud = min(cloud / 5.4, 1.0)

            broad_density = math.exp(-((px * 0.76) ** 2 + (py * 1.08) ** 2) * 1.7)
            density = envelope * (0.34 + 0.42 * cloud + 0.24 * broad_density)
            alpha = min(max(maximum_alpha * density, 0.0), maximum_alpha)

            # Keep the albedo nearly neutral white; the old grey/blue variation combined
            # with intersecting backfaces and produced dark mossy-looking balls.
            brightness = int(round(248 + 7 * cloud))
            pixels.extend(
                (
                    min(255, brightness),
                    min(255, brightness + 1),
                    255,
                    int(round(alpha * 255.0)),
                )
            )
    write_png(path, width, height, bytes(pixels))


def make_obj(path: Path, texture_name: str) -> None:
    # One softly textured quad is rotated toward the current X-Plane camera by the
    # plugin. This removes the visible cross-sections produced by v2's three planes.
    vertices = [
        (-1.28, -0.78, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0),
        (1.28, -0.78, 0.0, 0.0, 0.0, -1.0, 1.0, 0.0),
        (1.28, 0.78, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0),
        (-1.28, 0.78, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0),
    ]
    indices = [0, 2, 1, 0, 3, 2]
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
            # OBJ8 requires the first ATTR_LOD to be the first command record.
            "ATTR_LOD 0 120000",
            "ANIM_begin",
            "ANIM_scale 0.001 0.001 0.001 24 24 24 0 24 ffatmo/contrail_debug/scale",
            "ATTR_no_cull",
            "ATTR_blend",
            "ATTR_no_shadow",
            f"TRIS 0 {len(indices)}",
            "ANIM_end",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def validate_obj(path: Path) -> None:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    commands = [
        line
        for line in lines
        if line.startswith(("ATTR_", "ANIM_", "TRIS", "LINES", "LIGHT", "IF", "ELSE", "ENDIF"))
    ]
    if not commands or not commands[0].startswith("ATTR_LOD "):
        raise RuntimeError(f"{path.name}: ATTR_LOD must be the first command")
    if lines.count("ANIM_begin") != lines.count("ANIM_end"):
        raise RuntimeError(f"{path.name}: unbalanced ANIM_begin/ANIM_end")
    if sum(1 for line in lines if line.startswith("TRIS ")) != 1:
        raise RuntimeError(f"{path.name}: expected exactly one TRIS command")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    alpha_levels = (0.065, 0.105, 0.155, 0.225)
    for index, maximum_alpha in enumerate(alpha_levels):
        texture_name = f"contrail_puff_{index}.png"
        object_name = f"contrail_puff_{index}.obj"
        texture_path = args.output / texture_name
        object_path = args.output / object_name
        make_texture(texture_path, maximum_alpha, 9300 + index)
        make_obj(object_path, texture_name)
        validate_obj(object_path)

    (args.output / "ASSET_INFO.txt").write_text(
        "Deterministically generated camera-facing contrail billboard assets.\n"
        "Four soft alpha buckets, each using one depth-tested world-space quad.\n"
        "OBJ8 LOD and animation ordering validated during generation.\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
