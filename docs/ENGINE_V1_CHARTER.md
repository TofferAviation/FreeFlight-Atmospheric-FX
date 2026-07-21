# Atmospheric Effects Engine — Version 1 Charter

Status: **Proposed design baseline**  
Branch: `engine/v1`

## Mission

Build a commercial-quality, modular atmospheric simulation engine for X-Plane 12. The engine consumes simulator atmosphere and aircraft state, evolves physically plausible atmospheric phenomena in real time, and supplies renderer-neutral visual state to one or more rendering backends.

Contrails are the first production effect and the architecture validation case. The engine must support future effects without redesigning the core.

## Product principles

- Physics first, visuals second.
- Simulation-driven, not effect-driven.
- X-Plane weather is authoritative for the simulated environment.
- Physics state is independent from its visual representation.
- XPLM API access is isolated to the X-Plane host thread.
- Persistent effects exist in world space and retain history independently of the aircraft.
- GPU acceleration is optional and replaceable, never a requirement for correctness.
- Features must have measurable performance budgets and graceful quality reduction.
- Hardcoded constants require a physical, numerical, or product justification.

## Version 1 scope

Version 1 includes:

1. A main-thread X-Plane host adapter that creates immutable simulator snapshots.
2. A platform-independent atmospheric simulation core.
3. A dedicated simulation worker and frame-safe state exchange.
4. Environment and aircraft state models.
5. Versioned aircraft profiles and generic effect-source generation.
6. A modular effect interface and effect orchestrator.
7. Lagrangian trail storage and world-space persistence.
8. Paired wake-vortex filaments with circulation, descent, diffusion, and decay.
9. Contrail formation, persistence, microphysical growth, sublimation, and dissipation.
10. Shared wind advection and spatially coherent turbulence.
11. Renderer-neutral render packets.
12. The existing XPLM particle/instance path as a compatibility backend.
13. A ribbon or clustered-billboard contrail renderer prototype.
14. Deterministic recording and replay.
15. Debug visualisation, performance telemetry, and diagnostics export.
16. A second effect module proving that the architecture is not contrail-specific.

## Non-goals for Version 1

Version 1 will not attempt to provide:

- A global full-resolution Navier–Stokes simulation.
- A kilometre-scale dense Eulerian fluid grid.
- A mandatory Vulkan compute pipeline inside X-Plane.
- Final implementations of every planned atmospheric effect.
- Full volumetric cloud replacement.
- Cross-platform parity for the advanced renderer before Windows stability is proven.
- Network services in the simulator's frame-critical path.
- Physics logic embedded in UI code, particle definitions, or aircraft-specific branches.

## Platform policy

- Simulation core: portable modern C++.
- Initial product and advanced-rendering target: Windows 64-bit.
- X-Plane target: X-Plane 12 with a documented minimum supported build.
- Compatibility renderer: XPLM particle/instance backend.
- Advanced rendering: developed behind an interface and enabled only after a compatibility research gate.

## Ownership model

- The plugin owns authoritative live simulation state.
- The companion application owns configuration, profiles, telemetry presentation, debugging, and updates.
- Closing or losing the companion must not stop or corrupt the engine.
- X-Plane atmosphere and aircraft state are inputs; external weather services must not silently override them.

## Engineering quality requirements

- No blocking file, network, or process operations in X-Plane callbacks.
- No XPLM calls from worker threads.
- No unbounded effect-state growth.
- Deterministic tests for core physics.
- Replayable scenario tests for every contrail lifecycle stage.
- Explicit units for physical values.
- Versioned serialization and IPC formats.
- Crash-safe resource ownership and shutdown.
- Automated build, tests, packaging, and rollback-capable updates.

## Definition of done

Version 1 is complete when:

1. A recorded X-Plane flight can be replayed deterministically outside live flight.
2. Contrails form or fail to form based on atmospheric and engine inputs.
3. Two engine trails remain distinct during the young-plume stage.
4. Wake vortices advect historical trail state through counter-rotating, descending paths.
5. Persistent trails move with wind, react to turbulence, and grow or dissipate through a microphysics model.
6. The same physical simulation can be visualised by at least two render backends.
7. A second atmospheric effect is implemented without adding effect-specific dependencies to the engine core.
8. Main-thread, worker-thread, GPU, and memory budgets are measured and enforced.
9. Long-duration, aircraft-reload, pause, time-acceleration, and repositioning tests pass.
10. The update package contains the correct plugin, assets, schema versions, and diagnostics information.

## Scope control

A feature may enter Version 1 only when it helps prove the architecture, contrail lifecycle, second-module extensibility, or commercial stability. Visual extras that do not advance those goals are deferred.