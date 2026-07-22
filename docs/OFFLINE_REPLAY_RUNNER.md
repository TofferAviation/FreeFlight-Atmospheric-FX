# Atmospheric Engine v1 Offline Replay Runner

Status: **Engineering baseline**

## Purpose

`FFAtmoReplayRunner` is a small command-line development tool. It does not run inside X-Plane and is not part of the customer-facing companion application.

It reads a checksummed `.ffar` recording and repeatedly feeds the same immutable simulator snapshots through the same normalization path. This gives physics development a deterministic input sequence without launching X-Plane or repeating a flight.

## Current responsibilities

- Validate the `.ffar` container and snapshot schema.
- Convert geodetic aircraft positions into an engine-owned East/Up/North world frame anchored at the first snapshot.
- Detect X-Plane local-coordinate origin shifts without treating them as aircraft teleports.
- Freeze physics time while X-Plane is paused or in replay mode.
- Clamp unusually large live callback steps to a bounded physics step.
- Interpolate the recorded dew-point profile at aircraft altitude.
- Estimate relative humidity with respect to ice for diagnostic use.
- Reject angle-of-attack and sideslip as physics inputs below 15 m/s true airspeed.
- Average recorded propulsion inputs across active engines.
- Produce a deterministic 64-bit hash of normalized output.
- Export a human-readable summary and optional CSV.

## Usage

```text
FFAtmoReplayRunner.exe <recording.ffar>
```

Optional arguments:

```text
--output <directory>
--no-csv
--help
```

By default the runner creates `FFAtmoReplayOutput` beside the recording.

## Outputs

```text
<recording>-runner-summary.txt
<recording>-normalized.csv
```

The summary reports sample counts, integrated physics time, pause/replay coverage, local-origin rebases, low-speed aerodynamic rejection count, environmental ranges, and the deterministic hash.

## Determinism contract

The same runner build, replay file, configuration, and platform must produce the same normalized sample sequence and deterministic hash. Any intentional change to the normalization contract must update tests and be documented.

The current hash is an engineering regression signal, not a cryptographic signature and not a cross-compiler serialization format.

## Deferred

- Contrail microphysics.
- Wake-vortex integration.
- Rendering.
- Replay seeking and checkpoints.
- A graphical timeline.
- Parallel scenario batches.
- Canonical fixture distribution inside the public repository.
