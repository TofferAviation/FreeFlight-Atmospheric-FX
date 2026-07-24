# Boeing 737-800NG Contrail Baseline v1 Validation

Status: **Accepted as engineering baseline**

Date: 2026-07-22

## Source

- Replay fixture: `20260722-151222-B738.ffar`
- Aircraft: Boeing 737-800NG (`B738`)
- Model: `engine-v1-contrail-baseline-v1`
- Contrail timeline: `20260722-151222-B738-contrail-timeline.csv`
- Contrail summary: `20260722-151222-B738-contrail-summary.txt`

## Result

- Status: `OK`
- Input samples: 17,057
- Emitted parcels: 834
- Expired parcels: 834
- Final parcels: 0
- Peak active parcels: 230
- Capacity drops: 0
- Persistent-environment samples: 0
- Frozen-physics samples: 333
- First emission time: 620.928574 s
- Last emission time: 813.452866 s
- Last visible parcel time: approximately 850.847 s
- Maximum parcel age: 47.682190 s
- Maximum total normalized visual ice mass: 78.459991
- Maximum combined visible trail length: 20,144.272635 m
- Maximum formation potential: 0.962954
- Deterministic hash: `0xaaccb2808a3e6020`

## Detailed checks

- Timeline and normalized replay both contain 17,057 rows with identical sequence numbers.
- Parcel accounting balances exactly: all 834 emitted parcels expire and none remain at the end.
- No parcels are emitted while physics is frozen.
- All frozen samples have a zero physics delta.
- The first sample plus paused/replay timing account for 333 frozen timeline rows.
- Formation potential becomes positive in one continuous cold-altitude window.
- Emission begins only after formation potential becomes strong enough to fill the deterministic emission accumulators.
- The two X-Plane local-origin rebases occur without a discontinuity in the engine-owned world trajectory.
- The reported 20.1 km trail length is the combined measured length of the two engine trails; it is approximately 10 km per engine and is consistent with about 47.7 seconds of high-speed flight.
- No persistent growth occurs because relative humidity with respect to ice never reaches 100%.
- The trail decays completely in the dry environment, as intended for this fixture.

## Acceptance decision

The first contrail formation and non-persistent decay model has passed the accepted B738 deterministic replay fixture. Engine-gated formation, deterministic emission, world-space continuity, pause/replay freezing, wind advection, young-wake descent, spreading, dry-air sublimation, parcel expiry, capacity protection, and deterministic hashing behave consistently on the recorded flight.

This model remains an engineering visual-mass baseline. It is not yet calibrated microphysics, aircraft-specific engine efficiency, or production rendering. The next milestone is a visual debug renderer that consumes these parcel positions in X-Plane, followed by calibration against dedicated dry and ice-supersaturated atmospheric fixtures.
