#!/usr/bin/env python3
"""Generate deterministic X-Plane native ribbon-particle contrail assets."""

from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

WIDTH = 256
HEIGHT = 256


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


def generate_texture() -> bytes:
    rng = random.Random(5001)
    phase_a = rng.uniform(0.0, math.tau)
    phase_b = rng.uniform(0.0, math.tau)
    lobes = [
        (
            rng.uniform(-0.35, 0.35),
            rng.uniform(0.0, math.tau),
            rng.uniform(0.08, 0.22),
            rng.uniform(0.08, 0.24),
        )
        for _ in range(28)
    ]

    pixels = bytearray()
    nonzero = 0
    for y in range(HEIGHT):
        v = (y + 0.5) / HEIGHT
        angle = v * math.tau
        for x in range(WIDTH):
            px = (2.0 * (x + 0.5) / WIDTH) - 1.0
            centre = 0.030 * math.sin(angle * 2.0 + phase_a)
            centre += 0.012 * math.sin(angle * 5.0 + phase_b)
            lateral = abs(px - centre)

            broad = math.exp(-((lateral / 0.38) ** 2))
            core = math.exp(-((lateral / 0.15) ** 2))
            edge = 1.0 - smoothstep(0.42, 0.82, lateral)

            detail = 0.0
            for lobe_x, lobe_phase, sigma, weight in lobes:
                phase_distance = abs(angle - lobe_phase)
                phase_distance = min(phase_distance, math.tau - phase_distance)
                distance2 = (px - lobe_x) ** 2 + (phase_distance / math.pi) ** 2
                detail += weight * math.exp(-distance2 / (2.0 * sigma * sigma))
            detail = min(detail / 2.2, 1.0)

            longitudinal = 0.90
            longitudinal += 0.06 * math.sin(angle * 3.0 + phase_a)
            longitudinal += 0.04 * math.sin(angle * 7.0 + phase_b)
            density = edge * longitudinal * (0.42 * broad + 0.42 * core + 0.16 * detail)
            density = min(max(density, 0.0), 1.0)

            if x < 8 or x >= WIDTH - 8:
                density = 0.0

            alpha = int(round(255.0 * density))
            if alpha > 0:
                nonzero += 1
            # Slightly cool neutral white; the X-Plane particle lighting model
            # controls final ambient/diffuse brightness.
            pixels.extend((248, 252, 255, alpha))

    if nonzero < WIDTH * HEIGHT * 0.20:
        raise RuntimeError("particle texture contains too little visible density")
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_particle.png
PARTICLE
 NAME ffatmo_ribbon_particle
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
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
SIZE_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.650000
	0.060000	0.850000
	0.350000	1.350000
	1.000000	2.100000
END_KEYFRAME_TABLE
ALPHA_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.000000
	0.012000	0.080000
	0.040000	0.500000
	0.120000	0.850000
	0.720000	0.720000
	1.000000	0.000000
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
 DIFFUSE 0.550000
 AMBIENT 0.950000
EMISSIVE
INTERP_MODE LINEAR
	0.000000	0.060000
	1.000000	0.030000
END_KEYFRAME_TABLE
TINT
INTERP_MODE LINEAR
	0.000000	0.980000	0.990000	1.000000
	1.000000	0.950000	0.980000	1.000000
END_KEYFRAME_TABLE
GRAVITY_CURVE
INTERP_MODE LINEAR
	0.000000	0.000000
	1.000000	0.000000
END_KEYFRAME_TABLE
TURBULENCE
INTERP_MODE CUBIC_AVG
	0.000000	0.005000
	0.250000	0.025000
	1.000000	0.090000
END_KEYFRAME_TABLE
DRAG_CURVE
INTERP_MODE CUBIC_AVG
	0.000000	0.550000
	0.250000	0.800000
	1.000000	1.000000
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
	1.000000	72.000000	72.000000
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
	0.000000	0.050000	0.120000
	1.000000	0.100000	0.220000
END_KEYFRAME_TABLE
INITIAL_ROTATION
INTERP_MODE LINEAR
	0.000000	0.000000	360.000000
	1.000000	0.000000	360.000000
END_KEYFRAME_TABLE
INITIAL_SIZE
SLOT 2
INTERP_MODE CUBIC_AVG
	0.000000	0.200000	0.200000
	0.080000	0.300000	0.350000
	1.000000	3.200000	3.600000
END_KEYFRAME_TABLE
INITIAL_ALPHA
SLOT 3
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	1.000000	1.000000
END_KEYFRAME_TABLE
TIME_TO_LIVE
SLOT 4
INTERP_MODE LINEAR
	0.000000	0.000000	0.000000
	1.000000	45.000000	45.000000
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
# FFAtmo Renderer Foundation v5 native particle-only contrail object
PARTICLE_SYSTEM contrail_v5.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_ribbon 0 0 0 0 0 0
"""


def validate_assets(pss: str, obj: str) -> None:
    required_pss = (
        "BILLBOARD_MODE RIBBON",
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
            raise RuntimeError(f"particle system is missing: {token}")
    if "PARTICLE_SYSTEM contrail_v5.pss" not in obj:
        raise RuntimeError("OBJ does not reference contrail_v5.pss")
    if "EMITTER ffatmo_ribbon" not in obj:
        raise RuntimeError("OBJ does not contain the native ribbon emitter")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("particle-only OBJ must not contain card geometry or LIT material")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    texture = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate_assets(pss, obj)

    write_rgba_png(args.output / "contrail_particle.png", texture)
    (args.output / "contrail_v5.pss").write_text(pss, encoding="utf-8", newline="\n")
    (args.output / "contrail_ribbon.obj").write_text(obj, encoding="utf-8", newline="\n")
    (args.output / "ASSET_INFO.txt").write_text(
        "FFAtmo Renderer Foundation v5.0 native X-Plane particle proof.\n"
        "Two plugin-managed XPLM instances use one particle-only OBJ.\n"
        "BILLBOARD_MODE RIBBON and EMIT_MODE STREAM provide continuous contrails.\n"
        "No OBJ card geometry, no TEXTURE_LIT material and no custom OpenGL drawing.\n"
        "The LevelUp cooling/nucleation model and Wake Fluid Simulation v1 remain active.\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
