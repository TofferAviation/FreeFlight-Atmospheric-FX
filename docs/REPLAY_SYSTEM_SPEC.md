# Replay System Specification

## Purpose

The replay system makes atmosphere and aircraft inputs reproducible. It is required before production physics work because live-flight screenshots do not provide controlled comparisons.

The same recording must support:

- Offline deterministic physics replay.
- Automated scenario tests.
- Visual regression runs.
- Performance profiling.
- Bug reproduction.
- Diagnostics packages from users.

It is not intended to replace X-Plane's own replay feature.

## Architecture

```text
XPlaneHostAdapter
      |
      +---- SimulatorSnapshot ----> live simulation
      |
      +---- bounded capture queue ----> ReplayWriter (I/O thread)

Replay file ----> ReplayReader ----> ReplayClock ----> simulation core
```

The simulation core receives the same logical `SimulatorSnapshot` contract in live and replay modes.

## Recorded content

Every recording contains:

### Header

- File magic.
- Container version.
- Snapshot schema version.
- Endianness marker.
- Engine build and git revision.
- X-Plane build.
- Platform and architecture.
- Aircraft identity and profile ID/version.
- Configuration revision and physical-model settings.
- Scenario random seed.
- Start geodetic position and local-coordinate origin.
- Units and coordinate convention identifier.

### Snapshot stream

- Sequence number.
- Simulator and monotonic time.
- Delta time, pause, replay, and time acceleration.
- Aircraft transform, velocity, acceleration, and angular motion.
- Atmosphere inputs and validity/provenance.
- Engine and aerodynamic inputs.
- Ground interaction inputs when enabled.
- Aircraft or scenery lifecycle events.

### Command stream

- Configuration changes.
- Effect enable/disable changes.
- Profile changes.
- Debug settings relevant to output.
- Explicit reset events.

### Optional diagnostics stream

- Module timings.
- Parcel and filament counts.
- State hashes.
- Numerical warnings.
- Selected camera samples for visual tests.

## File format

Use a chunked, forward-compatible binary container.

```text
FileHeader
ChunkHeader + MetadataChunk
ChunkHeader + SnapshotBlock
ChunkHeader + CommandBlock
ChunkHeader + DiagnosticsBlock
...
ChunkHeader + IndexChunk
ChunkHeader + EndChunk
```

Each chunk includes:

- Type.
- Schema version.
- Uncompressed size.
- Stored size.
- Timestamp or sequence range.
- Checksum.
- Compression type.

Unknown optional chunk types are skipped. Unknown required schema versions are rejected with a clear error.

## Capture policy

- Snapshot capture occurs on the X-Plane host thread through a bounded non-blocking queue.
- Serialization and compression occur on the I/O worker.
- If the queue is full, recording drops samples according to policy and records a gap marker; it never blocks X-Plane.
- Lifecycle and reset events are higher priority than ordinary samples.
- Default capture rate follows the host snapshot rate, with configurable downsampling for long diagnostics.

## Replay clock

Supported modes:

1. **Deterministic fixed-step** — canonical automated-test mode.
2. **Recorded timing** — reproduces original timing and discontinuities.
3. **Fast as possible** — headless physics and performance tests.
4. **Single step** — debugging.
5. **Seek and settle** — loads the nearest checkpoint and simulates forward.

## Checkpoints

Long recordings contain periodic checkpoints with enough engine state to avoid replaying from the beginning.

Checkpoint policy must be versioned. A checkpoint may include:

- Environment-history cache.
- Trail parcel state.
- Vortex filament state.
- Spatial index reconstruction data.
- Module configuration and random-generator state.

When a checkpoint schema is incompatible, the reader falls back to an earlier compatible checkpoint or start-of-stream replay.

## Determinism requirements

For the same:

- Replay file.
- Engine revision.
- Configuration.
- Quality-independent physical settings.
- Random seed.
- Fixed-step mode.

The physical state hash must match within the declared deterministic scope.

GPU rendering is not required to be bit-identical across vendors. Physics reference tests run on CPU and use tolerances for floating-point values where bitwise equality is not practical.

## State hashing

At selected ticks, compute stable hashes of quantized authoritative state:

- Parcel positions, masses, radii, phase, and age.
- Filament positions, circulation, core radius, and age.
- Environment summary.
- Module counts and reset generation.

Hashes help locate the first divergent tick.

## Scenario library

The repository should include small reusable recordings for:

- No-contrail warm atmosphere.
- Cold but non-persistent contrail.
- Cold ice-supersaturated persistent contrail.
- Strong crosswind.
- Vertical wind shear.
- Low and high turbulence.
- Steady cruise.
- Climb and descent.
- Coordinated turn.
- Abrupt manoeuvre.
- Pause/resume.
- Time acceleration.
- Teleport/reposition.
- Aircraft reload.
- Engine shutdown and restart.

Large proprietary recordings should be stored as CI artifacts or release test data rather than bloating the repository.

## Visual regression

A visual scenario defines:

- Replay file and tick range.
- Fixed camera path.
- Renderer and quality tier.
- Resolution and exposure settings.
- Expected image set.
- Difference thresholds and masked regions.

Visual regression complements physics tests; it does not replace them.

## Privacy and diagnostics

User-generated diagnostics must clearly list recorded data. No account credentials, GitHub tokens, unrelated file paths, or network content may be included.

A diagnostics export should support trimming location precision when the user chooses privacy mode.

## Milestone exit criteria

Replay foundation is complete when:

1. Live and replay paths feed the same simulation contracts.
2. A 30-minute recording does not block or destabilize X-Plane.
3. Corrupt or truncated files fail safely.
4. Fixed-step replay produces repeatable state hashes.
5. The test runner can replay without launching X-Plane.
6. At least six canonical scenarios are included in automated tests.
7. Profiling output identifies cost by module and tick.