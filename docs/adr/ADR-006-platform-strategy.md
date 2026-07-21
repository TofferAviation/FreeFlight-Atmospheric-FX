# ADR-006 — Platform and Graphics Strategy

Status: **Accepted for Version 1 design**

## Context

Advanced atmospheric rendering depends on graphics-context access, driver behaviour, and X-Plane backend support. Requiring an advanced GPU path before the simulation architecture is proven would place the project at unnecessary risk.

## Decision

- Keep the simulation core platform-independent modern C++.
- Target Windows 64-bit first for the commercial Version 1 implementation and advanced rendering research.
- Maintain an XPLM particle/instance compatibility backend.
- Place ribbon, billboard, compute, and volumetric work behind renderer interfaces and capability checks.
- Adopt sparse volumetric rendering only after an explicit stability, compatibility, and performance gate.

## Consequences

- Advanced rendering can evolve without rewriting physics.
- Other platforms can use the compatibility backend until an approved advanced backend exists.
- Build and packaging must identify backend capabilities and fallback status.
- The engine cannot assume direct Vulkan access merely because X-Plane is using Vulkan.

## Rejected alternatives

- Vulkan-only engine: rejected because plugin integration and platform compatibility are not guaranteed.
- Delaying all architecture until final graphics technology is selected: rejected because simulation and renderer contracts can be designed independently.