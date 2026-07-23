#!/usr/bin/env python3
"""Generate deterministic Renderer v4.1 OBJ8 trail-segment assets."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path


WIDTH = 256
HEIGHT = 256
BORDER_FRACTION = 0.08
ALPHA_LEVELS = (0.030, 0.052, 0.082, 0.120)
VARIANTS = ("a", "b")


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


def gaussian(distance2: float, sigma: float) -> float:
    return math.exp(-distance2 / (2.0 * sigma * sigma))


def make_texture(maximum_alpha: float, seed: int) -> bytes:
    rng = random.Random(seed)
    centreline_phase = rng.uniform(0.0, math.tau)
    edge_phase = rng.uniform(0.0, math.tau)
    broad_lobes = [
        (
            rng.uniform(-0.20, 0.20),
            rng.uniform(-0.82, 0.82),
            rng.uniform(0.16, 0.34),
            rng.uniform(0.45, 1.00),
        )
        for _ in range(18)
    ]
    detail_lobes = [
        (
            rng.uniform(-0.48, 0.48),
            rng.uniform(-0.90, 0.90),
            rng.uniform(0.045, 0.13),
            rng.uniform(0.08, 0.30),
        )
        for _ in range(56)
    ]

    pixels = bytearray()
    border_pixels = int(round(WIDTH * BORDER_FRACTION))
    for y in range(HEIGHT):
        py = (2.0 * (y + 0.5) / HEIGHT) - 1.0
        for x in range(WIDTH):
            px = (2.0 * (x + 0.5) / WIDTH) - 1.0

            centre_offset = (
                0.055 * math.sin(py * 4.8 + centreline_phase)
                + 0.022 * math.sin(py * 11.0 + edge_phase)
            )
            lateral = abs(px - centre_offset)
            edge_width = (
                0.72
                + 0.055 * math.sin(py * 5.3 + edge_phase)
                + 0.025 * math.sin(py * 12.7 + centreline_phase)
            )
            lateral_envelope = 1.0 - smoothstep(0.18, max(edge_width, 0.52), lateral)
            end_envelope = 1.0 - smoothstep(0.72, 0.96, abs(py))

            broad = 0.0
            for lx, ly, sigma, weight in broad_lobes:
                broad += weight * gaussian((px - lx) ** 2 + (py - ly) ** 2, sigma)
            broad = min(broad / 4.8, 1.0)

            detail = 0.0
            for lx, ly, sigma, weight in detail_lobes:
                detail += weight * gaussian((px - lx) ** 2 + (py - ly) ** 2, sigma)
            detail = min(detail / 2.5, 1.0)

            longitudinal_noise = 0.88 + 0.12 * math.sin(py * 8.0 + math.sin(px * 4.0))
            density = (
                lateral_envelope
                * end_envelope
                * (0.20 + 0.60 * broad + 0.20 * detail)
                * longitudinal_noise
            )
            density *= 0.84 + 0.16 * smoothstep(0.0, 0.8, broad)
            alpha = min(max(maximum_alpha * density, 0.0), maximum_alpha)

            if (
                x < border_pixels
                or y < border_pixels
                or x >= WIDTH - border_pixels
                or y >= HEIGHT - border_pixels
            ):
                alpha = 0.0

            pixels.extend((255, 255, 255, int(round(alpha * 255.0))))
    return bytes(pixels)


def validate_pixels(name: str, pixels: bytes, maximum_alpha: float) -> None:
    expected = WIDTH * HEIGHT * 4
    if len(pixels) != expected:
        raise RuntimeError(f"{name}: expected {expected} RGBA bytes, got {len(pixels)}")
    border_pixels = int(round(WIDTH * BORDER_FRACTION))
    observed_maximum_alpha = 0
    nonzero_alpha = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 4
            r, g, b, alpha = pixels[offset : offset + 4]
            if max(r, g, b) - min(r, g, b) > 3:
                raise RuntimeError(f"{name}: RGB channels diverge by more than three levels")
            if min(r, g, b) < 248:
                raise RuntimeError(f"{name}: texture contains non-neutral dark colour")
            if (
                x < border_pixels
                or y < border_pixels
                or x >= WIDTH - border_pixels
                or y >= HEIGHT - border_pixels
            ) and alpha != 0:
                raise RuntimeError(f"{name}: outer eight-percent border is not transparent")
            observed_maximum_alpha = max(observed_maximum_alpha, alpha)
            if alpha > 0:
                nonzero_alpha += 1
    allowed_maximum = int(math.ceil(maximum_alpha * 255.0))
    if observed_maximum_alpha > allowed_maximum:
        raise RuntimeError(f"{name}: alpha exceeds configured bucket maximum")
    if nonzero_alpha < WIDTH * HEIGHT * 0.10:
        raise RuntimeError(f"{name}: texture contains too little visible structure")


def make_obj(path: Path, texture_name: str) -> None:
    # Unit quad: width follows local X and trail length follows local Y.
    vertices = [
        (-0.5, -0.5, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0),
        (0.5, -0.5, 0.0, 0.0, 0.0, -1.0, 1.0, 0.0),
        (0.5, 0.5, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0),
        (-0.5, 0.5, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0),
    ]
    indices = [0, 2, 1, 0, 3, 2]
    lines = [
        "I",
        "800",
        "OBJ",
        f"TEXTURE {texture_name}",
        f"TEXTURE_LIT {texture_name}",
        "GLOBAL_luminance 120",
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
            "ATTR_LOD 0 120000",
            "ANIM_begin",
            "ANIM_scale 0.001 1 1 32 1 1 0 32 ffatmo/contrail_debug/width",
            "ANIM_begin",
            "ANIM_scale 1 0.001 1 1 32 1 0 32 ffatmo/contrail_debug/length",
            "ATTR_no_cull",
            "ATTR_blend",
            "ATTR_no_shadow",
            f"TRIS 0 {len(indices)}",
            "ANIM_end",
            "ANIM_end",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def validate_obj(path: Path, texture_name: str) -> None:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    commands = [
        line
        for line in lines
        if line.startswith(("ATTR_", "ANIM_", "TRIS", "LINES", "LIGHT", "IF", "ELSE", "ENDIF"))
    ]
    if not commands or not commands[0].startswith("ATTR_LOD "):
        raise RuntimeError(f"{path.name}: ATTR_LOD must be the first command")
    if lines.count("ANIM_begin") != 2 or lines.count("ANIM_end") != 2:
        raise RuntimeError(f"{path.name}: expected two balanced scale animation groups")
    if sum(1 for line in lines if line.startswith("TRIS ")) != 1:
        raise RuntimeError(f"{path.name}: expected exactly one TRIS command")
    if f"TEXTURE {texture_name}" not in lines:
        raise RuntimeError(f"{path.name}: expected albedo texture reference is missing")
    if f"TEXTURE_LIT {texture_name}" not in lines:
        raise RuntimeError(f"{path.name}: expected neutral lit texture reference is missing")
    if "GLOBAL_luminance 120" not in lines:
        raise RuntimeError(f"{path.name}: expected neutral luminance directive is missing")
    if "ATTR_no_cull" not in lines or "ATTR_blend" not in lines or "ATTR_no_shadow" not in lines:
        raise RuntimeError(f"{path.name}: required transparency attributes are missing")
    if not any("ffatmo/contrail_debug/width" in line for line in lines):
        raise RuntimeError(f"{path.name}: width dataref scale is missing")
    if not any("ffatmo/contrail_debug/length" in line for line in lines):
        raise RuntimeError(f"{path.name}: length dataref scale is missing")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    generated = []
    for bucket, maximum_alpha in enumerate(ALPHA_LEVELS):
        for variant_index, variant in enumerate(VARIANTS):
            stem = f"contrail_core_{bucket}_{variant}"
            texture_name = f"{stem}.png"
            object_name = f"{stem}.obj"
            texture_path = args.output / texture_name
            object_path = args.output / object_name
            pixels = make_texture(maximum_alpha, 14100 + bucket * 31 + variant_index * 109)
            validate_pixels(texture_name, pixels, maximum_alpha)
            write_png(texture_path, WIDTH, HEIGHT, pixels)
            make_obj(object_path, texture_name)
            validate_obj(object_path, texture_name)
            generated.extend((texture_name, object_name))

    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Contrail Renderer Foundation v4.1 deterministic segment assets.\n"
        "Eight neutral-white elongated assets: four alpha buckets and two variants.\n"
        "Width and trail length are controlled independently by XPLM instance datarefs.\n"
        "A low neutral lit overlay prevents black or olive camera-angle shading.\n"
        "OBJ8 LOD, animation balance, luminance, and transparency commands validated.\n"
        + "\n".join(generated)
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
