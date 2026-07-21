# ADR-002 — Persistent Simulation Representation

Status: **Accepted for Version 1 design**

## Context

Persistent contrails and future smoke, dust, snow, and rotor effects extend far behind moving sources. A global high-resolution fluid grid would be too expensive and difficult to integrate in real time.

## Decision

Use reduced-order physical representations:

- Lagrangian trail parcels and connected chains for transported material.
- Vortex filaments for coherent wake and rotor circulation.
- Analytical or reduced-order source models.
- Sparse local fields only where an effect genuinely needs them.

A global three-dimensional Eulerian fluid grid is not the Version 1 authority.

## Consequences

- Persistent state scales with effect importance rather than world volume.
- Trails can survive aircraft movement and be advected by wind.
- Spatial indexing, resampling, compression, and bounded retirement are required.
- Optional local GPU fluid refinement may be layered on later without replacing authoritative state.

## Rejected alternatives

- Visual particles as authority: rejected because appearance parameters cannot reliably represent mass, phase, or persistent wake history.
- Global CFD grid: rejected because cost, memory, boundary conditions, and plugin integration are incompatible with the Version 1 budget.