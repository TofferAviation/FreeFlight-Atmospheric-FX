# ADR-008 — ACF-Derived Aircraft Geometry Authority

Status: **Accepted**

## Context

Atmospheric effects require aircraft-relative positions and orientations for engines, exhaust outlets, wingtips, lifting surfaces, rotors, propellers, landing gear, and other source geometry.

The initial design treated manually maintained aircraft profiles as the primary geometry source because public DataRefs do not expose a single clean geometry contract. Examination of a real X-Plane 12 Boeing 737-800NG `.acf` demonstrates that the aircraft definition itself contains the detailed indexed records needed to derive these source locations, including engine centres, exhaust offsets, engine orientation, thrust limits, lifting-surface segments, chords, sweep, dihedral, control assignments, mass, and identity.

## Decision

The active aircraft's `.acf` file is the primary authority for static aircraft geometry and configuration.

A version-aware parser converts ACF records into an immutable, normalized `AircraftGeometryProfile`. Physics and effect modules consume this normalized profile and never parse ACF text directly.

The profile is therefore a generated cache and validation product, not a manually authored replacement for the aircraft definition.

## Precedence

1. Parsed ACF source data.
2. Geometry derived from connected ACF records.
3. Small, versioned aircraft-specific override patches where the base ACF is incomplete or differs from plugin-controlled visual geometry.
4. Conservative fallback or disabling of geometry-dependent effects.

## Consequences

### Positive

- Broad aircraft support without maintaining full manual profiles for every aircraft.
- Effect sources remain aligned with the aircraft's actual flight-model definition.
- Engine, wing, rotor, and gear geometry share one consistent source.
- Profiles can be regenerated when aircraft updates change the ACF.
- Manual integration work is limited to exceptional corrections.

### Costs and risks

- The parser must support multiple ACF format versions explicitly.
- ACF coordinate frames, units, record topology, and indexed arrays require validation.
- Third-party aircraft may use custom plugins or object transforms not represented completely in the base ACF.
- Malformed or unexpected files must be treated as untrusted input and must never crash or block X-Plane.

## Threading

X-Plane's main thread supplies the active aircraft path and lifecycle event. File reading, hashing, parsing, and validation occur off the simulator thread without XPLM calls. A completed immutable profile is published atomically at a safe boundary.

## Validation requirement

Production use requires an in-simulator debug overlay that verifies parsed engine centres, exhaust origins, thrust axes, connected wing surfaces, derived wingtips, datum, and CG against the visible aircraft.

## Related documents

- `docs/ACF_GEOMETRY_PIPELINE.md`
- `docs/fixtures/737_80NG_ACF_ANALYSIS.md`
- `docs/CORE_DATA_CONTRACTS.md`
- `docs/DATAREF_SOURCE_INVENTORY.md`
