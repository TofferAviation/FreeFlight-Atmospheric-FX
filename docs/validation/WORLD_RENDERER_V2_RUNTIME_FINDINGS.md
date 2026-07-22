# World Contrail Renderer v2 Runtime Findings

Status: **Physics accepted; visual renderer rejected**

Date: 2026-07-22

The B738 Vulkan runtime test confirmed that the world-object path, aircraft geometry, airborne preview gate, instance pooling, parcel accounting, expiry, and world-space continuity functioned. The report showed four loaded objects, 280 visible instances, 1,532 emitted parcels, 1,252 expired parcels, zero capacity drops, and zero local-origin rebases.

The visual result was not accepted because the three intersecting textured planes produced dark cross-sections and obvious spherical beads. X-Plane also reported that every generated object placed `ANIM_begin` before the first `ATTR_LOD`, violating OBJ8 LOD ordering rules.

Required v3 corrections:

- put `ATTR_LOD` first in the OBJ command section;
- replace intersecting planes with camera-facing world-space billboards;
- soften and lower texture opacity;
- reduce apparent parcel diameter;
- increase renderer pool density;
- preserve normal world depth and the airborne formation gate.
