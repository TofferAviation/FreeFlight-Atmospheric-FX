# Boeing 737-800NG Canonical Replay Validation — 2026-07-22

Status: **Accepted as the first full-flight Atmospheric Engine v1 replay fixture, with follow-up coverage still required**

## Source fixture

- File: `20260722-151222-B738.ffar`
- SHA-256: `0d1e0d56dcac01130c2446e1763b76772db9f2ae220969afdc81793d0a6b7dfa`
- Size: 44,348,536 bytes
- Recorder build: `engine-v1-recorder-v1`
- Aircraft: Boeing 737-800NG (`B738`)
- Aircraft path: `Aircraft/737NG Series_V2.S1/737_80NG.acf`
- X-Plane: 12.4.3-r2 / XPLM 4.3
- Platform: Windows x64 / Vulkan

The binary header, metadata, all chunk checksums, snapshot payloads, and final end chunk were independently decoded and validated.

## Container and capture result

- Replay status: valid
- Snapshots: 17,057
- Sequence range: 1–17,057, fully contiguous
- Dropped snapshots: 0
- Clean end chunk: present
- Simulator-time span: 1,023.967 seconds
- Monotonic wall-time span: 1,046.581 seconds
- Effective average capture rate: 16.30 Hz
- Nominal callback request: 20 Hz
- Median sample interval: 59.9 ms

The lower effective rate is expected from a flight-loop callback that is ultimately serviced by simulator frames. One 4.59-second wall-time stall occurred while simulator time advanced only 0.07 seconds; sequence continuity and recorder integrity were preserved.

## Flight coverage

### Ground and take-off

- Approximately 114.7 seconds recorded before becoming airborne.
- Engine state moved from idle through take-off thrust.
- Slats deployed before take-off and retracted during the initial climb.

### Climb

- Continuous climb from departure to approximately 29,600 ft.
- Climb coverage: approximately 554.5 simulator seconds.
- Mean climb vertical speed: approximately 3,080 ft/min.
- Mean engine N1 during the climb: approximately 97.3%.
- Multiple banked turns were recorded, including three sustained segments above 20 degrees of bank.

### High-level segment

- Maximum altitude: approximately 29,639 ft.
- Stable high-level segment: approximately 154 simulator seconds.
- Mean true airspeed: approximately 410 kt.
- Mean ambient temperature: approximately 234.10 K (-39.05 C).
- Mean static pressure: approximately 31.39 kPa.
- Mean engine N1: approximately 77.5%.
- Mean thrust: approximately 18.3 kN per engine.

### Pause and X-Plane replay

- Pause state was captured.
- X-Plane replay state was captured for approximately 15.6 seconds.
- Simulator time correctly stopped while paused/replaying.

### Descent

- More than 200 simulator seconds of reduced-power descent were recorded.
- The recording ended while still airborne near 26,100 ft.
- Slats deployed again late in the descent segment.

## Atmospheric coverage

- Aircraft-position temperature, pressure, density, wind, precipitation, snow, hail, thermal rate, and gravity fields are present.
- Temperature, dew-point, wind, shear, and turbulence profiles contain 13 levels throughout the recording.
- The high-level segment used roughly 21.6 m/s profile wind, 0.62 m/s profile shear, and low profile turbulence near 0.02.

At the high-level segment the interpolated dew point was approximately 226.3 K while ambient temperature was approximately 234.1 K. Depending on whether the simulator's sub-zero dew-point value is interpreted as frost point or conventional water dew point, estimated relative humidity with respect to ice is approximately 40–63%. The recorded atmosphere is therefore not ice-supersaturated and should be treated as a **non-persistent contrail reference case**, not a persistent spreading-contrail calibration case.

## Critical coordinate-system finding

Two large X-Plane local-coordinate origin shifts occurred:

- approximately 111.3 km
- approximately 78.6 km

Latitude and longitude remained continuous, with no geodetic jump greater than approximately 32 metres between samples. These are simulator local-origin recentering events, not aircraft teleports.

Atmospheric Engine v1 must therefore never treat long-lived X-Plane local coordinates as an absolute persistent world frame. Persistent trails need a geodetic or engine-owned world anchor and must rebase correctly when X-Plane shifts its local origin.

## Gaps still requiring a second fixture

1. No sustained 2x simulator-time segment was detected.
2. No deliberate geodetic reposition or aircraft reload was detected.
3. `flapRatio` remained fixed at 0.285714 for the entire recording. Either the flap setting was not changed or this DataRef is not representative for this aircraft. `slatRatio` did transition correctly.
4. The replay contains a dry, non-persistent high-altitude atmosphere. A separate ice-supersaturated fixture is required for persistent-contrail calibration.
5. No manual lifecycle marker was present beyond recorder start/stop flags.

## Acceptance decision

This fixture is accepted for:

- simulator snapshot and replay regression tests;
- aircraft-state normalization;
- propulsion-source modeling;
- climb, turn, cruise, pause, replay, and descent lifecycle behavior;
- local-origin rebasing development;
- non-persistent contrail formation and decay work.

It is not sufficient by itself for:

- persistent spreading contrails;
- commanded time-acceleration behavior;
- deliberate teleport/reload handling;
- flap-transition validation.

The next implementation milestone may proceed using this replay as the primary deterministic flight fixture while a shorter supplemental coverage recording is collected later.
