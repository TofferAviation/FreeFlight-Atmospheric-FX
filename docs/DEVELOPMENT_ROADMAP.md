# Development Roadmap

The roadmap is ordered to reduce architectural and commercial risk. Milestones are gated by measurable exit criteria. Visual polish does not allow a milestone to bypass missing tests, diagnostics, or budgets.

## Milestone 0 — Architecture freeze

### Deliverables

- Version 1 charter.
- Architecture overview.
- Core data contracts.
- Threading model.
- Performance budgets.
- Replay specification.
- Initial ADR pack.
- Supported platform and minimum X-Plane policy.

### Exit criteria

- Responsibilities and dependency direction are approved.
- Authoritative process and thread ownership are unambiguous.
- No proposed core contract contains XPLM or graphics API types.
- Version 1 scope and non-goals are accepted.

## Milestone 1 — Instrumentation and replay foundation

### Deliverables

- Immutable `SimulatorSnapshot` implementation.
- X-Plane DataRef catalogue with units, availability, fallback, and provenance.
- Non-blocking recorder queue and chunked replay writer.
- Replay reader and fixed-step clock.
- Headless replay runner linked to the platform-independent core.
- State hashing and module timing telemetry.
- Initial canonical scenario recordings.

### Exit criteria

- A 30-minute recording causes no main-thread stalls.
- Headless replay produces repeatable state hashes.
- Truncated and incompatible files fail safely.
- At least six scenarios run in CI.

## Milestone 2 — Modular engine skeleton

### Deliverables

- Engine library separated from XPLM, UI, updater, and renderer code.
- Environment and aircraft state models.
- Aircraft profile registry and schema validation.
- Generic effect-source generator.
- Effect-module interface and registry.
- Fixed-step scheduler.
- Triple-buffered frame publication.
- Hashed spatial index.
- Budget manager and diagnostics sink.
- Dummy effect module used only to validate architecture.

### Exit criteria

- A new dummy module is added without modifying the orchestrator or host adapter.
- Worker overruns never block X-Plane.
- Teleport, aircraft change, and shutdown tests pass.
- Core library builds and tests without the X-Plane SDK.

## Milestone 3 — Contrail reference physics

### Deliverables

- Contrail formation decision model.
- Relative-humidity-over-ice and persistence classification.
- Engine moisture and heat source estimation with provenance.
- Young-plume mass, temperature, radius, and ice state.
- Reduced-order growth, mixing, sublimation, and dissipation.
- Debug graphs for every state variable.

### Exit criteria

- Canonical atmosphere scenarios produce physically sensible formation outcomes.
- Model parameters have documented sources or engineering justification.
- Missing fuel flow or humidity activates visible diagnostics and declared fallbacks.
- Physics results are independent from renderer settings.

## Milestone 4 — Persistent trails and wake vortices

### Deliverables

- Lagrangian trail parcel service.
- Trail topology and sampling/error controls.
- Paired vortex filament model.
- Circulation estimation, descent, core growth, diffusion, and decay.
- Induced-velocity sampling for trail parcels.
- Ambient wind, shear, and turbulence advection.
- World-space persistence and local-coordinate rebasing.
- Debug rendering of nodes, vortex cores, velocity vectors, and age.

### Exit criteria

- Two young engine trails remain physically and visually distinct in debug state.
- Historical parcels roll into counter-rotating descending wake paths.
- Aircraft turns use historical source positions without forming a single artificial hook.
- Old state remains bounded during a 60-minute test.

## Milestone 5 — Rendering abstraction and compatibility backend

### Deliverables

- Renderer-neutral `RenderPacket` builder.
- Render backend interface and capability query.
- Existing XPLM particle/instance path converted into a pure compatibility backend.
- Debug renderer.
- Near/far representation and transition framework.
- Per-backend timing and fallback selection.

### Exit criteria

- No effect module creates XPLM or graphics resources.
- One simulation recording renders through the compatibility backend and debug backend.
- Advanced-backend failure falls back without losing physical state.

## Milestone 6 — Ribbon and billboard renderer

### Deliverables

- Connected contrail ribbon geometry.
- Clustered billboard volume reconstruction.
- Stable camera-facing basis and curve sampling.
- Translucency ordering strategy.
- Atmospheric colour and sunlight approximation.
- Turbulence-driven edge breakup.
- Distance and optical-importance LOD.
- Temporal stability controls.

### Exit criteria

- Persistent trails look continuous without obvious particle beads.
- Engine-trail gap and wake-core separation remain visible where physically expected.
- High preset stays within GPU budget on the reference system.
- Camera cuts and rapid movement do not produce severe flicker or explosive geometry.

## Milestone 7 — GPU and volumetric research gate

### Deliverables

- Graphics-context compatibility prototype.
- Compute-shader capability test where supported.
- Sparse density-brick prototype for near-camera regions.
- Half-resolution ray-march and temporal reconstruction prototype.
- GPU vendor compatibility matrix.
- Vulkan/OpenGL mode testing and fallback behaviour.
- Written adopt/defer decision.

### Exit criteria

Adopt only if:

- Stability is acceptable across supported X-Plane graphics modes.
- High preset remains within the additional GPU budget.
- Rendering quality is materially better than the ribbon/billboard backend.
- Failure falls back cleanly.

Otherwise defer volumetrics without blocking Version 1.

## Milestone 8 — Second effect module

Preferred candidates: wingtip condensation or engine exhaust.

### Deliverables

- Second module using existing environment, source, advection, turbulence, budget, replay, and renderer contracts.
- Module-specific physics and validation scenarios.
- Shared-service improvements only where genuinely generic.

### Exit criteria

- No contrail-specific logic is added to engine core.
- No architecture redesign is required.
- The second effect has independent controls, budgets, replay tests, and diagnostics.

## Milestone 9 — Commercial hardening

### Deliverables

- Long-duration stability and soak tests.
- Crash-safe resource and state handling.
- Installer and updater verification with rollback.
- Schema migration policy.
- Driver and graphics-mode compatibility testing.
- Profile distribution and validation tooling.
- User diagnostics export.
- Performance presets and auto-selection.
- Release, beta, and development channels.
- Documentation and support runbooks.

### Exit criteria

- Automated release package contains the matching plugin, companion, profiles, assets, schemas, and checksums.
- Update interruption and rollback tests pass.
- A multi-hour flight does not leak unbounded memory or resources.
- Known limitations and fallback modes are documented.

## Immediate work queue

1. Review and approve the design pack.
2. Build the DataRef catalogue.
3. Finalize field-level `SimulatorSnapshot` definitions.
4. Design replay container schemas and compatibility rules.
5. Split a platform-independent `ffatmo_engine` library from the current prototype.
6. Implement the recorder and headless replay runner before modifying contrail physics.

## Risk register

| Risk | Probability | Impact | Early mitigation |
|---|---|---|---|
| Unsafe or limited custom 3D rendering integration | High | High | Compatibility backend, renderer abstraction, early research gate |
| XPLM calls from workers | Medium | Critical | Host adapter, thread assertions, immutable snapshots |
| Incomplete spatial weather for long trails | High | High | Provenance, environment history, regional interpolation, documented uncertainty |
| Transparent overdraw and sorting | High | High | Ribbon research before volumes, representation LOD, optical budgeting |
| Unbounded persistent state | High | Critical | Fixed memory budgets, spatial chunks, compression and retirement |
| Aircraft-specific integrations proliferate | High | High | Versioned data profiles and declared adapters |
| Visual tuning corrupts physics | Medium | High | Separate physical and render state, replay comparisons |
| Update package ships stale plugin or assets | Medium | Critical | Artifact validation, version marker, package integration tests |
| Replay loses determinism | Medium | High | Fixed-step reference mode, state hashes, seeded coherent noise |
| Scope expands before foundation is stable | High | High | Charter, milestone gates, ADR requirement |