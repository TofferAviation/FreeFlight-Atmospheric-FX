#!/usr/bin/env python3
"""Generate the v5.1.2 single-ribbon native contrail asset set."""

from __future__ import annotations

import argparse
import math
import random
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


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    ratio = min(max((value - edge0) / (edge1 - edge0), 0.0), 1.0)
    return ratio * ratio * (3.0 - 2.0 * ratio)


def generate_texture() -> bytes:
    rng = random.Random(5120)
    phase_a = rng.uniform(0.0, math.tau)
    phase_b = rng.uniform(0.0, math.tau)
    pixels = bytearray()
    nonzero = 0

    for y in range(SIZE):
        v = (y + 0.5) / SIZE
        angle = v * math.tau
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0
            centre = 0.025 * math.sin(angle * 2.0 + phase_a)
            centre += 0.012 * math.sin(angle * 5.0 + phase_b)
            lateral = abs(px - centre)

            broad = math.exp(-((lateral / 0.46) ** 2))
            middle = math.exp(-((lateral / 0.24) ** 2))
            core = math.exp(-((lateral / 0.105) ** 2))
            edge = 1.0 - smoothstep(0.48, 0.90, lateral)
            detail = 0.91
            detail += 0.055 * math.sin(angle * 3.0 + phase_a)
            detail += 0.035 * math.sin(angle * 7.0 + phase_b)
            detail += 0.020 * math.sin(angle * 13.0 + 1.2)

            density = edge * detail * (
                0.40 * broad + 0.38 * middle + 0.22 * core
            )
            density = min(max(density, 0.0), 1.0)
            if x < 5 or x >= SIZE - 5:
                density = 0.0

            alpha = int(round(235.0 * density))
            nonzero += int(alpha > 0)
            pixels.extend((252, 254, 255, alpha))

    if nonzero < SIZE * SIZE * 0.20:
        raise RuntimeError("v5.1.2 ribbon texture has too little visible density")
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_particle_v512.png
PARTICLE
 NAME ffatmo_ribbon_soft
 MAX_PARTICLES 16384
 BILLBOARD_MODE RIBBON
 BLEND_MODE NORMAL
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
SIZE_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.850000
\t0.080000\t1.050000
\t0.350000\t1.550000
\t1.000000\t2.550000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.000000
\t0.020000\t0.180000
\t0.080000\t0.620000
\t0.520000\t0.520000
\t0.850000\t0.360000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.180000
 AMBIENT 1.000000
EMISSIVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.420000
\t0.550000\t0.340000
\t1.000000\t0.180000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
\t0.000000\t1.000000\t1.000000\t1.000000
\t1.000000\t0.960000\t0.985000\t1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.003000
\t0.300000\t0.015000
\t1.000000\t0.055000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.650000
\t1.000000\t0.950000
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
EMITTER
 NAME ffatmo_ribbon
 EMIT_MODE STREAM
SUB_EMITTER
 PARTICLE_TYPE 0
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
\t0.000000\t0.020000\t0.050000
\t1.000000\t0.060000\t0.120000
END_KEYFRAME_TABLE
INITIAL_ROTATION
INTERP_MODE LINEAR
\t0.000000\t0.000000\t360.000000
\t1.000000\t0.000000\t360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE CUBIC_AVG
\t0.000000\t0.700000\t0.700000
\t0.350000\t1.450000\t1.650000
\t1.000000\t4.200000\t4.800000
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
\t1.000000\t50.000000\t50.000000
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
# FFAtmo Renderer Foundation v5.1.2 single-ribbon emission hotfix
PARTICLE_SYSTEM contrail_v5_1_2.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_ribbon 0 0 0 0 0 0
"""


def validate(pss: str, obj: str) -> None:
    if sum(line.strip() == "SUB_EMITTER" for line in pss.splitlines()) != 1:
        raise RuntimeError("v5.1.2 must contain one sub-emitter")
    if pss.count("BILLBOARD_MODE RIBBON") != 1:
        raise RuntimeError("v5.1.2 ribbon particle is missing")
    if "BILLBOARD_MODE BILLBOARD" in pss:
        raise RuntimeError("v5.1.2 must not contain the mixed billboard halo")
    if "PARTICLE_SYSTEM contrail_v5_1_2.pss" not in obj:
        raise RuntimeError("v5.1.2 OBJ reference is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.1.2 particle object contains legacy geometry")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    texture = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj)

    write_rgba_png(args.output / "contrail_particle_v512.png", texture)
    (args.output / "contrail_v5_1_2.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_ribbon.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v5.1.2 emission hotfix.\n"
        "Single native ribbon particle, one streaming sub-emitter and one texture cell.\n"
        "The mixed v5.1 ribbon-plus-billboard path is deferred to a separate emitter test.\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
