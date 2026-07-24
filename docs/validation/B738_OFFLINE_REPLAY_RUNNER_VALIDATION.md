# Boeing 737-800NG Offline Replay Runner Validation

Status: **Accepted**

Date: 2026-07-22

## Source

- Replay fixture: `20260722-151222-B738.ffar`
- Aircraft: Boeing 737-800NG (`B738`)
- Normalized CSV: `20260722-151222-B738-normalized.csv`
- Runner summary: `20260722-151222-B738-runner-summary.txt`

## Result

- Runner status: `OK`
- Input snapshots: 17,057
- Normalized samples: 17,057
- Sequence continuity: complete
- Non-finite normalized inputs: 0
- Negative physics steps: 0
- Physics steps above the 0.25-second clamp: 0
- Integrated physics time: approximately 1,023.967 seconds
- Pause samples: 332
- Replay samples: 274
- Pause samples with non-zero physics advance: 0
- Replay samples with non-zero physics advance: 0
- Local-origin rebases detected: 2
- Low-speed aerodynamic rejection samples: 1,321
- Deterministic hash: `0x889e98d92c61c0c8`

## Environment range

- Minimum altitude MSL: approximately 37.0 m
- Maximum altitude MSL: approximately 9,033.8 m
- Minimum temperature: approximately 233.86 K
- Maximum temperature: approximately 301.55 K
- Minimum diagnostic relative humidity with respect to ice: approximately 14.24%
- Maximum diagnostic relative humidity with respect to ice: approximately 75.59%

## Coordinate continuity

The two local-origin rebase flags occur while the engine-owned geodetic East/Up/North trajectory remains continuous. The normalized world displacement across those samples remains consistent with aircraft motion rather than the raw X-Plane local-coordinate jumps.

## Acceptance decision

The first offline replay runner has passed live-fixture validation. The deterministic clock freezes correctly during pause and X-Plane replay, the geodetic world frame survives local-origin shifts, low-speed aerodynamic validity filtering activates, and all normalized values remain finite.

This output is accepted as the regression baseline for the first contrail formation and non-persistent decay simulation.
