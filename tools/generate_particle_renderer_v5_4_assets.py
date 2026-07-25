#!/usr/bin/env python3
"""Generate deterministic Renderer Foundation v5.4 ultra-real billboard assets."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

SIZE = 512


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
    """Pure-white multi-scale ice cloud with soft, broken feathering."""
    rng = random.Random(5400)
    large_lobes = [
        (
            rng.uniform(-0.38, 0.38),
            rng.uniform(-0.34, 0.34),
            rng.uniform(0.18, 0.42),
            rng.uniform(0.16, 0.38),
            rng.uniform(0.12, 0.34),
        )
        for _ in range(24)
    ]
    small_lobes = [
        (
            rng.uniform(-0.52, 0.52),
            rng.uniform(-0.48, 0.48),
            rng.uniform(0.07, 0.18),
            rng.uniform(0.06, 0.16),
            rng.uniform(0.025, 0.10),
        )
        for _ in range(36)
    ]
    holes = [
        (
            rng.uniform(-0.42, 0.42),
            rng.uniform(-0.38, 0.38),
            rng.uniform(0.08, 0.20),
            rng.uniform(0.07, 0.18),
            rng.uniform(0.04, 0.14),
        )
        for _ in range(14)
    ]

    pixels = bytearray()
    for y in range(SIZE):
        py = (2.0 * (y + 0.5) / SIZE) - 1.0
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0

            radius2 = (px * 0.94) ** 2 + (py * 1.06) ** 2
            broad = math.exp(-radius2 / 0.48)
            centre = math.exp(-radius2 / 0.12)

            large = 0.0
            for cx, cy, sx, sy, weight in large_lobes:
                large += weight * gaussian(px, py, cx, cy, sx, sy)
            large = min(large / 2.5, 1.0)

            small = 0.0
            for cx, cy, sx, sy, weight in small_lobes:
                small += weight * gaussian(px, py, cx, cy, sx, sy)
            small = min(small / 1.35, 1.0)

            voids = 0.0
            for cx, cy, sx, sy, weight in holes:
                voids += weight * gaussian(px, py, cx, cy, sx, sy)
            voids = min(voids / 1.2, 0.34)

            wave = (
                0.94
                + 0.030 * math.sin(px * 13.0 + py * 9.0)
                + 0.025 * math.sin(px * 29.0 - py * 17.0)
                + 0.018 * math.sin(px * 47.0 + py * 31.0)
            )
            density = (
                0.34 * broad
                + 0.18 * centre
                + 0.31 * large
                + 0.17 * small
                - voids
            ) * wave

            edge = max(0.0, min(1.0, (1.11 - radius2) / 0.34))
            density = max(0.0, min(1.0, density * edge))
            alpha = int(round(188.0 * density))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_v5_4_cloud.png
PARTICLE
 NAME ffatmo_v54_ice_cloud
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
 DIFFUSE 0.180000
 AMBIENT 0.240000
EMISSIVE
INTERP_MODE LINEAR
 0.000000 0.820000
 1.000000 0.820000
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
 NAME ffatmo_v54_attached_cloud
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
 0.000000 0.500000 0.500000
 1.000000 32.000000 32.000000
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
# FFAtmo Renderer Foundation v5.4 ultra-real pooled billboards
PARTICLE_SYSTEM contrail_v5_4_billboard.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_v54_attached_cloud 0 0 0 0 0 0
"""


def validate(pss: str, obj: str, pixels: bytes) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 1:
        raise RuntimeError("v5.4 requires exactly one particle type")
    if lines.count("SUB_EMITTER") != 1:
        raise RuntimeError("v5.4 requires exactly one sub-emitter")
    if lines.count("BILLBOARD_MODE BILLBOARD") != 1:
        raise RuntimeError("v5.4 billboard mode is missing")
    if lines.count("BILLBOARD_MODE RIBBON") != 0:
        raise RuntimeError("ribbon mode must not return")
    if lines.count("EMIT_MODE ATTACH") != 1:
        raise RuntimeError("v5.4 must use one attached particle per instance")
    if "EMITTER ffatmo_v54_attached_cloud" not in obj:
        raise RuntimeError("v5.4 OBJ emitter attachment is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.4 OBJ must remain particle-only")
    if len(pixels) != SIZE * SIZE * 4:
        raise RuntimeError("v5.4 RGBA texture size is invalid")
    rgb_values = pixels[0::4], pixels[1::4], pixels[2::4]
    if any(value != 255 for channel in rgb_values for value in channel):
        raise RuntimeError("v5.4 cloud RGB must remain pure white")
    alpha_values = pixels[3::4]
    if max(alpha_values) < 170:
        raise RuntimeError("v5.4 cloud alpha support is too weak")
    visible = sum(1 for value in alpha_values if value > 3)
    if visible < SIZE * SIZE * 0.18:
        raise RuntimeError("v5.4 cloud texture contains too little visible structure")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    pss = particle_system_text()
    obj = object_text()
    pixels = generate_texture()
    validate(pss, obj, pixels)

    write_rgba_png(args.output / "contrail_v5_4_cloud.png", pixels)
    (args.output / "contrail_v5_4_billboard.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_billboard.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v5.4 ultra-real pooled billboard asset set.\n"
        "One 512x512 white ATTACH billboard is controlled by each XPLM instance.\n"
        "Multi-scale cloud lobes, feathered voids and deterministic rotation reduce repetition.\n"
        "No ribbon particles, streaming history, OBJ triangles or LIT texture.\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
