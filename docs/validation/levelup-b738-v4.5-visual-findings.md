# LevelUp B738 v4.5 visual findings

The first in-sim v4.5 run was numerically healthy but failed visual acceptance.

Observed:

- opaque black/grey segmented card chains when viewed along the trail
- transparent card borders did not blend into the sky
- the cooling gap, parsed exhaust geometry, wake geometry and stream continuity remained active
- no renderer pool drops or stream breaks were reported

Root cause:

The same RGBA cloud texture was assigned as both the daytime albedo texture and the X-Plane LIT/emissive texture. The LIT alpha participates in object opacity, defeating the intended soft transparency. The single no-cull face could also expose dark back-face lighting.

v4.6 correction:

- albedo-only RGBA cloud texture
- no TEXTURE_LIT, GLOBAL_luminance or ATTR_emission_rgb
- separately culled front/back faces with opposite normals
- unchanged condensation, ACF exhaust placement and Wake Fluid v1
