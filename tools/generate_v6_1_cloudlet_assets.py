#!/usr/bin/env python3
"""Generate the v6.1 native 3-D contrail cloudlet OBJ and diffuse-alpha texture."""
from __future__ import annotations

import argparse
import math
import random
import struct
import zlib
from pathlib import Path

TEX_SIZE = 256


def chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path: Path) -> None:
    rng = random.Random(6101)
    coarse = [(rng.uniform(-1,1), rng.uniform(-1,1), rng.uniform(0.18,0.45), rng.uniform(0.15,0.42), rng.uniform(0.10,0.30)) for _ in range(20)]
    pixels = bytearray()
    for y in range(TEX_SIZE):
        v = (y + 0.5) / TEX_SIZE
        py = v * 2.0 - 1.0
        for x in range(TEX_SIZE):
            u = (x + 0.5) / TEX_SIZE
            px = u * 2.0 - 1.0
            n = 0.0
            for cx, cy, sx, sy, w in coarse:
                dx = (px-cx)/sx
                dy = (py-cy)/sy
                n += w * math.exp(-0.5*(dx*dx+dy*dy))
            wave = 0.5 + 0.25*math.sin(px*11.0 + py*7.0) + 0.15*math.sin(px*23.0 - py*17.0)
            density = max(0.0, min(1.0, 0.30 + 0.34*n + 0.16*wave))
            alpha = int(round(34 + density * 78))
            pixels.extend((242, 248, 255, alpha))
    raw = bytearray()
    stride = TEX_SIZE * 4
    for y in range(TEX_SIZE):
        raw.append(0)
        raw.extend(pixels[y*stride:(y+1)*stride])
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", TEX_SIZE, TEX_SIZE, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def add_lobe(vertices, indices, centre, radii, seed, lat_steps=8, lon_steps=16):
    rng = random.Random(seed)
    ring_noise = [rng.uniform(0.88, 1.13) for _ in range(lat_steps + 1)]
    base = len(vertices)
    for iy in range(lat_steps + 1):
        theta = math.pi * iy / lat_steps
        st = math.sin(theta)
        ct = math.cos(theta)
        for ix in range(lon_steps):
            phi = 2.0 * math.pi * ix / lon_steps
            cp = math.cos(phi)
            sp = math.sin(phi)
            local_noise = ring_noise[iy] * (0.93 + 0.08 * math.sin(phi * 3.0 + seed * 0.13))
            nx = st * cp
            ny = ct
            nz = st * sp
            x = centre[0] + radii[0] * nx * local_noise
            y = centre[1] + radii[1] * ny * local_noise
            z = centre[2] + radii[2] * nz * local_noise
            # Ellipsoid normal approximation.
            ex = nx / max(radii[0], 1e-4)
            ey = ny / max(radii[1], 1e-4)
            ez = nz / max(radii[2], 1e-4)
            m = math.sqrt(ex*ex + ey*ey + ez*ez) or 1.0
            ex, ey, ez = ex/m, ey/m, ez/m
            u = ix / lon_steps
            v = iy / lat_steps
            vertices.append((x,y,z,ex,ey,ez,u,v))
    for iy in range(lat_steps):
        for ix in range(lon_steps):
            a = base + iy*lon_steps + ix
            b = base + iy*lon_steps + ((ix+1) % lon_steps)
            c = base + (iy+1)*lon_steps + ix
            d = base + (iy+1)*lon_steps + ((ix+1) % lon_steps)
            indices.extend((a,c,b, b,c,d))


def build_obj() -> str:
    vertices = []
    indices = []
    # Local -Z/+Z axis is the contrail longitudinal axis. Lobes overlap enough
    # to create a closed irregular 3-D cloud body with true parallax.
    lobes = [
        ((-0.5,  0.0, -4.2), (2.1, 1.7, 3.1), 61011),
        (( 0.8,  0.5, -1.9), (2.7, 2.1, 3.4), 61012),
        ((-0.9, -0.4,  0.6), (2.9, 2.3, 3.5), 61013),
        (( 0.7,  0.2,  3.0), (2.5, 2.0, 3.1), 61014),
        ((-0.3,  0.6,  5.0), (1.8, 1.5, 2.3), 61015),
    ]
    for centre, radii, seed in lobes:
        add_lobe(vertices, indices, centre, radii, seed)

    lines = [
        "I", "800", "OBJ",
        "# FFAtmo Renderer Foundation v6.1 native 3-D cloudlet",
        "TEXTURE contrail_cloudlet.png",
        "GLOBAL_no_shadow",
        f"POINT_COUNTS {len(vertices)} 0 0 {len(indices)}",
    ]
    for v in vertices:
        lines.append("VT " + " ".join(f"{value:.6f}" for value in v))
    # Pack indices ten per line where possible.
    i = 0
    while i + 10 <= len(indices):
        lines.append("IDX10 " + " ".join(str(n) for n in indices[i:i+10]))
        i += 10
    while i < len(indices):
        lines.append(f"IDX {indices[i]}")
        i += 1
    lines += [
        "ATTR_no_shadow",
        "ATTR_shade_smooth",
        "ATTR_no_cull",
        "ATTR_blend",
        "ATTR_shiny_rat 0.0",
        f"TRIS 0 {len(indices)}",
        "",
    ]
    return "\n".join(lines)


def validate(obj: str) -> None:
    if "PARTICLE_SYSTEM" in obj or "EMITTER" in obj:
        raise RuntimeError("v6.1 cloudlet must not contain particle-system primitives")
    if "ATTR_blend" not in obj or "GLOBAL_no_shadow" not in obj:
        raise RuntimeError("v6.1 cloudlet translucency/no-shadow state missing")
    if obj.count("VT ") < 500:
        raise RuntimeError("v6.1 cloudlet mesh is unexpectedly sparse")
    if "TRIS 0 " not in obj:
        raise RuntimeError("v6.1 cloudlet triangle command missing")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    obj = build_obj()
    validate(obj)
    (args.output_dir / "contrail_cloudlet.obj").write_text(obj, encoding="utf-8", newline="\n")
    write_png(args.output_dir / "contrail_cloudlet.png")
    (args.output_dir / "ASSET_INFO.txt").write_text(
        "FFAtmo v6.1 native 3-D cloudlet volume field. Closed irregular OBJ geometry; no particle/billboard system.\n",
        encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
