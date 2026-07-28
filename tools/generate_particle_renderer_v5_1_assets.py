#!/usr/bin/env python3
"""Generate deterministic X-Plane native v5.1 contrail particle assets."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

CELL_SIZE = 256
WIDTH = CELL_SIZE * 2
HEIGHT = CELL_SIZE


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, pixels: bytes) -> None:
    rows = bytearray()
    stride = WIDTH * 4
    for row in range(HEIGHT):
        rows.append(0)
        start = row * stride
        rows.extend(pixels[start : start + stride])
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(
        b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    )
    data += png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = min(max((value - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def ribbon_density(x: int, y: int, rng: random.Random) -> float:
    px = (2.0 * (x + 0.5) / CELL_SIZE) - 1.0
    v = (y + 0.5) / CELL_SIZE
    angle = v * math.tau
    centre = 0.025 * math.sin(angle * 2.0 + 0.8)
    centre += 0.012 * math.sin(angle * 5.0 + 2.1)
    lateral = abs(px - centre)

    broad = math.exp(-((lateral / 0.46) ** 2))
    middle = math.exp(-((lateral / 0.24) ** 2))
    core = math.exp(-((lateral / 0.105) ** 2))
    edge = 1.0 - smoothstep(0.48, 0.90, lateral)

    periodic_detail = 0.91
    periodic_detail += 0.055 * math.sin(angle * 3.0 + 0.7)
    periodic_detail += 0.035 * math.sin(angle * 7.0 + 2.4)
    periodic_detail += 0.020 * math.sin(angle * 13.0 + 1.2)

    density = edge * periodic_detail * (
        0.40 * broad + 0.38 * middle + 0.22 * core
    )
    if x < 5 or x >= CELL_SIZE - 5:
        density = 0.0
    return min(max(density, 0.0), 1.0)


def halo_density(x: int, y: int, lobes: list[tuple[float, float, float, float]]) -> float:
    px = (2.0 * (x + 0.5) / CELL_SIZE) - 1.0
    py = (2.0 * (y + 0.5) / CELL_SIZE) - 1.0
    radius = math.sqrt(px * px + py * py)
    envelope = 1.0 - smoothstep(0.52, 0.98, radius)
    broad = math.exp(-((radius / 0.58) ** 2))
    centre = math.exp(-((radius / 0.27) ** 2))

    detail = 0.0
    for lx, ly, sigma, weight in lobes:
        distance2 = (px - lx) ** 2 + (py - ly) ** 2
        detail += weight * math.exp(-distance2 / (2.0 * sigma * sigma))
    detail = min(detail / 2.4, 1.0)

    density = envelope * (0.48 * broad + 0.25 * centre + 0.27 * detail)
    return min(max(density, 0.0), 1.0)


def generate_texture() -> bytes:
    rng = random.Random(5101)
    lobes = [
        (
            rng.uniform(-0.42, 0.42),
            rng.uniform(-0.42, 0.42),
            rng.uniform(0.10, 0.28),
            rng.uniform(0.10, 0.30),
        )
        for _ in range(32)
    ]

    pixels = bytearray()
    ribbon_nonzero = 0
    halo_nonzero = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if x < CELL_SIZE:
                density = ribbon_density(x, y, rng)
                ribbon_nonzero += int(density > 0.0)
                alpha = int(round(235.0 * density))
                rgb = (252, 254, 255)
            else:
                density = halo_density(x - CELL_SIZE, y, lobes)
                halo_nonzero += int(density > 0.0)
                alpha = int(round(205.0 * density))
                rgb = (248, 252, 255)
            pixels.extend((*rgb, alpha))

    minimum = CELL_SIZE * CELL_SIZE * 0.20
    if ribbon_nonzero < minimum or halo_nonzero < minimum:
        raise RuntimeError("v5.1 texture cells contain too little visible density")
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_particle_v51.png
PARTICLE
 NAME ffatmo_ribbon_core
 MAX_PARTICLES 12000
 BILLBOARD_MODE RIBBON
 BLEND_MODE NORMAL
 TEX_CELLS_X 2
 TEX_CELLS_Y 1
 ANIM_CELL_START 0
 ANIM_CELL_COUNT 1
 ANIM_CELL_REPEAT 1
 ANIM_CELL_RANDOM 0
ANIM_CELL_KF
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
SIZE_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.850000
	0.080000	1.050000
	0.350000	1.550000
	1.000000	2.550000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.000000
	0.020000	0.180000
	0.080000	0.620000
	0.520000	0.520000
	0.850000	0.360000
	1.000000	0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.180000
 AMBIENT 1.000000
EMISSIVE
INTERP_MODE CUBIC_AVG
	0.000000	0.420000
	0.550000	0.340000
	1.000000	0.180000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
	0.000000	1.000000	1.000000	1.000000
	1.000000	0.960000	0.985000	1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE CUBIC_AVG
	0.000000	0.003000
	0.300000	0.015000
	1.000000	0.055000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.650000
	1.000000	0.950000
END_KEYFRAME_TABLE
SPIN_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
ELASTICITY
INTERP_MODE LINEAR
	0.000000	1.000000
	1.000000	1.000000
END_KEYFRAME_TABLE
 COLLISION_MODE NONE
END_PARTICLE

PARTICLE
 NAME ffatmo_cloud_halo
 MAX_PARTICLES 6000
 BILLBOARD_MODE BILLBOARD
 BLEND_MODE NORMAL
 TEX_CELLS_X 2
 TEX_CELLS_Y 1
 ANIM_CELL_START 1
 ANIM_CELL_COUNT 1
 ANIM_CELL_REPEAT 1
 ANIM_CELL_RANDOM 0
ANIM_CELL_KF
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
SIZE_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.700000
	0.180000	1.350000
	0.600000	2.100000
	1.000000	2.850000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.000000
	0.040000	0.150000
	0.180000	0.260000
	0.650000	0.170000
	1.000000	0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.150000
 AMBIENT 1.000000
EMISSIVE
INTERP_MODE CUBIC_AVG
	0.000000	0.300000
	0.500000	0.220000
	1.000000	0.100000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
	0.000000	1.000000	1.000000	1.000000
	1.000000	0.950000	0.980000	1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE CUBIC_AVG
	0.000000	0.018000
	0.350000	0.060000
	1.000000	0.150000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.720000
	1.000000	1.000000
END_KEYFRAME_TABLE
SPIN_CURVE
INTERP_MODE LINEAR
	0.000000	-0.080000
	1.000000	0.080000
END_KEYFRAME_TABLE
ELASTICITY
INTERP_MODE LINEAR
	0.000000	1.000000
	1.000000	1.000000
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
	0.000000	0.000000	0.000000
	0.010000	0.000000	0.000000
	1.000000	52.000000	52.000000
END_KEYFRAME_TABLE
INITIAL_SPEED
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
ROTATION_SPEED
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
INITIAL_HEADING
INTERP_MODE LINEAR
	0.000000	180.000000	180.000000
	1.000000	180.000000	180.000000
END_KEYFRAME_TABLE
INITIAL_PITCH
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DX
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DY
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DZ
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DLON
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DLAT
INTERP_MODE LINEAR
	0.000000	0.020000	0.050000
	1.000000	0.060000	0.120000
END_KEYFRAME_TABLE
INITIAL_ROTATION
INTERP_MODE LINEAR
	0.000000	0.000000	360.000000
	1.000000	0.000000	360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE CUBIC_AVG
	0.000000	0.700000	0.700000
	0.350000	1.450000	1.650000
	1.000000	4.200000	4.800000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.900000	0.900000
END_KEYFRAME_TABLE
TIME_TO_LIVE
SLOT 4
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	50.000000	50.000000
END_KEYFRAME_TABLE
END_SUB_EMITTER

SUB_EMITTER
 PARTICLE_TYPE 1
EMIT_RATE
SLOT 1
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	0.010000	0.000000	0.000000
	1.000000	11.000000	14.000000
END_KEYFRAME_TABLE
INITIAL_SPEED
INTERP_MODE LINEAR
	0.000000	0.000000	0.080000
	1.000000	0.150000	0.450000
END_KEYFRAME_TABLE
ROTATION_SPEED
INTERP_MODE LINEAR
	0.000000	-0.120000	0.120000
	1.000000	-0.250000	0.250000
END_KEYFRAME_TABLE
INITIAL_HEADING
INTERP_MODE LINEAR
	0.000000	0.000000	360.000000
	1.000000	0.000000	360.000000
END_KEYFRAME_TABLE
INITIAL_PITCH
INTERP_MODE LINEAR
	0.000000	-20.000000	20.000000
	1.000000	-35.000000	35.000000
END_KEYFRAME_TABLE
DX
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DY
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DZ
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.000000	0.000000
END_KEYFRAME_TABLE
DLON
INTERP_MODE LINEAR
	0.000000	-0.080000	0.080000
	1.000000	-0.200000	0.200000
END_KEYFRAME_TABLE
DLAT
INTERP_MODE LINEAR
	0.000000	0.120000	0.320000
	1.000000	0.250000	0.650000
END_KEYFRAME_TABLE
INITIAL_ROTATION
INTERP_MODE LINEAR
	0.000000	0.000000	360.000000
	1.000000	0.000000	360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE CUBIC_AVG
	0.000000	0.500000	0.700000
	0.350000	1.200000	1.700000
	1.000000	3.800000	5.200000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	0.460000	0.620000
END_KEYFRAME_TABLE
TIME_TO_LIVE
SLOT 4
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	18.000000	24.000000
END_KEYFRAME_TABLE
END_SUB_EMITTER
DATAREFS 5
DREF
DREF ffatmo/contrail_particle/rate
DREF ffatmo/contrail_particle/size
DREF ffatmo/contrail_particle/alpha
DREF ffatmo/contrail_particle/lifetime
END_EMITTER
 TEX_CELLS_X 2
 TEX_CELLS_Y 1
DATAREFS 0
END_PARTICLE_SYSTEM
"""


def object_text() -> str:
    return """I
800
OBJ
# FFAtmo Renderer Foundation v5.1 native soft-ribbon contrail object
PARTICLE_SYSTEM contrail_v5_1.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_ribbon 0 0 0 0 0 0
"""


def validate_assets(pss: str, obj: str) -> None:
    required_pss = (
        "NAME ffatmo_ribbon_core",
        "NAME ffatmo_cloud_halo",
        "BILLBOARD_MODE RIBBON",
        "BILLBOARD_MODE BILLBOARD",
        "BLEND_MODE NORMAL",
        "EMIT_MODE STREAM",
        "NAME ffatmo_ribbon",
        "DREF ffatmo/contrail_particle/rate",
        "DREF ffatmo/contrail_particle/size",
        "DREF ffatmo/contrail_particle/alpha",
        "DREF ffatmo/contrail_particle/lifetime",
    )
    for token in required_pss:
        if token not in pss:
            raise RuntimeError(f"v5.1 particle system is missing: {token}")
    if pss.count("SUB_EMITTER") != 2:
        raise RuntimeError("v5.1 must contain exactly two sub-emitters")
    if "PARTICLE_SYSTEM contrail_v5_1.pss" not in obj:
        raise RuntimeError("OBJ does not reference contrail_v5_1.pss")
    if "EMITTER ffatmo_ribbon" not in obj:
        raise RuntimeError("OBJ does not contain the native emitter")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("particle-only OBJ must not contain card geometry")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    texture = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate_assets(pss, obj)

    write_rgba_png(args.output / "contrail_particle_v51.png", texture)
    (args.output / "contrail_v5_1.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output / "contrail_ribbon.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v5.1 native particle asset set.\n"
        "One native ribbon core and one sparse billboard cloud halo.\n"
        "Normal alpha blending, cool-white texture cells and no OBJ triangles.\n"
        "The safe-start deferred object-loading path remains mandatory.\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
