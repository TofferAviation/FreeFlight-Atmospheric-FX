# Renderer Foundation v6.9 — Lagrangian Vortex Sheet

## Why v6.8 is rejected

The v6.8 in-sim capture remains visually equivalent to the v6.6/v6.7 family: two segmented opaque ropes with local cloud blobs. The live gain command executed and the report measured large candidate-space offsets, but the visible morphology did not become a rolling wake.

The failure is architectural. One contrail parcel per engine emission is a centreline sample. Moving one centreline sample around a vortex cannot represent deformation of an exhaust/ice sheet. Compact closed OBJ cloudlets then hide most centreline displacement and preserve the rope silhouette.

## v6.9 model

### 1. Wake section, not centreline blob

Each selected physical parcel becomes a wake cross-section with multiple persistent Lagrangian material markers. Markers are seeded across the unresolved ice plume cross-section and assigned stable IDs. They do not use random per-frame offsets.

Minimum marker topology per engine wake section:
- inner sheet marker;
- core sheet marker;
- outer sheet marker;
- optional secondary-wake marker after roll-up onset.

### 2. Physical roll-up

Each material marker is advanced in the local wake plane by the same finite-core counter-rotating vortex pair used by WakeFluidSolver. The marker velocity is the sum of:
- finite-core Biot-Savart induction from both wing vortices;
- vortex-pair descent;
- entrainment/capture;
- turbulence and shear where available.

The visible phase is therefore the accumulated Lagrangian marker trajectory, not an arbitrary render-time helix and not a single centreline displacement.

### 3. Curved longitudinal tangents

Renderable marker tangents are computed from neighbouring displaced marker positions in the same lane. Do not reuse the undisplaced base `trailTangentLocal`; the geometry itself must point along the curled sheet.

### 4. Continuous soft morphology on supported XPLMInstance path

Stay on the supported native 3-D instance path. Replace compact high-occupancy blobs with overlapping elongated ice volumes. Because normal translucent blending interacts badly with X-Plane haze on the current Vulkan path, use stochastic/micro-dither alpha-test coverage:
- pure white RGB;
- binary alpha at material level;
- lower spatial occupancy in outer/soft volumes;
- denser inner volumes;
- large overlap so individual OBJ boundaries disappear at normal viewing distance.

The target is a continuous soft cloud sheet, not beads, torpedoes, cotton balls or isolated curls.

### 5. Budget

The 1024 instance pool is allocated by wake section/lane, not primary/fill/decorative-swirl layers. Longitudinal LOD reduces section density with age so marker lanes remain coherent instead of dropping arbitrary assets.

### 6. Acceptance metrics must measure what is actually rendered

Remove formula-only acceptance such as `maximumPrimaryRollupOffsetM` as the primary gate. New gates are computed from the final selected/rendered marker positions:
- rendered vortex phase span per engine;
- signed angular progression with age;
- rendered lateral spread;
- rendered vertical spread;
- wake-sheet width growth;
- lane continuity gap;
- lane curvature;
- counter-rotation sign between left/right wakes;
- secondary-wake separation;
- actual selected/rendered counts per lane.

A build fails if the final rendered field can still collapse into two approximately one-dimensional ropes.

### 7. Offline visual gate

Before packaging an X-Plane build, run the actual WakeFluidSolver (not a synthetic phase curve) with representative B738 cruise state and export rear-quarter/top/side previews of the final selected marker positions and morphology envelopes. Compare against the supplied reference effect qualitatively:
- coherent twin vortex roll-up;
- broadening wake cross-section;
- smooth continuous density;
- visible counter-rotation;
- breakup only after the organised roll-up phase.

No simulator package is handed off until this offline preview is unmistakably different from v6.8.
