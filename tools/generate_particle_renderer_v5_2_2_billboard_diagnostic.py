#!/usr/bin/env python3
"""Generate a billboard-only live colour isolation particle asset."""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from pathlib import Path

SIZE = 256


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


def generate_texture() -> bytes:
    """Pure-white soft cloud puff with feathered RGBA alpha."""
    pixels = bytearray()
    for y in range(SIZE):
        py = (2.0 * (y + 0.5) / SIZE) - 1.0
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0
            radius2 = px * px + py * py
            broad = math.exp(-radius2 / 0.42)
            core = math.exp(-radius2 / 0.10)
            breakup = 0.92 + 0.08 * math.sin(px * 11.0 + py * 7.0)
            density = max(0.0, min(1.0, (0.72 * broad + 0.28 * core) * breakup))
            if radius2 > 0.96:
                density *= max(0.0, (1.08 - radius2) / 0.12)
            alpha = int(round(215.0 * density))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_billboard_diag.png
PARTICLE
 NAME ffatmo_billboard_white
 MAX_PARTICLES 32768
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
INTERP_MODE CUBIC_AVG
 0.000000 0.700000
 0.120000 1.000000
 1.000000 2.600000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
 0.000000 0.000000
 0.040000 0.850000
 0.720000 0.620000
 1.000000 0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.000000
 AMBIENT 0.000000
EMISSIVE
INTERP_MODE LINEAR
 0.000000 1.000000
 1.000000 1.000000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
 0.000000 1.000000 1.000000 1.000000
 1.000000 1.000000 1.000000 1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
 0.000000 0.000000
 1.000000 0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE LINEAR
 0.000000 0.020000
 1.000000 0.080000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE LINEAR
 0.000000 0.900000
 1.000000 0.900000
END_KEYFRAME_TABLE
SPIN_CURVE
INTERP_MODE LINEAR
 0.000000 -8.000000
 1.000000 8.000000
END_KEYFRAME_TABLE
ELASTICITY
INTERP_MODE LINEAR
 0.000000 1.000000
 1.000000 1.000000
END_KEYFRAME_TABLE
 COLLISION_MODE NONE
END_PARTICLE
EMITTER
 NAME ffatmo_billboard_stream
 EMIT_MODE STREAM
SUB_EMITTER
 PARTICLE_TYPE 0
EMIT_RATE
SLOT 1
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 0.010000 0.000000 0.000000
 1.000000 34.000000 34.000000
END_KEYFRAME_TABLE
INITIAL_SPEED
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.000000 0.000000
END_KEYFRAME_TABLE
ROTATION_SPEED
INTERP_MODE LINEAR
 0.000000 -5.000000 5.000000
 1.000000 -5.000000 5.000000
END_KEYFRAME_TABLE
INITIAL_HEADING
INTERP_MODE LINEAR
 0.000000 0.000000 360.000000
 1.000000 0.000000 360.000000
END_KEYFRAME_TABLE
INITIAL_PITCH
INTERP_MODE LINEAR
 0.000000 -4.000000 4.000000
 1.000000 -4.000000 4.000000
END_KEYFRAME_TABLE
DX
INTERP_MODE LINEAR
 0.000000 -0.120000 0.120000
 1.000000 -0.120000 0.120000
END_KEYFRAME_TABLE
DY
INTERP_MODE LINEAR
 0.000000 -0.120000 0.120000
 1.000000 -0.120000 0.120000
END_KEYFRAME_TABLE
DZ
INTERP_MODE LINEAR
 0.000000 -0.120000 0.120000
 1.000000 -0.120000 0.120000
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
INTERP_MODE LINEAR
 0.000000 0.000000 360.000000
 1.000000 0.000000 360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE LINEAR
 0.000000 1.400000 1.400000
 1.000000 1.400000 1.400000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 0.900000 0.900000
END_KEYFRAME_TABLE
TIME_TO_LIVE
SLOT 4
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 18.000000 18.000000
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
# FFAtmo v5.2.2 billboard-only live colour diagnostic
PARTICLE_SYSTEM contrail_v5_2_2_billboard_diag.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_billboard_stream 0 0 0 0 0 0
"""


def validate(pss: str, obj: str) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 1:
        raise RuntimeError("diagnostic must contain one particle type")
    if lines.count("SUB_EMITTER") != 1:
        raise RuntimeError("diagnostic must contain one sub-emitter")
    if lines.count("BILLBOARD_MODE BILLBOARD") != 1:
        raise RuntimeError("billboard mode is missing")
    if lines.count("BILLBOARD_MODE RIBBON") != 0:
        raise RuntimeError("ribbon mode must not be present")
    if lines.count("BLEND_MODE NORMAL") != 1:
        raise RuntimeError("normal blend mode is missing")
    if "EMITTER ffatmo_billboard_stream" not in obj:
        raise RuntimeError("OBJ emitter attachment is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("diagnostic OBJ must remain particle-only")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj)
    write_rgba_png(args.output / "contrail_billboard_diag.png", generate_texture())
    (args.output / "contrail_v5_2_2_billboard_diag.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_ribbon.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo v5.2.2 billboard-only live colour diagnostic.\n"
        "One white emissive normal-blend billboard stream per engine.\n"
        "No ribbon particles and no OBJ triangle geometry.\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
