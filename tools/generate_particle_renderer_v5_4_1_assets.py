#!/usr/bin/env python3
"""Generate deterministic Renderer Foundation v5.4.1 stable-cloud assets."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

SIZE = 512
MAX_ALPHA = 145


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
    """Pure-white broken ice puff with low optical density and soft edges."""
    rng = random.Random(5410)
    lobes = [
        (
            rng.uniform(-0.42, 0.42),
            rng.uniform(-0.38, 0.38),
            rng.uniform(0.12, 0.34),
            rng.uniform(0.10, 0.31),
            rng.uniform(0.08, 0.26),
        )
        for _ in range(30)
    ]
    details = [
        (
            rng.uniform(-0.58, 0.58),
            rng.uniform(-0.54, 0.54),
            rng.uniform(0.045, 0.13),
            rng.uniform(0.040, 0.12),
            rng.uniform(0.018, 0.075),
        )
        for _ in range(48)
    ]
    holes = [
        (
            rng.uniform(-0.44, 0.44),
            rng.uniform(-0.42, 0.42),
            rng.uniform(0.06, 0.17),
            rng.uniform(0.06, 0.16),
            rng.uniform(0.05, 0.16),
        )
        for _ in range(18)
    ]

    pixels = bytearray()
    for y in range(SIZE):
        py = (2.0 * (y + 0.5) / SIZE) - 1.0
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0
            radius2 = (px * 0.96) ** 2 + (py * 1.04) ** 2
            broad = math.exp(-radius2 / 0.52)
            centre = math.exp(-radius2 / 0.15)

            lobe_density = 0.0
            for cx, cy, sx, sy, weight in lobes:
                lobe_density += weight * gaussian(px, py, cx, cy, sx, sy)
            lobe_density = min(lobe_density / 2.45, 1.0)

            detail_density = 0.0
            for cx, cy, sx, sy, weight in details:
                detail_density += weight * gaussian(px, py, cx, cy, sx, sy)
            detail_density = min(detail_density / 1.30, 1.0)

            void_density = 0.0
            for cx, cy, sx, sy, weight in holes:
                void_density += weight * gaussian(px, py, cx, cy, sx, sy)
            void_density = min(void_density / 1.15, 0.38)

            wave = (
                0.92
                + 0.035 * math.sin(px * 11.0 + py * 7.0)
                + 0.025 * math.sin(px * 23.0 - py * 19.0)
                + 0.015 * math.sin(px * 41.0 + py * 29.0)
            )
            density = (
                0.30 * broad
                + 0.14 * centre
                + 0.36 * lobe_density
                + 0.20 * detail_density
                - void_density
            ) * wave
            edge = max(0.0, min(1.0, (1.12 - radius2) / 0.38))
            density = max(0.0, min(1.0, density * edge))
            alpha = int(round(MAX_ALPHA * density))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_v5_4_1_cloud.png
PARTICLE
 NAME ffatmo_v541_ice_cloud
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
 DIFFUSE 0.220000
 AMBIENT 0.300000
EMISSIVE
INTERP_MODE LINEAR
 0.000000 0.700000
 1.000000 0.700000
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
 NAME ffatmo_v541_attached_cloud
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
 0.000000 0.300000 0.300000
 1.000000 12.000000 12.000000
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
# FFAtmo Renderer Foundation v5.4.1 stable pooled billboards
PARTICLE_SYSTEM contrail_v5_4_1_billboard.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_v541_attached_cloud 0 0 0 0 0 0
"""


def validate(pss: str, obj: str, pixels: bytes) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 1:
        raise RuntimeError("v5.4.1 requires exactly one particle type")
    if lines.count("SUB_EMITTER") != 1:
        raise RuntimeError("v5.4.1 requires exactly one sub-emitter")
    if lines.count("BILLBOARD_MODE BILLBOARD") != 1:
        raise RuntimeError("v5.4.1 billboard mode is missing")
    if lines.count("BILLBOARD_MODE RIBBON") != 0:
        raise RuntimeError("Ribbon mode must not return")
    if lines.count("EMIT_MODE ATTACH") != 1:
        raise RuntimeError("v5.4.1 must use one attached particle")
    if "EMITTER ffatmo_v541_attached_cloud" not in obj:
        raise RuntimeError("v5.4.1 OBJ emitter attachment is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.4.1 OBJ must remain particle-only")
    if len(pixels) != SIZE * SIZE * 4:
        raise RuntimeError("v5.4.1 RGBA texture size is invalid")
    if any(value != 255 for channel in (pixels[0::4], pixels[1::4], pixels[2::4]) for value in channel):
        raise RuntimeError("v5.4.1 cloud RGB must remain pure white")
    alpha = pixels[3::4]
    if max(alpha) < 70 or max(alpha) > MAX_ALPHA:
        raise RuntimeError("v5.4.1 cloud peak alpha is outside the stable range")
    nonzero = sum(value > 0 for value in alpha)
    if nonzero < SIZE * SIZE * 0.16:
        raise RuntimeError("v5.4.1 cloud contains too little broken structure")
    for x, y in ((0, 0), (SIZE - 1, 0), (0, SIZE - 1), (SIZE - 1, SIZE - 1)):
        if alpha[y * SIZE + x] != 0:
            raise RuntimeError("v5.4.1 cloud corners must be transparent")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    pixels = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj, pixels)

    write_rgba_png(args.output / "contrail_v5_4_1_cloud.png", pixels)
    (args.output / "contrail_v5_4_1_billboard.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_billboard.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v5.4.1 stable billboard asset set.\n"
        "One low-opacity ATTACH billboard is controlled by each persistent XPLM instance.\n"
        "Texture RGB is pure white; alpha is capped for layered ice-cloud blending.\n"
        "No Ribbon particles, streaming history, OBJ triangles or LIT texture.\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
