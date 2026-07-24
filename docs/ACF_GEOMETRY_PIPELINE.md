# ACF-Driven Aircraft Geometry Pipeline

Status: **Architecture baseline**

## Decision

The loaded aircraft's `.acf` file is the primary source for aircraft-relative geometry and configuration. Atmospheric Engine v1 will not require hand-authored effect-source positions for every supported aircraft when the required information can be extracted from the aircraft definition.

The engine will parse the `.acf` when an aircraft is loaded, convert the relevant values into a normalized `AircraftGeometryProfile`, validate the result, and cache it. Simulation modules consume only the normalized profile; they do not parse `.acf` text directly and do not depend on X-Plane-specific field names.

## Why this matters

This allows the engine to support many aircraft without shipping a manually maintained geometry profile for each one. It also ensures engine exhaust, propeller, rotor, wing, flap, gear, and related source locations remain aligned with the actual aircraft definition.

## Data flow

```text
Loaded aircraft path
        ↓
ACF file reader
        ↓
Version-aware ACF parser
        ↓
Raw ACF geometry/configuration
        ↓
Coordinate and unit normalization
        ↓
Geometry validation
        ↓
AircraftGeometryProfile cache
        ↓
EffectSourceGenerator
        ↓
Contrail / vortex / exhaust / rotor-wash / dust modules
```

## Runtime rule

The `.acf` is parsed only when:

- the user's aircraft changes;
- the `.acf` timestamp or content hash changes;
- the cached schema version is incompatible;
- the user explicitly requests a profile rebuild.

It must not be reparsed every frame.

## Normalized output

The parser produces a renderer-neutral and simulator-neutral profile containing, where available:

- aircraft identity and ACF format/version;
- coordinate-system metadata and reference origin;
- engine count, engine type, engine position, orientation, and thrust axis;
- propeller and rotor disc centres, axes, radii, blade count, and rotation direction;
- wing, stabilizer, and lifting-surface segments;
- wingtip positions derived from lifting-surface geometry;
- flap, slat, spoiler, and speedbrake geometry;
- landing-gear and wheel contact geometry;
- aircraft mass and structural reference values needed by wake physics;
- object attachment transforms relevant to effect-source placement;
- source provenance and validation confidence for every generated field.

## Responsibilities

### ACF file locator

- Resolves the current aircraft's relative path from X-Plane.
- Resolves the absolute `.acf` path safely under the X-Plane installation.
- Rejects missing, inaccessible, or unexpectedly external paths.

### Version-aware parser

- Reads the ACF header and format version before interpreting records.
- Supports explicitly tested ACF versions only.
- Preserves unknown records for diagnostics but never guesses their meaning.
- Produces structured parse errors with line/record context.

### Geometry normalizer

- Converts source units into SI units.
- Converts ACF aircraft coordinates into the engine's documented body coordinate system.
- Records every axis/sign conversion in tests.
- Keeps source values alongside normalized values for diagnostics and replay metadata.

### Validator

Checks at minimum:

- finite numeric values;
- plausible engine and wing counts;
- left/right symmetry where expected, without requiring symmetry;
- source positions within a plausible aircraft bounding region;
- positive radii, spans, areas, and masses;
- unique or intentionally shared source identifiers;
- agreement between derived wingtips and aircraft dimensions;
- consistency between engine type and available propeller/rotor geometry.

Validation failures must degrade to a declared fallback rather than producing effects at arbitrary positions.

### Cache

Cache keys include:

- absolute `.acf` path;
- file size and modification timestamp;
- content hash;
- parser schema version;
- engine profile schema version.

The cache stores normalized data, validation reports, and source provenance. It is disposable and may always be rebuilt from the `.acf`.

## Overrides

Some third-party aircraft may use custom objects, plugins, animations, or nonstandard systems that cannot be fully inferred from the base `.acf`.

The architecture therefore supports an optional override layer:

```text
ACF-derived profile
        +
Aircraft-specific override patch
        =
Validated runtime geometry profile
```

Overrides may correct or add fields, but they must be small, versioned patches. They must not duplicate the entire aircraft profile or become the default integration method.

Override precedence is explicit:

1. Parsed `.acf` data.
2. Derived geometry.
3. Validated aircraft-specific override.
4. Conservative fallback or effect disabled.

## Threading

- X-Plane main thread: supplies the loaded aircraft path and aircraft-change event.
- I/O worker: reads and hashes the `.acf`.
- Parser worker: parses and validates geometry without calling XPLM.
- Main thread: atomically publishes the completed immutable profile at a safe boundary.
- Simulation worker: reads the immutable profile.

An aircraft change immediately suspends geometry-dependent emission until the replacement profile is valid. Existing world-space effects may continue according to their stored physical state.

## Security and robustness

- Treat `.acf` content as untrusted input.
- Apply file-size, record-count, array-size, and numeric-range limits.
- Never execute content or interpret paths without canonicalization.
- Reject malformed records without crashing X-Plane.
- Do not allow parser failure to block the simulator thread.

## Testing requirements

- Golden-file parsing tests for each supported ACF version.
- Default X-Plane aircraft plus the X-Crafts Lineage 1000 as initial fixtures.
- Unit tests for coordinates, signs, units, engine positions, wingtips, rotor axes, and gear points.
- Malformed, truncated, oversized, and unknown-record tests.
- Cache invalidation tests.
- Aircraft reload and rapid aircraft-switch tests.
- Comparison tool that overlays parsed source positions in X-Plane for visual verification.

## Milestone 1 deliverable

Before contrail physics implementation, the engine must be able to:

1. locate the active `.acf`;
2. parse its supported geometry records;
3. generate a normalized immutable profile;
4. show engine and wingtip source points in a debug overlay;
5. export a validation report through the companion application.
