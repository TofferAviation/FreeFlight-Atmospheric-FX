# Simulator Snapshot and Recorder Implementation Specification

Status: **Implementation baseline**

## Purpose

This milestone creates the first executable boundary between X-Plane and Atmospheric Engine v1. It records coherent immutable simulator snapshots without placing physics, rendering, or blocking file I/O on the X-Plane callback thread.

## Deliverables

- `SimulatorSnapshot` schema version 1.
- X-Plane DataRef resolver and sampler.
- Separate temperature, dew-point, and wind-layer coordinates.
- Fixed-capacity single-producer/single-consumer capture queue.
- Chunked replay container with checksums and explicit end marker.
- Background snapshot serialization and file writing.
- Safe replay reader and corruption handling.
- DataRef resolution, type, array-length, range, and non-finite report.
- Standalone `FFAtmoEngineRecorder` development plugin.
- Automated round-trip and truncation tests.

## Thread ownership

### X-Plane thread

- Resolves DataRefs during plugin enable.
- Reads DataRefs at the configured 20 Hz capture rate.
- Creates one complete immutable `SimulatorSnapshot`.
- Publishes the snapshot through a bounded non-blocking SPSC queue.
- Never waits for disk I/O or serialization.

### Recorder worker

- Removes snapshots from the queue.
- Serializes versioned snapshot blocks.
- Computes block checksums.
- Writes and flushes the replay container.

### Manual finalization

Stopping a recording joins the writer, emits the end chunk, validates the completed file, and writes human-readable reports. This work occurs only after an explicit user command or plugin shutdown, not on the regular capture path.

## Snapshot semantics

- SI units are used except explicitly named degree fields.
- X-Plane local coordinates remain identified as X-Plane local coordinates until the future host adapter publishes the engine world frame.
- Temperature, dew-point, and wind profiles preserve their own altitude arrays and counts.
- Missing groups remain absent through the validity mask.
- Engine arrays are bounded to 16 entries.
- Weather arrays are bounded to 32 levels.
- Aircraft identity stores relative paths only.
- The structure contains no XPLM handles, pointers, strings, graphics objects, or mutable shared state.

## Replay container v1

The file extension is `.ffar`.

```text
FileHeader
MetadataChunk
SnapshotBlock...
EndChunk
```

Properties:

- Magic: `FFATRPL1`.
- Little-endian numeric encoding with an endianness marker.
- Independently versioned container and snapshot schemas.
- CRC-32 on every chunk payload.
- No compression in the first implementation.
- 128 snapshots per normal block.
- Unknown chunks can be skipped by future readers.
- A missing or corrupt end chunk causes validation failure.

## Queue and overload policy

The queue contains 2,048 snapshots. At 20 Hz this represents more than 100 seconds of temporary writer backlog.

When full:

- the simulator thread drops the incoming ordinary snapshot;
- a dropped-snapshot counter increments;
- X-Plane is never blocked;
- final reports expose the exact drop count.

The controlled validation flight is accepted only with zero dropped snapshots.

## Privacy

The recorder includes aircraft state, atmospheric state, engine state, simulator timing, aircraft identity, and the aircraft-relative ACF path. It does not include login details, GitHub credentials, account tokens, unrelated files, network traffic, or companion authentication data.

## First milestone acceptance

1. Recorder and parser tests compile on the Windows CI runner.
2. A recorded file round-trips 500 synthetic snapshots exactly within floating-point representation.
3. Truncated files fail safely.
4. The plugin loads separately from production FFAtmo.
5. A live 737 recording finalizes with a valid end chunk.
6. DataRef report shows runtime resolution and array lengths.
7. The recording contains climb, cruise, turn, descent, pause, time acceleration, and an aircraft-position discontinuity or reload.
8. No snapshot drops occur during the controlled flight.

## Deferred from this milestone

- Physics simulation.
- GPU work.
- Contrail rendering changes.
- Replay seeking and checkpoints.
- Compression.
- Full offline scenario runner.
- State hashing beyond container checksums.
- Production integration into the normal plugin or companion.
