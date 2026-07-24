#!/usr/bin/env python3
"""Generate deterministic Renderer Foundation v4.4 single-layer composite assets."""

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
ALPHA_LEVELS = (0.028, 0.045, 0.070, 0.100)
VARIANTS = ("a", "b")
LUMINANCE_NITS = 250
EMISSION_RGB = "1.0 1.0 1.0"


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
    centre_phase = rng.uniform(0.0, math.tau)
    edge_phase = rng.uniform(0.0, math.tau)
    cloud_lobes = [
        (
            rng.uniform(-0.28, 0.28),
            rng.uniform(-0.88, 0.88),
            rng.uniform(0.10, 0.28),
            rng.uniform(0.10, 0.34),
        )
        for _ in range(22)
    ]

    pixels = bytearray()
    border_pixels = int(round(WIDTH * BORDER_FRACTION))
    centre_column_minimum = 255

    for y in range(HEIGHT):
        py = (2.0 * (y + 0.5) / HEIGHT) - 1.0
        for x in range(WIDTH):
            px = (2.0 * (x + 0.5) / WIDTH) - 1.0

            centre_offset = (
                0.024 * math.sin(py * 4.2 + centre_phase)
                + 0.010 * math.sin(py * 10.1 + edge_phase)
            )
            lateral = abs(px - centre_offset)
            edge_width = (
                0.70
                + 0.035 * math.sin(py * 4.8 + edge_phase)
                + 0.015 * math.sin(py * 11.7 + centre_phase)
            )

            edge_envelope = 1.0 - smoothstep(0.34, max(edge_width, 0.56), lateral)
            end_envelope = 1.0 - smoothstep(0.66, 0.96, abs(py))
            broad_profile = math.exp(-((lateral / 0.37) ** 2))
            centre_profile = math.exp(-((lateral / 0.115) ** 2))

            cloud_detail = 0.0
            for lx, ly, sigma, weight in cloud_lobes:
                cloud_detail += weight * gaussian((px - lx) ** 2 + (py - ly) ** 2, sigma)
            cloud_detail = min(cloud_detail / 2.5, 1.0)

            longitudinal = (
                0.93
                + 0.045 * math.sin(py * 7.1 + centre_phase)
                + 0.025 * math.sin(py * 15.2 + edge_phase)
            )

            # One continuous density profile: the centre and feathered halo are
            # baked into the same alpha channel, so no second transparent card
            # can cut a dark hole through the trail.
            density = (
                end_envelope
                * edge_envelope
                * (0.34 * broad_profile + 0.46 * centre_profile + 0.20 * cloud_detail)
                * longitudinal
            )
            density = min(max(density, 0.0), 1.0)
            alpha = min(max(maximum_alpha * density, 0.0), maximum_alpha)

            if (
                x < border_pixels
                or y < border_pixels
                or x >= WIDTH - border_pixels
                or y >= HEIGHT - border_pixels
            ):
                alpha = 0.0

            alpha_byte = int(round(alpha * 255.0))
            if abs(px) < 0.03 and abs(py) < 0.55:
                centre_column_minimum = min(centre_column_minimum, alpha_byte)
            pixels.extend((255, 255, 255, alpha_byte))

    if centre_column_minimum <= 0:
        raise RuntimeError("composite texture contains a transparent centreline")
    return bytes(pixels)


def validate_pixels(name: str, pixels: bytes, maximum_alpha: float) -> None:
    expected = WIDTH * HEIGHT * 4
    if len(pixels) != expected:
        raise RuntimeError(f"{name}: expected {expected} RGBA bytes, got {len(pixels)}")
    border_pixels = int(round(WIDTH * BORDER_FRACTION))
    observed_maximum_alpha = 0
    nonzero_alpha = 0
    centre_nonzero = 0

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
            if abs(x - WIDTH // 2) <= 3 and HEIGHT * 0.25 < y < HEIGHT * 0.75 and alpha > 0:
                centre_nonzero += 1

    allowed_maximum = int(math.ceil(maximum_alpha * 255.0))
    if observed_maximum_alpha > allowed_maximum:
        raise RuntimeError(f"{name}: alpha exceeds configured bucket maximum")
    if nonzero_alpha < WIDTH * HEIGHT * 0.10:
        raise RuntimeError(f"{name}: texture contains too little visible structure")
    if centre_nonzero < HEIGHT * 0.40:
        raise RuntimeError(f"{name}: continuous composite centre is missing")


def make_obj(path: Path, texture_name: str) -> None:
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
        f"GLOBAL_luminance {LUMINANCE_NITS}",
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
            f"ATTR_emission_rgb {EMISSION_RGB}",
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
    if f"GLOBAL_luminance {LUMINANCE_NITS}" not in lines:
        raise RuntimeError(f"{path.name}: expected v4.4 luminance directive is missing")
    if f"ATTR_emission_rgb {EMISSION_RGB}" not in lines:
        raise RuntimeError(f"{path.name}: expected v4.4 neutral emission override is missing")
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
            # Keep the legacy filename expected by the current XPLM instance
            # loader; the contents are v4.4 composite assets, not core assets.
            stem = f"contrail_core_{bucket}_{variant}"
            texture_name = f"{stem}.png"
            object_name = f"{stem}.obj"
            texture_path = args.output / texture_name
            object_path = args.output / object_name
            pixels = make_texture(maximum_alpha, 14400 + bucket * 19 + variant_index * 107)
            validate_pixels(texture_name, pixels, maximum_alpha)
            write_png(texture_path, WIDTH, HEIGHT, pixels)
            make_obj(object_path, texture_name)
            validate_obj(object_path, texture_name)
            generated.extend((texture_name, object_name))

    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v4.4 deterministic single-layer composite asset set.\n"
        "A dense white centre and feathered halo are baked into one continuous alpha profile.\n"
        "No separate core object is rendered, preventing transparent depth/sorting cut-outs.\n"
        f"Neutral daytime luminance: {LUMINANCE_NITS} nits.\n"
        "Eight assets: four optical buckets and two deterministic variants.\n"
        "Legacy contrail_core filenames are retained for loader compatibility only.\n"
        + "\n".join(generated)
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
