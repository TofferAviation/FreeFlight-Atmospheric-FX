#!/usr/bin/env python3
"""Generate deterministic Renderer Foundation v4.7 lit-alpha composite assets."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

WIDTH = 256
HEIGHT = 256
SIDE_BORDER_FRACTION = 0.08
END_BORDER_PIXELS = 1
ALPHA_LEVELS = (0.028, 0.045, 0.070, 0.100)
VARIANTS = ("a", "b")
LIT_BRIGHTNESS_NITS = 85


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_png(
    path: Path,
    width: int,
    height: int,
    pixels: bytes,
    channels: int,
    colour_type: int,
) -> None:
    rows = bytearray()
    stride = width * channels
    for row in range(height):
        rows.append(0)
        start = row * stride
        rows.extend(pixels[start : start + stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += png_chunk(
        b"IHDR", struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    )
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


def make_textures(maximum_alpha: float, seed: int) -> tuple[bytes, bytes]:
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

    albedo = bytearray()
    lit = bytearray()
    side_border_pixels = int(round(WIDTH * SIDE_BORDER_FRACTION))
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
            # Keep density almost continuous along the card length. Adjacent cards
            # overlap heavily, so only the outermost texel row is transparent.
            end_envelope = 1.0 - smoothstep(0.94, 0.995, abs(py))
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
            density = (
                end_envelope
                * edge_envelope
                * (0.34 * broad_profile + 0.46 * centre_profile + 0.20 * cloud_detail)
                * longitudinal
            )
            density = min(max(density, 0.0), 1.0)

            if (
                x < side_border_pixels
                or x >= WIDTH - side_border_pixels
                or y < END_BORDER_PIXELS
                or y >= HEIGHT - END_BORDER_PIXELS
            ):
                density = 0.0

            alpha = min(max(maximum_alpha * density, 0.0), maximum_alpha)
            alpha_byte = int(round(alpha * 255.0))
            lit_byte = int(round(density * 255.0)) if alpha_byte > 0 else 0
            if abs(px) < 0.03 and abs(py) < 0.88:
                centre_column_minimum = min(centre_column_minimum, alpha_byte)
            albedo.extend((255, 255, 255, alpha_byte))
            lit.extend((lit_byte, lit_byte, lit_byte))

    if centre_column_minimum <= 0:
        raise RuntimeError("composite texture contains a transparent centreline")
    return bytes(albedo), bytes(lit)


def validate_albedo(name: str, pixels: bytes, maximum_alpha: float) -> None:
    expected = WIDTH * HEIGHT * 4
    if len(pixels) != expected:
        raise RuntimeError(f"{name}: expected {expected} RGBA bytes, got {len(pixels)}")
    side_border_pixels = int(round(WIDTH * SIDE_BORDER_FRACTION))
    observed_maximum_alpha = 0
    nonzero_alpha = 0
    centre_nonzero = 0
    end_support = 0

    for y in range(HEIGHT):
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 4
            r, g, b, alpha = pixels[offset : offset + 4]
            if max(r, g, b) - min(r, g, b) > 3 or min(r, g, b) < 248:
                raise RuntimeError(f"{name}: albedo must remain neutral white")
            if (
                x < side_border_pixels
                or x >= WIDTH - side_border_pixels
                or y < END_BORDER_PIXELS
                or y >= HEIGHT - END_BORDER_PIXELS
            ) and alpha != 0:
                raise RuntimeError(f"{name}: required transparent edge is not empty")
            observed_maximum_alpha = max(observed_maximum_alpha, alpha)
            if alpha > 0:
                nonzero_alpha += 1
            if abs(x - WIDTH // 2) <= 3 and HEIGHT * 0.10 < y < HEIGHT * 0.90 and alpha > 0:
                centre_nonzero += 1
            if abs(x - WIDTH // 2) <= 3 and (
                2 <= y <= 12 or HEIGHT - 13 <= y <= HEIGHT - 3
            ) and alpha > 0:
                end_support += 1

    if observed_maximum_alpha > int(math.ceil(maximum_alpha * 255.0)):
        raise RuntimeError(f"{name}: alpha exceeds configured bucket maximum")
    if nonzero_alpha < WIDTH * HEIGHT * 0.14:
        raise RuntimeError(f"{name}: texture contains too little visible structure")
    if centre_nonzero < HEIGHT * 0.70:
        raise RuntimeError(f"{name}: continuous composite centre is missing")
    if end_support < 30:
        raise RuntimeError(f"{name}: longitudinal seam support is too weak")


def validate_lit(name: str, pixels: bytes, albedo: bytes) -> None:
    expected = WIDTH * HEIGHT * 3
    if len(pixels) != expected:
        raise RuntimeError(f"{name}: expected {expected} RGB bytes, got {len(pixels)}")
    nonblack = 0
    for index in range(WIDTH * HEIGHT):
        r, g, b = pixels[index * 3 : index * 3 + 3]
        alpha = albedo[index * 4 + 3]
        if r != g or g != b:
            raise RuntimeError(f"{name}: RGB-only light map is not neutral")
        if alpha == 0 and r != 0:
            raise RuntimeError(f"{name}: light map leaks outside albedo alpha support")
        if r > 0:
            nonblack += 1
    if nonblack < WIDTH * HEIGHT * 0.14:
        raise RuntimeError(f"{name}: RGB-only light map contains too little cloud density")


def make_obj(path: Path, texture_name: str, lit_texture_name: str) -> None:
    vertices = [
        (-0.5, -0.5, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0),
        (0.5, -0.5, 0.0, 0.0, 0.0, -1.0, 1.0, 0.0),
        (0.5, 0.5, 0.0, 0.0, 0.0, -1.0, 1.0, 1.0),
        (-0.5, 0.5, 0.0, 0.0, 0.0, -1.0, 0.0, 1.0),
        (-0.5, -0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0),
        (0.5, -0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0),
        (0.5, 0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0),
        (-0.5, 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0),
    ]
    indices = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7]
    lines = [
        "I",
        "800",
        "OBJ",
        f"TEXTURE {texture_name}",
        f"TEXTURE_LIT {lit_texture_name}",
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
            "ATTR_blend",
            "ATTR_no_shadow",
            f"ATTR_light_level 0 1 ffatmo/contrail_debug/illumination {LIT_BRIGHTNESS_NITS}",
            f"TRIS 0 {len(indices)}",
            "ATTR_light_level_reset",
            "ANIM_end",
            "ANIM_end",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def validate_obj(path: Path, texture_name: str, lit_texture_name: str) -> None:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    commands = [
        line
        for line in lines
        if line.startswith(
            ("ATTR_", "ANIM_", "TRIS", "LINES", "LIGHT", "IF", "ELSE", "ENDIF")
        )
    ]
    if not commands or not commands[0].startswith("ATTR_LOD "):
        raise RuntimeError(f"{path.name}: ATTR_LOD must be the first command")
    if lines.count("ANIM_begin") != 2 or lines.count("ANIM_end") != 2:
        raise RuntimeError(f"{path.name}: expected two balanced scale animation groups")
    if sum(1 for line in lines if line.startswith("TRIS ")) != 1:
        raise RuntimeError(f"{path.name}: expected exactly one TRIS command")
    if f"TEXTURE {texture_name}" not in lines:
        raise RuntimeError(f"{path.name}: albedo texture reference is missing")
    if f"TEXTURE_LIT {lit_texture_name}" not in lines:
        raise RuntimeError(f"{path.name}: RGB-only LIT texture reference is missing")
    if (
        f"ATTR_light_level 0 1 ffatmo/contrail_debug/illumination {LIT_BRIGHTNESS_NITS}"
        not in lines
    ):
        raise RuntimeError(f"{path.name}: controlled cloud illumination is missing")
    if "ATTR_light_level_reset" not in lines:
        raise RuntimeError(f"{path.name}: light-level state is not reset")
    if any(line.startswith(("GLOBAL_luminance", "ATTR_emission_rgb")) for line in lines):
        raise RuntimeError(f"{path.name}: deprecated/uncontrolled emission state must not be present")
    if "POINT_COUNTS 8 0 0 12" not in lines:
        raise RuntimeError(f"{path.name}: expected matched front/back faces")
    if "ATTR_no_cull" in lines:
        raise RuntimeError(f"{path.name}: no-cull path can expose dark back-face lighting")
    if "ATTR_blend" not in lines or "ATTR_no_shadow" not in lines:
        raise RuntimeError(f"{path.name}: required transparency attributes are missing")
    for dataref in ("width", "length", "illumination"):
        if not any(f"ffatmo/contrail_debug/{dataref}" in line for line in lines):
            raise RuntimeError(f"{path.name}: {dataref} dataref is missing")


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
            lit_texture_name = f"{stem}_LIT.png"
            object_name = f"{stem}.obj"
            texture_path = args.output / texture_name
            lit_texture_path = args.output / lit_texture_name
            object_path = args.output / object_name
            albedo, lit = make_textures(
                maximum_alpha, 14400 + bucket * 19 + variant_index * 107
            )
            validate_albedo(texture_name, albedo, maximum_alpha)
            validate_lit(lit_texture_name, lit, albedo)
            write_png(texture_path, WIDTH, HEIGHT, albedo, 4, 6)
            write_png(lit_texture_path, WIDTH, HEIGHT, lit, 3, 2)
            make_obj(object_path, texture_name, lit_texture_name)
            validate_obj(object_path, texture_name, lit_texture_name)
            generated.extend((texture_name, lit_texture_name, object_name))

    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v4.7 deterministic lit-alpha single-layer composite asset set.\n"
        "The RGBA albedo texture alone controls transparency.\n"
        "A separate RGB-only LIT density map has no alpha channel and cannot make cards opaque.\n"
        f"A constant {LIT_BRIGHTNESS_NITS}-nit light-level proxy prevents black sun-facing/shaded cards.\n"
        "Longitudinal density remains continuous to the card ends so overlapping sections do not form gaps.\n"
        "Matched front/back faces carry opposite normals.\n"
        "No separate core object is rendered.\n"
        "Eight objects: four optical buckets and two deterministic variants.\n"
        "Legacy contrail_core filenames are retained for loader compatibility only.\n"
        + "\n".join(generated)
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
