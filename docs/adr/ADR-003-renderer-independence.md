# ADR-003 — Renderer Independence

Status: **Accepted for Version 1 design**

## Context

X-Plane integration, platform support, transparency, and graphics-backend compatibility are significant risks. Coupling contrail physics to one particle or graphics API would make the engine difficult to evolve and test.

## Decision

Physical simulation modules publish renderer-neutral state. A separate render-packet builder produces generic visual primitives. Backend implementations translate those primitives into XPLM particles, ribbons, billboards, volumes, or debug geometry.

Effect modules may provide visual attributes derived from physical state but may not allocate or control XPLM, OpenGL, Vulkan, Direct3D, or platform resources.

## Consequences

- The existing particle path becomes a compatibility backend rather than the engine architecture.
- Multiple backends can visualise the same replay and physical frame.
- Renderer failure can fall back without discarding simulation state.
- Render-packet schemas and representation-transition rules become explicit contracts.

## Rejected alternatives

- Particle definitions containing lifecycle physics: rejected because it couples correctness to one renderer.
- One advanced renderer with no fallback: rejected due to platform and driver risk.