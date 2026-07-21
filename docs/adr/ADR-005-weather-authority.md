# ADR-005 — Weather Authority and Provenance

Status: **Accepted for Version 1 design**

## Context

Atmospheric effects must match the weather the user sees and flies through in X-Plane. Persistent trails may extend beyond the location of a single point sample, and some desired quantities may be unavailable directly.

## Decision

X-Plane's simulated atmosphere is authoritative. External weather services do not override live effect conditions.

Every atmospheric value carries provenance: direct, derived, estimated, or unavailable. Spatial weather away from the aircraft is reconstructed from X-Plane regional data, cached flight-path history, and documented interpolation or fallback methods.

## Consequences

- The environment model must expose confidence and provenance.
- Missing humidity, fuel flow, or vertical-air-motion inputs cannot silently become physically exact values.
- Replay files record the actual inputs supplied to the engine.
- The companion may display external weather for comparison only when clearly separated from authoritative simulation inputs.

## Rejected alternatives

- Live internet weather as authority: rejected because it can disagree with the simulator.
- Hidden constants presented as measured atmosphere: rejected because they make tuning and validation misleading.