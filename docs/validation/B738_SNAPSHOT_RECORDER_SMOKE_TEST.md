# Boeing 737-800NG Snapshot Recorder Smoke Test

Status: **Accepted with two follow-up corrections**

Date: 2026-07-21

## Environment

- X-Plane 12.4.3-r2, build 124311
- XPLM 4.3
- Windows x64
- Vulkan renderer
- Aircraft: Boeing 737-800NG (`B738`)
- Recorder build: `engine-v1-recorder-v1`

## Replay-container result

- Replay status: `OK`
- Snapshots: 2,036
- Dropped snapshots: 0
- Sequence range: 1–2,036
- Captured simulator-time span: 121.855294 seconds
- Clean end chunk: present
- Manual event marker: present
- Pause state: captured
- Replay-state transition: not exercised in this smoke test

The binary container header, metadata chunk, every snapshot chunk, CRC-32 checksums, and clean end chunk were independently inspected. The first and final snapshots decode successfully.

## DataRef validation

- DataRefs catalogued: 69
- Unresolved DataRefs: 0
- Aircraft identity resolved as Boeing 737-800NG / B738
- Aircraft-relative path resolved
- Engine count resolved as 2
- Engine-running, N1, N2, fuel flow, thrust, throttle, EGT, ITT, jetwash, and exhaust-velocity inputs changed during the test
- Atmospheric temperature, pressure, density, wind, and 13-level regional profiles were captured
- Pause changed from false to true and back to false
- No non-finite samples were observed except `sim/time/sim_speed_actual` during the paused samples

The recorder already substitutes a finite fallback for invalid time-acceleration samples, so the replay snapshots remain finite. The Environment/Clock normalization layer must nevertheless treat paused time explicitly rather than interpreting X-Plane's non-finite achieved-speed value as physical input.

## Follow-up findings

### 1. Metadata identity refresh

The metadata chunk and output filename used empty aircraft identity (`AIRCRAFT`) because recording metadata was generated before the first lifecycle snapshot refreshed the cached aircraft identity. The snapshots themselves contain the correct Boeing 737-800NG name, B738 ICAO, and aircraft-relative path, so no simulation data was lost.

Before the canonical flight, recorder startup should capture or refresh aircraft identity before creating the filename and metadata chunk.

### 2. Low-speed aerodynamic validity

At nearly zero airspeed, X-Plane reported angle-of-attack and sideslip values that wrapped through extreme angles. Raw values are preserved correctly. The future `AircraftState` normalizer must mark angle of attack and sideslip unavailable below a justified minimum airspeed rather than feeding those ground values into effect physics.

### 3. Configuration transition not exercised

Flap and slat values remained constant in this smoke test. The canonical flight must include take-off flap deployment/retraction and an approach configuration change.

## Acceptance decision

The snapshot queue, non-blocking writer, binary serialization, checksums, reader, finalization, lifecycle marker, pause capture, propulsion telemetry, weather telemetry, and zero-drop requirement have passed the first live simulator test.

The next acceptance run is the 15–20 minute canonical B738 scenario containing ground idle, take-off, climb, cold level flight, a coordinated turn, pause, 2x time, X-Plane replay, descent, flap/slat changes, and one reposition or aircraft reload.
