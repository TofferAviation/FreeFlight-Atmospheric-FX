# Atmospheric Effects Engine v1 — Design Pack

This directory is the authoritative design baseline for the next-generation Atmospheric Effects Engine.

## Permanent design principles

1. Physics first, visuals second.
2. Simulation-driven, not effect-driven.
3. Modular architecture with replaceable backends.
4. Use X-Plane's atmosphere as the authoritative environment.
5. Keep X-Plane callbacks fast, deterministic, and main-thread safe.
6. Separate simulation state from rendering state.
7. Use particles only where particles are the correct representation.
8. Avoid hardcoded values unless physically justified and documented.
9. Every subsystem must degrade gracefully under performance pressure.
10. Commercial-quality testing, telemetry, crash safety, and update reliability are product features.

## Documents

- [ENGINE_V1_CHARTER.md](ENGINE_V1_CHARTER.md) — product scope, non-goals, platform policy, and definition of done.
- [ARCHITECTURE_OVERVIEW.md](ARCHITECTURE_OVERVIEW.md) — module boundaries, dependencies, and end-to-end data flow.
- [CORE_DATA_CONTRACTS.md](CORE_DATA_CONTRACTS.md) — canonical engine data structures and ownership rules.
- [THREADING_MODEL.md](THREADING_MODEL.md) — thread ownership, synchronization, and frame safety.
- [PERFORMANCE_BUDGETS.md](PERFORMANCE_BUDGETS.md) — CPU, GPU, memory, and scalability targets.
- [REPLAY_SYSTEM_SPEC.md](REPLAY_SYSTEM_SPEC.md) — deterministic recording, replay, regression, and diagnostics design.
- [DATAREF_CATALOGUE.md](DATAREF_CATALOGUE.md) — authoritative catalogue rules and implementation gate.
- [DATAREF_SOURCE_INVENTORY.md](DATAREF_SOURCE_INVENTORY.md) — first source pass from the supplied X-Plane `DataRefs.txt`; runtime validation is still required.
- [ACF_GEOMETRY_PIPELINE.md](ACF_GEOMETRY_PIPELINE.md) — version-aware ACF parsing, normalization, validation, caching, and override design.
- [fixtures/737_80NG_ACF_ANALYSIS.md](fixtures/737_80NG_ACF_ANALYSIS.md) — first real-aircraft parser analysis based on the Boeing 737-800NG ACF.
- [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) — milestones, exit criteria, and technical risk gates.
- [adr/](adr/) — Architecture Decision Records.

## Branch policy

The `engine/v1` branch is the architecture and implementation branch for the new engine. Existing `release/v*` branches remain prototype and product-release branches. New engine work must not be developed directly on `main` or a release branch.

## Change policy

Any change that violates a permanent design principle, changes thread ownership, changes authoritative state, or couples physics to a renderer requires a new or amended Architecture Decision Record before implementation.
