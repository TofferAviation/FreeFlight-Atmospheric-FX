# FFAtmo Contrail Renderer v4 — Implementation Plan

Status: **Approved next build target**

## 1. Objective

Renderer v4 must eliminate the remaining visible-card, oval-disc, and bead-chain appearance while preserving the accepted ACF geometry, deterministic contrail physics, dry-air decay, airborne preview gate, world-space continuity, and Vulkan-safe X-Plane integration.

The accepted v3 runtime baseline processed 9,067 samples, held 281 active physics parcels, expanded those into 770 visible instances, stayed below the 1,024-instance renderer capacity, reported zero capacity drops, and preserved a roughly 10.99 km visible trail. The renderer therefore has sufficient input data and headroom; v4 is a render-planning, camera-facing, asset, blending, and density-quality pass rather than a physics rewrite.

## 2. Scope and non-goals

### In scope

- New neutral-white soft-alpha contrail textures.
- Deterministic texture variation.
- Explicit per-frame camera-facing orientation.
- Adaptive render-only interpolation between physics parcels.
- Near-field core and age-dependent halo representation.
- Capacity-aware instance prioritization.
- Renderer timing and allocation diagnostics.
- Automated tests for interpolation, continuity, camera-facing math, deterministic selection, and capacity limits.

### Not in scope

- Changes to ACF parsing or exhaust origins.
- Changes to the accepted contrail formation threshold or dry-air sublimation model.
- Calibrated physical ice mass.
- Production persistent-contrail weather calibration.
- Customer-facing configuration UI.
- Replacement of XPLMInstance with a custom Vulkan renderer.

## 3. Asset changes

### 3.1 Asset set

Replace the four single-variant puff assets with eight deterministic assets:

- Four optical/age buckets.
- Two texture variants per bucket.
- One camera-facing quad per OBJ.
- Total: eight OBJ files and eight PNG files.

Naming:

```text
contrail_core_0_a.obj / .png
contrail_core_0_b.obj / .png
contrail_core_1_a.obj / .png
contrail_core_1_b.obj / .png
contrail_core_2_a.obj / .png
contrail_core_2_b.obj / .png
contrail_core_3_a.obj / .png
contrail_core_3_b.obj / .png
```

Variant selection must be deterministic from parcel ID and engine index. No runtime random-number generator is permitted.

### 3.2 Texture specification

- Resolution: 256 × 256 RGBA.
- RGB: neutral white only, target range 248–255 in every channel.
- No green, brown, blue, or baked directional shadowing.
- Alpha carries all visible structure.
- Alpha contour must be irregular and broken, not a perfect circle or ellipse.
- Alpha begins fading close to the centre and reaches zero before the texture boundary.
- Pixels at the outer 8% border must have alpha zero.
- Internal structure uses deterministic multi-scale noise with broad low-frequency density and weaker high-frequency breakup.
- Texture variants differ in internal density distribution and edge breakup, not colour.

Target maximum alpha by bucket:

| Bucket | Intended use | Maximum source alpha |
|---|---|---:|
| 0 | very old / weak | 0.035 |
| 1 | old diffuse | 0.060 |
| 2 | developing | 0.095 |
| 3 | young dense core | 0.140 |

The visual target is accumulation from many faint overlapping sprites, not opacity from individual sprites.

### 3.3 OBJ requirements

- `ATTR_LOD` remains the first command record after geometry declarations.
- Every animation group is balanced and contained inside the LOD.
- Blending enabled.
- Shadows disabled.
- Back-face culling disabled.
- No intersecting cross-plane geometry.
- Quad pivot centred at the sprite centre.
- Asset validation script must reject non-zero border alpha, colour-channel divergence above 3 levels, unbalanced animations, missing textures, or an invalid first command.

## 4. Renderer parameter changes

### 4.1 Explicit camera-facing orientation

The current visible ellipses indicate that the world quad is not remaining perpendicular to the camera in all views. v4 must explicitly orient every visible instance toward the current camera every frame.

Required inputs:

- Camera local X/Y/Z.
- Camera heading and pitch, or a direction vector derived from camera and parcel position.

For each instance:

1. Calculate the normalized vector from parcel to camera.
2. Derive billboard heading and pitch from that vector.
3. Apply a small deterministic roll around the view axis.
4. Keep the sprite perpendicular to the camera within 1.0 degree.

No sprite may become edge-on when the camera moves around the trail.

### 4.2 Size model

Replace the existing broad radius clamp with an age-dependent render radius:

| Parcel age | Core radius target | Halo radius target |
|---|---:|---:|
| 0–2 s | 0.30–0.75 m | none |
| 2–8 s | 0.65–1.60 m | optional 1.2–2.4 m |
| 8–20 s | 1.3–3.2 m | 2.5–5.5 m |
| 20–60 s | 2.5–6.5 m | 5.0–12.0 m |

Global hard limits:

```text
minimum_render_radius_m = 0.25
maximum_core_radius_m = 7.0
maximum_halo_radius_m = 12.0
```

Young condensation must begin tighter than the engine nacelle diameter and expand progressively. No single dry-preview sprite may reach the previous 28 m visual scale.

### 4.3 Opacity model

Final sprite opacity is determined by:

```text
optical depth × age fade × layer factor × texture bucket alpha
```

Layer factors:

```text
core = 1.00
halo = 0.22 to 0.38 depending on age
```

Rules:

- Near-engine core opacity ramps in over the first 0.35 seconds to avoid a hard spawn pop.
- Dry-trail opacity must monotonically decrease after peak formation.
- Halo opacity must never exceed core opacity.
- Individual sprites must remain faint enough that their silhouette is not visible in isolation at normal external-camera distance.

### 4.4 Instance capacity and prioritization

Keep total capacity at 1,024 instances for v4.

Allocate dynamically rather than reserving equal fixed pools per bucket:

- Reserve at least 35% for young cores.
- Reserve at least 20% for the second engine to prevent one engine monopolising capacity.
- Use remaining capacity for developing cores, halos, and old diffuse samples.
- When over capacity, discard in this order:
  1. weakest old halos;
  2. weakest old cores;
  3. distant developing halos;
  4. never discard the newest 8 seconds of either engine unless the entire capacity is exhausted.

### 4.5 Transparent-instance ordering

Before assigning instances, sort render samples back-to-front by camera distance within each texture bucket. This reduces transparency-order artifacts and dark intersections.

### 4.6 Culling

Cull samples when any of the following is true:

- optical depth below 0.004;
- calculated opacity below 0.002;
- non-finite position or size;
- behind the configured maximum render distance;
- outside a conservative camera-frustum margin;
- segment discontinuity caused by reset, aircraft reload, or invalid time gap.

## 5. Parcel-to-render interpolation changes

### 5.1 Separate physics from render planning

Create a platform-independent render planner:

```text
src/render/ContrailRenderPlanner.h
src/render/ContrailRenderPlanner.cpp
tests/ContrailRenderPlannerTests.cpp
```

The planner consumes deterministic physics parcels and returns render samples. X-Plane-specific instance code only displays those samples.

### 5.2 Per-engine stream construction

- Group parcels by engine index.
- Sort each engine stream by age or emission order.
- Never interpolate between different engines.
- Break a stream if sequence/time continuity is invalid.
- Break a stream if adjacent world positions differ by more than 250 m or the recorded time gap exceeds 1.0 s.

### 5.3 Curved interpolation

Use Catmull–Rom or cubic Hermite interpolation through neighbouring physics parcels.

Linear interpolation is permitted only at the first and last segment where neighbours are unavailable.

The curve must:

- pass through each physics parcel position;
- retain deterministic output;
- preserve local-origin-rebase continuity;
- avoid overshoot greater than 25% of the adjacent segment length;
- maintain independent left and right engine streams.

### 5.4 Adaptive spacing

Render-sample spacing is based on age, radius, and segment length.

Target spacing:

| Age | Target spacing |
|---|---:|
| 0–2 s | 0.45–0.80 m |
| 2–8 s | 0.75–1.30 m |
| 8–20 s | 1.2–2.2 m |
| 20–60 s | 2.0–4.0 m |

Additionally:

```text
target_spacing <= 0.75 × visible core diameter
```

This guarantees overlap and prevents a bead chain. Insert no more than 16 render samples inside one physics segment. Capacity prioritization runs after interpolation.

### 5.5 Core and halo layers

For every planned centreline sample:

- Emit one core render sample.
- Add a halo sample only when age is above 2 seconds and available capacity permits.
- Halo uses the same centreline position plus deterministic cross-trail offset.
- Cross-trail offset amplitude grows from 0 at 2 seconds to at most 0.35 × halo radius in old parcels.
- Offset uses a deterministic low-frequency sequence so neighbouring samples remain coherent rather than sparkling independently.

### 5.6 Controlled irregularity

Add deterministic spatial variation after curve interpolation:

- Young 0–2 s: maximum positional jitter 0.03 × radius.
- Developing 2–20 s: 0.08–0.18 × radius.
- Old 20–60 s: 0.18–0.35 × radius.

Jitter is applied in the plane perpendicular to the local trail tangent. It must vary smoothly along the stream and have approximately zero mean over long sections.

No frame-to-frame random movement is allowed.

### 5.7 Determinism

The render planner must output an independent deterministic hash based on:

- physics parcel IDs;
- interpolated positions;
- sizes;
- opacities;
- texture variants;
- core/halo layer;
- final priority selection.

Same input, settings, and camera pose must produce the same render plan.

## 6. Diagnostics and report additions

Add these fields to `contrail_visual_debug.txt`:

```text
renderer_version=4
physics_parcel_count=
planned_core_sample_count=
planned_halo_sample_count=
interpolated_sample_count=
visible_instance_count=
capacity_culled_count=
opacity_culled_count=
frustum_culled_count=
maximum_segment_gap_m=
maximum_billboard_error_deg=
average_render_planner_time_ms=
maximum_render_planner_time_ms=
render_plan_hash=
```

The top-left debug panel should show:

```text
V4 BILLBOARDS READY
physics / planned / visible
planner ms
capacity culled
```

## 7. Automated tests

Required tests:

1. Two engine streams never cross-interpolate.
2. Adaptive spacing never exceeds 75% of the selected visible diameter.
3. A straight input path produces a continuous straight render path.
4. A curved path produces a smooth path without excessive overshoot.
5. Reset and discontinuity gaps terminate interpolation.
6. Camera-facing math remains within 1 degree for front, rear, side, top, and oblique camera positions.
7. Texture-variant selection is deterministic.
8. Core/halo generation is deterministic.
9. Capacity selection preserves both engines and the newest eight seconds.
10. Planner output never exceeds 1,024 samples.
11. Same input and camera pose produce the same render-plan hash.
12. Non-finite inputs are rejected without contaminating valid samples.

CI must also validate all generated OBJ and PNG assets before packaging.

## 8. Acceptance criteria

### 8.1 Visual acceptance

Renderer v4 is accepted only when all are true:

- No visible rectangular, circular, or elliptical card boundary in rear, side, front, top, or oblique views.
- No olive, green, brown, black, or scenery-tinted sprite edge.
- No sprite becomes visibly edge-on while orbiting the camera 360 degrees.
- No identifiable bead chain after the first two aircraft lengths.
- Two distinct narrow engine cores are visible for the first 2–5 seconds of trail age.
- Near-engine condensation begins at or immediately behind both parsed exhaust origins.
- Core width grows continuously with age; it does not jump between buckets.
- Older trail appears broader, softer, and less opaque than young trail.
- No hard spawn popping or opacity pulsing.
- Aircraft and scenery correctly occlude the world-space trail.
- Dry-preview trail fully fades without leaving isolated persistent discs.

### 8.2 Technical acceptance

- `status=OK`.
- `world_renderer_ready=1`.
- All eight assets load without OBJ warnings or errors.
- `capacity_drop_count=0` in physics.
- Planned sample count never exceeds 1,024 after selection.
- Both engines retain visible young cores under capacity pressure.
- `maximum_billboard_error_deg <= 1.0`.
- `maximum_render_planner_time_ms <= 2.0` on the current B738 test machine.
- No new local-origin-rebase discontinuity.
- No simulation change to the accepted deterministic physics hash when only renderer code is changed.
- Reset Trail hides all instances within one frame.
- Visual toggle hides all instances within one frame and restores safely.

### 8.3 Performance acceptance

On the current Ryzen 7 5800X / RTX 3080 Vulkan test system:

- Average render-planner CPU time target: 0.75 ms or less.
- Maximum render-planner CPU time target: 2.0 ms or less.
- No continuously increasing instance count or memory allocation.
- No noticeable stutter when the trail reaches full v4 capacity.

These are development-machine targets, not final minimum-system requirements.

## 9. In-simulator test procedure

### Preparation

1. Close X-Plane completely.
2. Delete the existing `FFAtmoContrailDebug` folder.
3. Install the complete v4 package, including all eight OBJ/PNG pairs.
4. Keep only the live contrail debug plugin enabled for visual evaluation; the geometry overlay may remain installed but should be switched off.
5. Start X-Plane with the B738 and Vulkan.
6. Select `FORCED DRY PREVIEW`.
7. Confirm the upper-left panel reports `V4 BILLBOARDS READY` and `ACF EXHAUSTS: 2`.

### Test A — ground gate

1. Remain parked with engines running for 20 seconds.
2. Taxi above idle thrust.
3. Confirm no contrail is emitted below 120 m AGL or 65 m/s TAS.

Pass condition: zero visible instances and no parcel emission caused by forced preview while below the gate.

### Test B — near-field continuity

1. Take off and climb above the preview gate.
2. Fly straight with stable thrust for 15 seconds.
3. Use a close rear-oblique camera 20–80 m behind the aircraft.
4. Inspect both engine origins and the first 5 seconds of trail.

Required captures:

- rear-oblique screenshot at 3–5 seconds;
- direct rear screenshot at 10–15 seconds.

Pass condition: two narrow continuous cores, no cards, no bead gaps, no oversized first puffs.

### Test C — age development

1. Continue straight flight for 60 seconds.
2. Capture at approximately 20 seconds and 60 seconds.
3. Inspect from rear-oblique, side, and top-oblique views.

Pass condition: progressive widening and fading, no visible sprite silhouettes, no abrupt bucket transition.

### Test D — camera orbit

1. Pause X-Plane after a developed trail exists.
2. Orbit the camera around the trail through front, side, rear, above, and below views.
3. Resume after 10 seconds.

Pass condition: sprites remain camera-facing and never become thin ellipses or edge-on cards. Physics does not advance while paused.

### Test E — manoeuvre continuity

1. Fly one coordinated 20–30 degree bank turn.
2. Return to level flight.
3. Observe the trail through the turn from a distant rear-oblique camera.

Pass condition: smooth curved streams, no interpolation overshoot, no cross-connection between engines, no kinks or teleport segments.

### Test F — capacity and old-trail quality

1. Continue in dry preview until at least 800 visible instances are reported.
2. Observe the oldest visible trail.
3. Confirm young cores remain present on both engines.

Pass condition: old weak halos are culled first, no capacity overflow, no alternating engine starvation, no isolated giant puffs.

### Test G — reset and visual toggle

1. Select `Reset Trail`.
2. Confirm all instances disappear within one frame.
3. Allow the trail to rebuild.
4. Toggle world visuals off and on.

Pass condition: clean hide/show with no orphaned objects.

### Test H — persistent-preview stress check

1. Cycle to `FORCED PERSISTENT PREVIEW`.
2. Fly straight for 90 seconds.
3. Observe density, continuity, capacity selection, and performance.

Pass condition: no stutter, no hard capacity popping, both engine cores retained, and broad trail remains cloud-like rather than card-like.

### Required return files

```text
X-Plane 12/Log.txt
Resources/plugins/FFAtmoContrailDebug/reports/contrail_visual_debug.txt
```

Required images:

- close rear-oblique at 3–5 seconds;
- direct rear at 10–15 seconds;
- rear-oblique at 30–60 seconds;
- side or top-oblique at 30–60 seconds;
- one camera-orbit view from the front or below.

## 10. Implementation order

1. Extract platform-independent render-planner code.
2. Add camera-facing math and tests.
3. Implement adaptive curved interpolation.
4. Add core/halo planning and deterministic variation.
5. Implement capacity prioritization and back-to-front ordering.
6. Regenerate and validate eight neutral-white assets.
7. Add report and timing diagnostics.
8. Build Windows artifact and run all tests.
9. Perform the complete B738 Vulkan acceptance procedure.

Renderer v4 is not considered complete when it merely looks better from one angle. It is complete only when the trail remains continuous, soft, neutral, camera-facing, depth-aware, deterministic, and performant from every required view.