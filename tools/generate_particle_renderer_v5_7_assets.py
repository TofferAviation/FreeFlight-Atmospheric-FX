#!/usr/bin/env python3
"""Generate Renderer Foundation v5.7 axial-core plus billboard-halo assets."""
from __future__ import annotations

import argparse
from pathlib import Path

from generate_particle_renderer_v5_5_2_assets import (
    SIZE,
    generate_texture,
    write_rgba_png,
)


def particle_block(name: str, billboard_mode: str, alpha_scale: float, length_scale: float) -> str:
    return f"""PARTICLE
 NAME {name}
 MAX_PARTICLES 8192
 BILLBOARD_MODE {billboard_mode}
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
 0.000000 {alpha_scale:.6f}
 1.000000 {alpha_scale:.6f}
END_KEYFRAME_TABLE
LENGTH_CURVE
INTERP_MODE LINEAR
 0.000000 {length_scale:.6f}
 1.000000 {length_scale:.6f}
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
"""


def sub_emitter(particle_type: int, initial_speed: float) -> str:
    return f"""SUB_EMITTER
 PARTICLE_TYPE {particle_type}
EMIT_RATE
SLOT 1
INTERP_MODE LINEAR
 0.000000 0.000000 0.000000
 1.000000 1.000000 1.000000
END_KEYFRAME_TABLE
INITIAL_SPEED
INTERP_MODE LINEAR
 0.000000 {initial_speed:.6f} {initial_speed:.6f}
 1.000000 {initial_speed:.6f} {initial_speed:.6f}
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
 0.000000 0.650000 0.650000
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
"""


def particle_system_text() -> str:
    return (
        "A\n1000\nPARTICLE_SYSTEM\n\n"
        " TEXTURE contrail_v5_7_cloud.png\n"
        + particle_block("ffatmo_v57_axial_core", "AXIAL", 0.720000, 5.200000)
        + particle_block("ffatmo_v57_soft_halo", "BILLBOARD", 0.240000, 0.000000)
        + "EMITTER\n"
          " NAME ffatmo_v57_attached_layers\n"
          " EMIT_MODE ATTACH\n"
        + sub_emitter(0, 0.010000)
        + sub_emitter(1, 0.000000)
        + "DATAREFS 5\n"
          "DREF \n"
          "DREF ffatmo/contrail_particle/rate\n"
          "DREF ffatmo/contrail_particle/size\n"
          "DREF ffatmo/contrail_particle/alpha\n"
          "DREF ffatmo/contrail_particle/lifetime\n"
          "END_EMITTER\n"
          " TEX_CELLS_X 1\n"
          " TEX_CELLS_Y 1\n"
          "DATAREFS 0\n"
          "END_PARTICLE_SYSTEM\n"
    )


def object_text() -> str:
    return """I
800
OBJ
# FFAtmo Renderer Foundation v5.7 filled axial-core cloud field
PARTICLE_SYSTEM contrail_v5_7_layered.pss
POINT_COUNTS 0 0 0 0
EMITTER ffatmo_v57_attached_layers 0 0 0 0 0 0
"""


def validate(pss: str, obj: str, pixels: bytes) -> None:
    lines = [line.strip() for line in pss.splitlines()]
    if lines.count("PARTICLE") != 2:
        raise RuntimeError("v5.7 requires exactly two particle layers")
    if lines.count("SUB_EMITTER") != 2:
        raise RuntimeError("v5.7 requires exactly two attached sub-emitters")
    if lines.count("BILLBOARD_MODE AXIAL") != 1:
        raise RuntimeError("v5.7 axial core is missing")
    if lines.count("BILLBOARD_MODE BILLBOARD") != 1:
        raise RuntimeError("v5.7 round halo is missing")
    if lines.count("BILLBOARD_MODE RIBBON") != 0:
        raise RuntimeError("Ribbon mode must not return")
    if lines.count("EMIT_MODE ATTACH") != 1:
        raise RuntimeError("v5.7 attach emitter is missing")
    if "5.200000" not in pss:
        raise RuntimeError("v5.7 axial length ratio is missing")
    if "0.650000 0.650000" not in pss or "12.000000 12.000000" not in pss:
        raise RuntimeError("v5.7 particle width range is missing")
    if "TRIS" in obj or "TEXTURE_LIT" in obj:
        raise RuntimeError("v5.7 OBJ must remain particle-only")
    if len(pixels) != SIZE * SIZE * 4:
        raise RuntimeError("v5.7 RGBA texture size is invalid")
    if any(
        value != 255
        for channel in (pixels[0::4], pixels[1::4], pixels[2::4])
        for value in channel
    ):
        raise RuntimeError("v5.7 cloud RGB must remain pure white")
    if max(pixels[3::4]) < 150:
        raise RuntimeError("v5.7 cloud alpha structure is too weak")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    pixels = generate_texture()
    pss = particle_system_text()
    obj = object_text()
    validate(pss, obj, pixels)

    write_rgba_png(args.output_dir / "contrail_v5_7_cloud.png", pixels)
    (args.output_dir / "contrail_v5_7_layered.pss").write_text(
        pss, encoding="utf-8", newline="\n"
    )
    (args.output_dir / "contrail_billboard.obj").write_text(
        obj, encoding="utf-8", newline="\n"
    )
    (args.output_dir / "ASSET_INFO.txt").write_text(
        "Renderer Foundation v5.7 axial core plus soft billboard halo\n",
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
