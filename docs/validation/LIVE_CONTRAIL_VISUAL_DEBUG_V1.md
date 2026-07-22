# Live Contrail Visual Debug v1 Runtime Validation

Status: **Physics accepted; visual renderer rejected for replacement**

Date: 2026-07-22

## Runtime result

- Aircraft: Boeing 737-800NG (`B738`)
- X-Plane: 12.4.3-r2, Vulkan
- Parsed ACF exhaust sources: 2
- Input samples: 14,300
- Emitted parcels: 1,510
- Expired parcels: 1,230
- Active parcels at report export: 280
- Peak active parcels: 282
- Capacity drops: 0
- Integrated physics time: 306.832836 seconds
- Forced dry preview formation potential: 1.0
- Forced dry preview RHi: 72%
- Maximum visible combined trail length: 14,816.590548 m
- Maximum parcel age: 56.512390 seconds
- Deterministic hash: `0xd0cb40e35d034d5d`

## Accepted

- Current B738 ACF geometry is parsed and two exhaust origins are used.
- Live snapshot normalization and engine-world parcel positions operate continuously.
- Deterministic emission, ageing, decay, expiry, and capacity protection are stable.
- No local-origin rebase occurred during this specific visual test.
- Pause-safe flight-loop simulation and report export loaded normally under Vulkan.

## Rejected visual behavior

The screen-space coach-mark renderer is not a production-quality visual baseline.

- Consecutive parcel connector lines create thin white curves and hooks.
- Camera-facing screen discs overlap into large smoke balls.
- Particles are not depth-occluded by the aircraft or scenery.
- Forced preview emits while parked because it overrides the formation altitude without an airborne gate.
- No aerodynamic vortex capture or roll-up is visible.

## Decision

Retain the validated live physics and coordinate pipeline. Replace the 2-D coach-mark parcel renderer with X-Plane-managed world-space object instances, remove connector lines, add an airborne/airspeed preview gate, and introduce a first deterministic wake-roll-up displacement. The replacement remains an engineering renderer; final atmospheric production rendering will require later shader, texture, LOD, and performance calibration.
