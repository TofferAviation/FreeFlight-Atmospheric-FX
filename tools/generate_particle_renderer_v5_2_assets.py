#!/usr/bin/env python3
"""Generate the v5.2 single-ribbon contrail realism asset set."""

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
    if edge0 == edge1:
        return 1.0 if value >= edge1 else 0.0
    ratio = min(max((value - edge0) / (edge1 - edge0), 0.0), 1.0)
    return ratio * ratio * (3.0 - 2.0 * ratio)


def periodic_distance(first: float, second: float) -> float:
    distance = abs(first - second)
    return min(distance, 1.0 - distance)


def generate_texture() -> bytes:
    rng = random.Random(5200)
    phase_a = rng.uniform(0.0, math.tau)
    phase_b = rng.uniform(0.0, math.tau)
    lobes = [
        (
            rng.uniform(-0.42, 0.42),
            rng.uniform(0.0, 1.0),
            rng.uniform(0.055, 0.16),
            rng.uniform(0.10, 0.30),
        )
        for _ in range(34)
    ]

    pixels = bytearray()
    nonzero = 0
    centre_support = 0

    for y in range(SIZE):
        v = (y + 0.5) / SIZE
        angle = v * math.tau
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0
            centre = 0.030 * math.sin(angle * 2.0 + phase_a)
            centre += 0.014 * math.sin(angle * 5.0 + phase_b)
            lateral = abs(px - centre)

            broad = math.exp(-((lateral / 0.50) ** 2))
            middle = math.exp(-((lateral / 0.265) ** 2))
            core = math.exp(-((lateral / 0.105) ** 2))
            edge = 1.0 - smoothstep(0.38, 0.92, lateral)

            cellular = 0.0
            for lobe_x, lobe_v, sigma, weight in lobes:
                dv = periodic_distance(v, lobe_v)
                distance2 = (px - lobe_x) ** 2 + (dv * 2.0) ** 2
                cellular += weight * math.exp(-distance2 / (2.0 * sigma * sigma))
            cellular = min(cellular / 1.7, 1.0)

            longitudinal = 0.86
            longitudinal += 0.075 * math.sin(angle * 3.0 + phase_a)
            longitudinal += 0.045 * math.sin(angle * 7.0 + phase_b)
            longitudinal += 0.025 * math.sin(angle * 13.0 + 0.7)

            density = edge * longitudinal * (
                0.38 * broad + 0.35 * middle + 0.17 * core + 0.10 * cellular
            )
            # Small, soft density notches create ice-cloud variation without
            # breaking the connected ribbon centreline.
            notch = 1.0 - 0.16 * max(
                0.0,
                math.sin(angle * 4.0 + phase_b) * math.sin(angle * 9.0 + phase_a),
            )
            density *= notch
            density = min(max(density, 0.0), 1.0)

            if x < 4 or x >= SIZE - 4:
                density = 0.0

            # Keep normal-alpha blending, but use a slightly lower maximum alpha
            # than v5.1.2. Brightness comes from the PSS emissive curve rather
            # than from an opaque black-looking ribbon.
            alpha = int(round(205.0 * density))
            if alpha > 0:
                nonzero += 1
            if abs(px) < 0.035 and alpha > 12:
                centre_support += 1
            pixels.extend((255, 255, 255, alpha))

    if nonzero < SIZE * SIZE * 0.20:
        raise RuntimeError("v5.2 texture has too little visible cloud density")
    if centre_support < SIZE * 0.80:
        raise RuntimeError("v5.2 texture does not preserve a continuous centreline")
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_particle_v52.png
PARTICLE
 NAME ffatmo_ribbon_ice_cloud
 MAX_PARTICLES 24576
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
\t0.000000\t0.720000
\t0.035000\t0.900000
\t0.120000\t1.180000
\t0.420000\t1.850000
\t1.000000\t3.250000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.000000
\t0.012000\t0.080000
\t0.045000\t0.520000
\t0.120000\t0.820000
\t0.480000\t0.680000
\t0.780000\t0.360000
\t0.930000\t0.140000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.000000
 AMBIENT 0.000000
EMISSIVE
INTERP_MODE CUBIC_AVG
\t0.000000\t1.000000
\t0.180000\t0.920000
\t0.600000\t0.680000
\t1.000000\t0.320000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
\t0.000000\t1.000000\t1.000000\t1.000000
\t0.650000\t0.985000\t0.995000\t1.000000
\t1.000000\t0.940000\t0.970000\t1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
\t0.000000\t0.000000
\t1.000000\t0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.001500
\t0.250000\t0.008000
\t0.650000\t0.028000
\t1.000000\t0.065000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE CUBIC_AVG
\t0.000000\t0.700000
\t0.400000\t0.880000
\t1.000000\t1.000000
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
\t1.000000\t96.000000\t96.000000
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
\t0.000000\t0.010000\t0.035000
\t1.000000\t0.035000\t0.085000
END_KEYFRAME_TABLE
INITIAL_ROTATION
INTERP_MODE LINEAR
\t0.000000\t0.000000\t360.000000
\t1.000000\t0.000000\t360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE CUBIC_AVG
\t0.000000\t0.650000\t0.650000
\t0.350000\t1.300000\t1.450000
\t1.000000\t3.900000\t4.400000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t0.920000\t0.920000
END_KEYFRAME_TABLE
TIME_TO_LIVE
SLOT 4
INTERP_MODE LINEAR
\t0.000000\t0.000000\t0.000000
\t1.000000\t55.000000\t55.000000
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
# FFAtmo Renderer Foundation v5.2 single-ribbon realism pass
PARTICLE_SYSTEM contrail_v5_2.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_ribbon 0 0 0 0 0 0
"""


def validate(pss: str, obj: str) -> None:
    if sum(line.strip() == "SUB_EMITTER" for line in pss.splitlines()) != 1:
        raise RuntimeError("v5.2 must retain exactly one sub-emitter")
    if pss.count("BILLBOARD_MODE RIBBON") != 1:
        raise RuntimeError("v5.2 ribbon particle is missing")
    if "BILLBOARD_MODE BILLBOARD" in pss:
        raise RuntimeError("v5.2 must not reintroduce the failed billboard halo")
    if "BLEND_MODE NORMAL" not in pss:
        raise RuntimeError("v5.2 must use stable normal-alpha blending")
    if "EMISSIVE\nINTERP_MODE CUBIC_AVG\n\t0.000000\t1.000000" not in pss:
        raise RuntimeError("v5.2 full white formation lighting is missing")
    if "PARTICLE_SYSTEM contrail_v5_2.pss" not in obj:
        raise RuntimeError("v5.2 OBJ reference is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.2 particle object contains legacy geometry")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    texture = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj)

    write_rgba_png(args.output / "contrail_particle_v52.png", texture)
    (args.output / "contrail_v5_2.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_ribbon.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v5.2 single-ribbon realism pass.\n"
        "One proven streaming ribbon per CFM56; no mixed sub-emitter path.\n"
        "White emissive formation lighting, feathered periodic cloud density, broader age growth and gentle turbulence.\n"
        "LevelUp geometry, cooling/nucleation and Wake Fluid Simulation v1 are unchanged.\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
