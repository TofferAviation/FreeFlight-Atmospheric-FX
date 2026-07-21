# Threading Model

## Objectives

- Protect X-Plane from blocking or unsafe plugin work.
- Keep XPLM access on the simulator thread.
- Allow physical simulation and render preparation to run independently.
- Avoid frame-to-frame allocations and coarse locks.
- Remain correct during pause, replay, time acceleration, aircraft reload, and shutdown.

## Thread ownership

### X-Plane host thread

Owns:

- All XPLM calls and handles.
- DataRef resolution and reads.
- Plugin messages and lifecycle callbacks.
- Creation of coherent `SimulatorSnapshot` values.
- Consumption of the latest completed render submission.
- SDK-safe resource creation, destruction, and instance updates.

May not perform:

- File or network I/O.
- Replay compression.
- Long physics updates.
- Blocking waits for worker completion.
- Companion IPC transactions requiring a response.

### Simulation worker

Owns:

- Mutable physical simulation state.
- Effect modules.
- Trail parcels and topology.
- Vortex filaments.
- Environment history and interpolation caches.
- Spatial index updates.
- Budget decisions for simulation detail.
- Construction of immutable `SimulationFrame` data.
- Renderer-neutral `RenderPacket` preparation when CPU-side.

It may not call XPLM or mutate host-thread state.

### Render worker or graphics submission path

Renderer-dependent. It owns:

- Graphics resource staging.
- GPU uploads.
- Compute dispatch preparation.
- Render packet conversion into backend-specific buffers.
- GPU timing collection.

Any actual API calls requiring X-Plane's graphics context remain on the callback/thread mandated by the selected backend.

### I/O worker

Owns:

- Replay file writing and reading.
- Profile and configuration file I/O.
- Diagnostics bundles.
- Companion protocol transport.
- Update metadata and non-frame-critical logging.

## Data exchange

```text
X-Plane host thread
    |
    | publishes newest immutable SimulatorSnapshot
    v
Single-producer/single-consumer snapshot channel
    |
    v
Simulation worker
    |
    | publishes immutable SimulationFrame / RenderPacket
    v
Latest-completed-frame channel
    |
    v
X-Plane host / rendering callback
```

The host always uses the newest complete output. If no new output is available, it reuses the previous valid frame.

## Buffering policy

Use triple-buffered frame storage:

1. **Published** — immutable and visible to rendering.
2. **Building** — exclusively owned by the simulation worker.
3. **Retired/free** — waiting to be reused after readers release it.

Large arrays use capacity-preserving storage or immutable shared blocks to avoid copying entire trails every frame.

## Synchronization policy

Preferred mechanisms:

- Atomic sequence numbers.
- Single-producer/single-consumer ring buffers.
- Atomic shared-pointer or index swaps for completed frames.
- Fine-grained queues for commands and reset events.
- Preallocated pools for high-frequency objects.

Forbidden in frame-critical paths:

- Waiting on condition variables from the X-Plane thread.
- Taking locks also used by file, network, or UI work.
- Recursive mutexes.
- Holding a lock while calling XPLM or a graphics driver.

## Snapshot timing

The host adapter records:

- Simulator time.
- Monotonic process time.
- Reported simulator delta time.
- Pause and replay flags.
- Time acceleration.
- Snapshot sequence number.

The simulation scheduler decides whether to integrate, pause, substep, clamp, or reset. Raw frame delta is never blindly fed into physical solvers.

## Fixed-step simulation

Recommended baseline:

- Core physics fixed step: 30 Hz.
- Maximum catch-up substeps per host update: 4.
- Maximum accepted single step: 1/15 second.
- Remaining excess time after a discontinuity triggers a reset or controlled fast-forward policy rather than an unstable large step.

Effect modules may declare lower-frequency updates when interpolation is safe.

## Command flow

UI and companion changes become versioned commands:

```text
ConfigurationCommand
ProfileReloadCommand
EffectEnableCommand
ReplayControlCommand
DebugCommand
ResetCommand
```

Commands are validated outside the simulation worker, queued, then applied at a simulation tick boundary. The worker publishes the applied configuration revision in diagnostics.

## Reset and discontinuity handling

A reset generation is incremented for:

- Aircraft change.
- Teleport or large local-position jump.
- Replay seek.
- Large simulator-time discontinuity.
- Profile geometry change.
- Scenery/local-coordinate rebase.
- Detected numerical corruption.

The worker handles a reset before consuming later snapshots. Renderers discard packets from older reset generations.

## Shutdown ordering

1. Stop accepting companion commands.
2. Unregister X-Plane callbacks that can produce new work.
3. Publish worker stop request.
4. Join simulation worker with a bounded shutdown strategy.
5. Stop I/O worker after flushing bounded diagnostics.
6. Destroy renderer resources on their required thread/context.
7. Destroy XPLM resources.

No detached worker may survive plugin unload.

## Thread assertions

Development builds should assert:

- XPLM entry points are called only from the registered host thread.
- Simulation services are mutated only by the simulation worker.
- Published frames are immutable.
- Renderer resources are accessed only from their backend-approved thread.

## Overrun behaviour

If simulation exceeds its budget:

1. Rendering continues with the previous valid frame.
2. Budget manager reduces update frequency or detail.
3. Telemetry records the overrun and responsible module.
4. The engine does not block X-Plane to catch up.
5. Persistent state is simplified or compressed rather than suddenly deleted where possible.