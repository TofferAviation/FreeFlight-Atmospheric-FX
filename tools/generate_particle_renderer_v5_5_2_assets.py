#!/usr/bin/env python3
"""Generate mip-stable Renderer Foundation v5.5.2 cruise cloud assets."""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

SIZE = 1024
MAX_ALPHA = 225


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, pixels: bytes) -> None:
    rows = bytearray()
    stride = SIZE * 4
    for row in range(SIZE):
        rows.append(0)
        start = row * stride
        rows.extend(pixels[start : start + stride])
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(
        b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    )
    data += png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def gaussian(x: float, y: float, cx: float, cy: float, sx: float, sy: float) -> float:
    dx = (x - cx) / sx
    dy = (y - cy) / sy
    return math.exp(-0.5 * (dx * dx + dy * dy))


def generate_texture() -> bytes:
    """White ice cloud with broad alpha mass that survives 16x downscaling."""
    rng = random.Random(5520)
    lobes = [
        (
            rng.uniform(-0.40, 0.40),
            rng.uniform(-0.34, 0.34),
            rng.uniform(0.16, 0.38),
            rng.uniform(0.14, 0.34),
            rng.uniform(0.12, 0.34),
        )
        for _ in range(24)
    ]
    details = [
        (
            rng.uniform(-0.55, 0.55),
            rng.uniform(-0.50, 0.50),
            rng.uniform(0.055, 0.15),
            rng.uniform(0.050, 0.14),
            rng.uniform(0.025, 0.085),
        )
        for _ in range(32)
    ]
    holes = [
        (
            rng.uniform(-0.40, 0.40),
            rng.uniform(-0.36, 0.36),
            rng.uniform(0.07, 0.18),
            rng.uniform(0.07, 0.17),
            rng.uniform(0.035, 0.11),
        )
        for _ in range(10)
    ]

    pixels = bytearray()
    for y in range(SIZE):
        py = (2.0 * (y + 0.5) / SIZE) - 1.0
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0
            radius2 = (px * 0.94) ** 2 + (py * 1.08) ** 2
            broad = math.exp(-radius2 / 0.66)
            core = math.exp(-radius2 / 0.20)

            lobe_density = 0.0
            for cx, cy, sx, sy, weight in lobes:
                lobe_density += weight * gaussian(px, py, cx, cy, sx, sy)
            lobe_density = min(lobe_density / 2.20, 1.0)

            detail_density = 0.0
            for cx, cy, sx, sy, weight in details:
                detail_density += weight * gaussian(px, py, cx, cy, sx, sy)
            detail_density = min(detail_density / 1.10, 1.0)

            void_density = 0.0
            for cx, cy, sx, sy, weight in holes:
                void_density += weight * gaussian(px, py, cx, cy, sx, sy)
            void_density = min(void_density / 1.05, 0.28)

            low_frequency = 0.36 * broad + 0.27 * core + 0.27 * lobe_density
            fine_structure = 0.10 * detail_density - 0.42 * void_density
            wave = (
                0.965
                + 0.020 * math.sin(px * 8.0 + py * 5.0)
                + 0.012 * math.sin(px * 17.0 - py * 13.0)
            )
            density = (low_frequency + fine_structure) * wave

            # Keep a broad, faint interior body so mip averaging cannot erase the cloud.
            interior_floor = 0.105 * broad * max(0.0, min(1.0, (0.95 - radius2) / 0.40))
            density = max(density, interior_floor)
            edge = max(0.0, min(1.0, (1.10 - radius2) / 0.34))
            density = max(0.0, min(1.0, density * edge))
            alpha = int(round(MAX_ALPHA * density))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_v5_5_2_cloud.png
PARTICLE
 NAME ffatmo_v552_ice_cloud
 MAX_PARTICLES 4096
 BILLBOARD_MODE BILLBOARD
 BLEND_MODE NORMAL
 TEX_CELLS_X 1
 TEX_CELLS_Y 1
 ANIM_CELL_START 0
 ANIM_CELL_COUNT 1
 ANIM_CELL_REPEAT 1
 ANIM_CELL_RANDOM 0
ANIM_CELL_KF
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
SIZE_CURVE
INTERP_MODE LINEAR
 0.000000 1.000000
 1.000000 1.000000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE LINEAR
 0.000000 1.000000
 1.000000 1.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.260000
 AMBIENT 0.420000
EMISSIVE
INTERP_MODE LINEAR
 0.000000 0.900000
 1.000000 0.900000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
 0.000000 1.000000 1.000000 1.000000
 1.000000 0.985000 0.992000 1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE LINEAR
 0.000000 1.000000
 1.000000 1.000000
END_KEYFRAME_TABLE
SPIN_CURVE
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
ELASTICITY
INTERP_MODE LINEAR
 0.000000 1.000000
 1.000000 1.000000
END_KEYFRAME_TABLE
 COLLISION_MODE NONE
END_PARTICLE
EMITTER
 NAME ffatmo_v552_attached_cloud
 EMIT_MODE ATTACH
SUB_EMITTER
 PARTICLE_TYPE 0
EMIT_RATE
SLOT 1
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 1.000000 1.000000
END_KEYFRAME_TABLE
INITIAL_SPEED
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
ROTATION_SPEED
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
INITIAL_HEADING
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
INITIAL_PITCH
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
DX
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
DY
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
DZ
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
DLON
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
DLAT
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
INITIAL_ROTATION
SLOT 4
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 360.000000 360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE LINEAR
 0.000000 0.750000 0.750000
 1.000000 20.000000 20.000000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 1.000000 1.000000
END_KEYFRAME_TABLE
TIME_TO_LIVE
INTERP_MODE LINEAR
 0.000000 60.000000 60.000000
 1.000000 60.000000 60.000000
END_KEYFRAME_TABLE
END_SUB_EMITTER
DATAREFS 5
DREF 
DREF ffatmo/contrail_particle/rate
DREF ffatmo/contrail_particle/size
DREF ffatmo/contrail_particle/alpha
DREF ffatmo/contrail_particle/lifetime
END_EMITTER
 TEX_CELLS_X 1
 TEX_CELLS_Y 1
DATAREFS 0
END_PARTICLE_SYSTEM
"""


def object_text() -> str:
    return """I
800
OBJ
# FFAtmo Renderer Foundation v5.5.2 mip-stable pooled billboards
PARTICLE_SYSTEM contrail_v5_5_2_billboard.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_v552_attached_cloud 0 0 0 0 0 0
"""


def validate(pss: str, obj: str, pixels: bytes) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 1 or lines.count("SUB_EMITTER") != 1:
        raise RuntimeError("v5.5.2 requires one attached billboard particle")
    if lines.count("BILLBOARD_MODE BILLBOARD") != 1:
        raise RuntimeError("v5.5.2 billboard mode is missing")
    if lines.count("BILLBOARD_MODE RIBBON") != 0:
        raise RuntimeError("Ribbon mode must not return")
    if lines.count("EMIT_MODE ATTACH") != 1:
        raise RuntimeError("v5.5.2 attach mode is missing")
    if "INITIAL_SIZE" not in pss or "20.000000" not in pss:
        raise RuntimeError("v5.5.2 20 metre size range is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.5.2 OBJ must remain particle-only")
    if len(pixels) != SIZE * SIZE * 4:
        raise RuntimeError("v5.5.2 RGBA texture size is invalid")
    if any(value != 255 for channel in (pixels[0::4], pixels[1::4], pixels[2::4]) for value in channel):
        raise RuntimeError("v5.5.2 cloud RGB must remain pure white")

    alpha = pixels[3::4]
    if max(alpha) < 150:
        raise RuntimeError("v5.5.2 cloud peak opacity is too low")

    # Simulate X-Plane's observed 16x texture scaling. The resulting 64x64
    # alpha field must retain broad optical mass instead of disappearing.
    block = 16
    block_averages = []
    for by in range(0, SIZE, block):
        for bx in range(0, SIZE, block):
            total = 0
            for yy in range(by, by + block):
                row = yy * SIZE
                for xx in range(bx, bx + block):
                    total += alpha[row + xx]
            block_averages.append(total / float(block * block))
    if max(block_averages) < 95.0:
        raise RuntimeError("v5.5.2 mip-scale peak alpha is too low")
    if sum(1 for value in block_averages if value >= 18.0) < 700:
        raise RuntimeError("v5.5.2 mip-scale cloud body is too sparse")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    pixels = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj, pixels)

    write_rgba_png(args.output_dir / "contrail_v5_5_2_cloud.png", pixels)
    (args.output_dir / "contrail_v5_5_2_billboard.pss").write_text(pss, encoding="utf-8", newline="\n")
    (args.output_dir / "contrail_billboard.obj").write_text(obj, encoding="utf-8", newline="\n")
    (args.output_dir / "ASSET_INFO.txt").write_text(
        "Renderer Foundation v5.5.2 cruise-visible mip-stable billboard cloud\n",
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
