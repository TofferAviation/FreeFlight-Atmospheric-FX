#!/usr/bin/env python3
"""Generate v5.7.1 billboard-only projected cloud assets."""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

SIZE = 1024
MAX_ALPHA = 236


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
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def gaussian(x: float, y: float, cx: float, cy: float, sx: float, sy: float) -> float:
    dx = (x - cx) / sx
    dy = (y - cy) / sy
    return math.exp(-0.5 * (dx * dx + dy * dy))


def generate_texture() -> bytes:
    """Elongated fluffy ice cloud with low-frequency alpha that survives mips."""
    rng = random.Random(5710)
    lobes = []
    for index in range(20):
        progress = index / 19.0
        cx = -0.78 + 1.56 * progress + rng.uniform(-0.055, 0.055)
        cy = rng.uniform(-0.13, 0.13)
        lobes.append((
            cx,
            cy,
            rng.uniform(0.12, 0.26),
            rng.uniform(0.075, 0.19),
            rng.uniform(0.18, 0.38),
        ))
    wisps = [
        (
            rng.uniform(-0.82, 0.82),
            rng.uniform(-0.23, 0.23),
            rng.uniform(0.07, 0.16),
            rng.uniform(0.045, 0.11),
            rng.uniform(0.035, 0.11),
        )
        for _ in range(30)
    ]
    holes = [
        (
            rng.uniform(-0.72, 0.72),
            rng.uniform(-0.13, 0.13),
            rng.uniform(0.06, 0.16),
            rng.uniform(0.045, 0.10),
            rng.uniform(0.025, 0.075),
        )
        for _ in range(12)
    ]

    pixels = bytearray()
    for y in range(SIZE):
        py = (2.0 * (y + 0.5) / SIZE) - 1.0
        for x in range(SIZE):
            px = (2.0 * (x + 0.5) / SIZE) - 1.0

            # Broad connected body and denser inner spine.
            body = math.exp(-0.5 * ((px / 0.78) ** 6 + (py / 0.205) ** 2))
            spine = math.exp(-0.5 * ((px / 0.70) ** 8 + (py / 0.095) ** 2))

            lobe_density = 0.0
            for cx, cy, sx, sy, weight in lobes:
                lobe_density += weight * gaussian(px, py, cx, cy, sx, sy)
            lobe_density = min(lobe_density / 1.72, 1.0)

            wisp_density = 0.0
            for cx, cy, sx, sy, weight in wisps:
                wisp_density += weight * gaussian(px, py, cx, cy, sx, sy)
            wisp_density = min(wisp_density / 0.95, 1.0)

            void_density = 0.0
            for cx, cy, sx, sy, weight in holes:
                void_density += weight * gaussian(px, py, cx, cy, sx, sy)
            void_density = min(void_density / 0.90, 0.25)

            density = (
                0.34 * body
                + 0.31 * spine
                + 0.28 * lobe_density
                + 0.11 * wisp_density
                - 0.32 * void_density
            )

            # Feather both ends and retain a faint irregular outer envelope.
            end_fade = max(0.0, min(1.0, (0.98 - abs(px)) / 0.18))
            vertical_fade = max(0.0, min(1.0, (0.42 - abs(py)) / 0.17))
            envelope = math.exp(-0.5 * ((px / 0.83) ** 8 + (py / 0.29) ** 2))
            density = max(density, 0.055 * envelope)
            density *= end_fade * vertical_fade
            density *= (
                0.965
                + 0.022 * math.sin(px * 12.0 + py * 7.0)
                + 0.013 * math.sin(px * 29.0 - py * 19.0)
            )
            density = max(0.0, min(1.0, density))
            alpha = int(round(MAX_ALPHA * density))
            pixels.extend((255, 255, 255, alpha))
    return bytes(pixels)


def particle_system_text() -> str:
    return """A
1000
PARTICLE_SYSTEM

 TEXTURE contrail_v5_7_1_projected_cloud.png
PARTICLE
 NAME ffatmo_v571_projected_cloud
 MAX_PARTICLES 8192
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
 NAME ffatmo_v571_attached_cloud
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
 0.000000 4.000000 4.000000
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
# FFAtmo Renderer Foundation v5.7.1 projected billboard cloud
PARTICLE_SYSTEM contrail_v5_7_1_projected.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_v571_attached_cloud 0 0 0 0 0 0
"""


def validate(pss: str, obj: str, pixels: bytes) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 1 or lines.count("SUB_EMITTER") != 1:
        raise RuntimeError("v5.7.1 requires exactly one attached cloud particle")
    if lines.count("BILLBOARD_MODE BILLBOARD") != 1:
        raise RuntimeError("v5.7.1 standard billboard mode is missing")
    if any(mode in pss for mode in ("BILLBOARD_MODE AXIAL", "BILLBOARD_MODE RIBBON")):
        raise RuntimeError("Axial or Ribbon mode must not return")
    if "4.000000 4.000000" not in pss or "32.000000 32.000000" not in pss:
        raise RuntimeError("v5.7.1 projected segment size range is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.7.1 OBJ must remain particle-only")
    if len(pixels) != SIZE * SIZE * 4:
        raise RuntimeError("v5.7.1 RGBA texture size is invalid")
    if any(value != 255 for channel in (pixels[0::4], pixels[1::4], pixels[2::4]) for value in channel):
        raise RuntimeError("v5.7.1 cloud RGB must remain pure white")
    alpha = pixels[3::4]
    if max(alpha) < 180:
        raise RuntimeError("v5.7.1 cloud peak opacity is too low")

    # Simulate the observed 16x mip reduction. The elongated body must retain a
    # connected horizontal spine and a softer outer cloud envelope.
    block = 16
    reduced = []
    for by in range(0, SIZE, block):
        row_values = []
        for bx in range(0, SIZE, block):
            total = 0
            for yy in range(by, by + block):
                row = yy * SIZE
                for xx in range(bx, bx + block):
                    total += alpha[row + xx]
            row_values.append(total / float(block * block))
        reduced.append(row_values)
    centre = reduced[len(reduced) // 2]
    if sum(1 for value in centre if value >= 28.0) < 42:
        raise RuntimeError("v5.7.1 mip-scale longitudinal core is not connected")
    if max(max(row) for row in reduced) < 105.0:
        raise RuntimeError("v5.7.1 mip-scale peak alpha is too low")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    pixels = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj, pixels)

    write_rgba_png(args.output_dir / "contrail_v5_7_1_projected_cloud.png", pixels)
    (args.output_dir / "contrail_v5_7_1_projected.pss").write_text(pss, encoding="utf-8", newline="\n")
    (args.output_dir / "contrail_billboard.obj").write_text(obj, encoding="utf-8", newline="\n")
    (args.output_dir / "ASSET_INFO.txt").write_text(
        "Renderer Foundation v5.7.1 projected standard-billboard cloud\n",
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
