#!/usr/bin/env python3
"""Generate a three-path native ribbon colour-isolation asset set."""

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
    """Pure-white RGB with a broad feathered alpha profile."""
    pixels = bytearray()
    for y in range(SIZE):
        phase = (y + 0.5) / SIZE * math.tau
        wobble = 0.025 * math.sin(phase * 3.0)
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0
            lateral = abs(px - wobble)
            core = math.exp(-((lateral / 0.20) ** 2))
            body = math.exp(-((lateral / 0.48) ** 2))
            edge = max(0.0, 1.0 - max(0.0, (lateral - 0.50) / 0.40) ** 2)
            density = min(max(edge * (0.62 * body + 0.38 * core), 0.0), 1.0)
            if x < 6 or x >= SIZE - 6:
                density = 0.0
            alpha = int(round(210.0 * density))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)


def curve_block() -> str:
    return """SIZE_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.900000
\t0.120000\t1.150000
\t1.000000\t2.250000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.000000
\t0.030000\t0.650000
\t0.750000\t0.650000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
"""


def common_tail() -> str:
    return """GRAVITY_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.850000
\t1.000000\t0.850000
END_KEYFRAME_TABLE
SPIN_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
ELASTICITY
INTERP_MODE LINEAR
\t0.000000\t1.000000
\t1.000000\t1.000000
END_KEYFRAME_TABLE
 COLLISION_MODE NONE
END_PARTICLE
"""


def particle(name: str, blend: str, diffuse: float, ambient: float,
             emissive: float, tint: tuple[float, float, float]) -> str:
    r, g, b = tint
    return f"""PARTICLE
 NAME {name}
 MAX_PARTICLES 8192
 BILLBOARD_MODE RIBBON
 BLEND_MODE {blend}
 TEX_CELLS_X 1
 TEX_CELLS_Y 1
 ANIM_CELL_START 0
 ANIM_CELL_COUNT 1
 ANIM_CELL_REPEAT 1
 ANIM_CELL_RANDOM 0
ANIM_CELL_KF
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
{curve_block()} DIFFUSE {diffuse:.6f}
 AMBIENT {ambient:.6f}
EMISSIVE
INTERP_MODE LINEAR
\t0.000000\t{emissive:.6f}
\t1.000000\t{emissive:.6f}
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
\t0.000000\t{r:.6f}\t{g:.6f}\t{b:.6f}
\t1.000000\t{r:.6f}\t{g:.6f}\t{b:.6f}
END_KEYFRAME_TABLE
{common_tail()}"""


def emitter(name: str, particle_type: int) -> str:
    return f"""EMITTER
 NAME {name}
 EMIT_MODE STREAM
SUB_EMITTER
 PARTICLE_TYPE {particle_type}
EMIT_RATE
SLOT 1
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t0.010000\t0.000000\t0.000000
\t1.000000\t72.000000\t72.000000
END_KEYFRAME_TABLE
INITIAL_SPEED
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
ROTATION_SPEED
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
INITIAL_HEADING
INTERP_MODE LINEAR
\t0.000000\t180.000000\t180.000000
\t1.000000\t180.000000\t180.000000
END_KEYFRAME_TABLE
INITIAL_PITCH
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
DX
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
DY
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
DZ
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
DLON
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
DLAT
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
INITIAL_ROTATION
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.000000\t0.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE LINEAR
\t0.000000\t1.600000\t1.600000
\t1.000000\t1.600000\t1.600000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.900000\t0.900000
END_KEYFRAME_TABLE
TIME_TO_LIVE
SLOT 4
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t30.000000\t30.000000
END_KEYFRAME_TABLE
END_SUB_EMITTER
DATAREFS 5
DREF 
DREF ffatmo/contrail_particle/rate
DREF ffatmo/contrail_particle/size
DREF ffatmo/contrail_particle/alpha
DREF ffatmo/contrail_particle/lifetime
END_EMITTER
"""


def particle_system_text() -> str:
    return (
        "A\n1000\nPARTICLE_SYSTEM\n\n TEXTURE contrail_colour_diag.png\n"
        + particle(
            "ffatmo_diag_world_lit_red", "NORMAL", 1.0, 1.0, 0.0,
            (1.0, 0.05, 0.05),
        )
        + particle(
            "ffatmo_diag_emissive_green", "NORMAL", 0.0, 0.0, 1.0,
            (0.05, 1.0, 0.05),
        )
        + particle(
            "ffatmo_diag_additive_blue", "ADDITIVE", 0.0, 0.0, 1.0,
            (0.05, 0.20, 1.0),
        )
        + emitter("ffatmo_diag_world_lit", 0)
        + emitter("ffatmo_diag_emissive", 1)
        + emitter("ffatmo_diag_additive", 2)
        + " TEX_CELLS_X 1\n TEX_CELLS_Y 1\nDATAREFS 0\nEND_PARTICLE_SYSTEM\n"
    )


def object_text() -> str:
    return """I
800
OBJ
# FFAtmo v5.2.1 colour-isolation diagnostic
PARTICLE_SYSTEM contrail_v5_2_1_colour_diag.pss
POINT_COUNTS 0 0 0 0
# Vertical separation makes all three material paths visible in one run.
EMITTER ffatmo_diag_world_lit 0 3 0 0 0 0
EMITTER ffatmo_diag_emissive 0 0 0 0 0 0
EMITTER ffatmo_diag_additive 0 -3 0 0 0 0
"""


def validate(pss: str, obj: str) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 3:
        raise RuntimeError("diagnostic must contain three particle types")
    if lines.count("SUB_EMITTER") != 3:
        raise RuntimeError("diagnostic must contain three separate sub-emitters")
    if lines.count("BILLBOARD_MODE RIBBON") != 3:
        raise RuntimeError("all diagnostic particles must remain ribbons")
    if lines.count("BLEND_MODE NORMAL") != 2:
        raise RuntimeError("two normal-blend paths are required")
    if lines.count("BLEND_MODE ADDITIVE") != 1:
        raise RuntimeError("one additive path is required")
    if obj.count("EMITTER ffatmo_diag_") != 3:
        raise RuntimeError("OBJ must attach all three diagnostic emitters")
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
    write_rgba_png(args.output / "contrail_colour_diag.png", generate_texture())
    (args.output / "contrail_v5_2_1_colour_diag.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_ribbon.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo v5.2.1 colour-isolation diagnostic.\n"
        "Top red: NORMAL blend with AMBIENT=1 and DIFFUSE=1.\n"
        "Middle green: NORMAL blend with EMISSIVE=1 only.\n"
        "Bottom blue: ADDITIVE blend with EMISSIVE=1.\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
