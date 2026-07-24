# ADR-007 — Version 1 Scope and Proof Strategy

Status: **Accepted for Version 1 design**

## Context

The long-term product includes contrails, wing condensation, wake vortices, exhaust, rotor wash, smoke, dust, snow, and future phenomena. Attempting all effects before proving the engine foundation would create broad but fragile software.

## Decision

Version 1 proves the architecture with:

1. One complete physically driven contrail lifecycle.
2. At least two renderer representations of the same physical state.
3. One additional atmospheric effect module implemented without redesigning the core.
4. Deterministic replay, diagnostics, budgets, and commercial hardening.

All other effects remain roadmap items until the module system and shared services pass this proof.

## Consequences

- Contrails receive deep engineering rather than many shallow effect implementations.
- The second module is an architectural acceptance test.
- New visual ideas do not enter Version 1 unless they prove a shared system or required product stability.
- Milestone gates and non-goals control scope.

## Rejected alternatives

- Implement all named effects in parallel: rejected due to duplicated infrastructure and uncontrolled scope.
- Ship contrails only without a second module: rejected because contrail-specific architecture could appear modular without actually being reusable.