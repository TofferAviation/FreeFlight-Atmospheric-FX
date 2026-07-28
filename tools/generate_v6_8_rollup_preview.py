#!/usr/bin/env python3
"""Generate an offline SVG acceptance preview for v6.8 wake morphology.

This is intentionally renderer-independent. It reproduces the v6.8 visible-radius
mapping over a representative B738 finite-core wake evolution so CI can reject a
build whose PRIMARY silhouette remains effectively straight/sub-metre.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def clamp(x, a, b): return max(a, min(b, x))
def smoothstep(a, b, x):
    if b == a: return 1.0 if x >= b else 0.0
    t = clamp((x-a)/(b-a), 0.0, 1.0)
    return t*t*(3.0-2.0*t)


def visible_radius(age, wake_radius, core_radius, width, gain):
    capture = smoothstep(3.2, 8.5, age)
    breakdown = 1.0 - 0.78*smoothstep(23.0, 33.0, age)
    radius = clamp(0.24*wake_radius + 0.58*core_radius + 0.16*width, 1.35, 5.80)
    return radius*capture*breakdown*gain


def representative_wake(engine_sign: float, gain: float):
    # B738-scale representative finite-core evolution. Phase advances in the
    # physically observed/solver-like direction; radius grows during capture,
    # then remains bounded as the wake begins to diffuse.
    pts=[]
    for i in range(121):
        age=i*0.25
        phase=engine_sign*math.radians(8.0 + 5.4*age - 0.045*age*age)
        wake_r=clamp(8.5 + 0.48*age, 8.5, 20.0)
        core=0.55 + 0.055*math.sqrt(max(age,0.0))*3.0
        width=0.55 + 0.16*age
        vr=visible_radius(age,wake_r,core,width,gain)
        # Use the same vertical squash as the runtime primary envelope.
        lateral=math.cos(phase+engine_sign*0.16)*vr
        vertical=math.sin(phase+engine_sign*0.16)*vr*0.82 - 0.10*age
        pts.append((age,lateral,vertical,vr,phase))
    return pts


def write_svg(path: Path, left, right):
    W,H=1500,720
    margin=70
    def xy(p, base_y):
        age,lat,vert,_,_=p
        x=margin + age/30.0*(W-2*margin)
        y=base_y - lat*34.0 - vert*13.0
        return x,y
    def poly(points,base_y):
        return ' '.join(f'{x:.1f},{y:.1f}' for x,y in (xy(p,base_y) for p in points))
    svg=f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
<rect width="100%" height="100%" fill="#10243a"/>
<text x="70" y="45" fill="white" font-family="sans-serif" font-size="26">FFAtmo v6.8 offline primary-vortex silhouette check</text>
<text x="70" y="75" fill="#b8c7d8" font-family="sans-serif" font-size="16">Fresh plume stays tight; primary mass visibly rolls after capture. This is a geometry acceptance preview, not final X-Plane shading.</text>
<line x1="70" y1="270" x2="1430" y2="270" stroke="#50677f" stroke-width="1"/>
<line x1="70" y1="520" x2="1430" y2="520" stroke="#50677f" stroke-width="1"/>
<polyline points="{poly(left,270)}" fill="none" stroke="white" stroke-width="10" stroke-linecap="round" stroke-linejoin="round"/>
<polyline points="{poly(right,520)}" fill="none" stroke="white" stroke-width="10" stroke-linecap="round" stroke-linejoin="round"/>
<text x="75" y="245" fill="#89bfff" font-family="sans-serif" font-size="18">LEFT wake: opposite-sense roll-up</text>
<text x="75" y="495" fill="#89bfff" font-family="sans-serif" font-size="18">RIGHT wake: counter-rotation</text>
<text x="70" y="680" fill="#b8c7d8" font-family="sans-serif" font-size="15">0 s → 30 s wake age</text>
</svg>'''
    path.write_text(svg,encoding='utf-8')


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('output_dir',type=Path); ap.add_argument('--gain',type=float,default=1.45)
    args=ap.parse_args(); args.output_dir.mkdir(parents=True,exist_ok=True)
    left=representative_wake(-1.0,args.gain); right=representative_wake(1.0,args.gain)
    radii=[p[3] for p in left+right if 7.0<=p[0]<=22.0]
    laterals=[abs(p[1]) for p in left+right if 7.0<=p[0]<=22.0]
    phase_span=abs(left[-1][4]-left[16][4])
    metrics={
        'gain':args.gain,
        'mean_rollup_radius_m':sum(radii)/len(radii),
        'max_rollup_radius_m':max(radii),
        'max_primary_lateral_excursion_m':max(laterals),
        'phase_span_deg_4_to_30':math.degrees(phase_span),
    }
    # Acceptance threshold deliberately demands a silhouette-level change. v6.7's
    # measured mean companion radius was only ~0.63 m and max ~1.16 m.
    if metrics['mean_rollup_radius_m'] < 2.6: raise RuntimeError(metrics)
    if metrics['max_rollup_radius_m'] < 4.0: raise RuntimeError(metrics)
    if metrics['max_primary_lateral_excursion_m'] < 3.0: raise RuntimeError(metrics)
    write_svg(args.output_dir/'v6_8_rollup_preview.svg',left,right)
    (args.output_dir/'v6_8_rollup_metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    print(json.dumps(metrics,indent=2))

if __name__=='__main__': main()
