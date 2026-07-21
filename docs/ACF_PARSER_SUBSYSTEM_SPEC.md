# ACF Parser Subsystem Specification

Status: **Ready for implementation review**

## Purpose

The ACF Parser Subsystem converts the loaded aircraft's `.acf` file into a validated, immutable, simulator-independent `AircraftGeometryProfile` used by Atmospheric Engine v1.

It is the first production subsystem to implement after the architecture pack because every geometry-dependent effect needs trustworthy source locations before physics or rendering can be evaluated.

## Responsibilities

The subsystem must:

1. Resolve the active aircraft `.acf` path supplied by the X-Plane host adapter.
2. Read and validate the ACF header and file version.
3. Parse property records without assuming a fixed ordering.
4. Support indexed property paths and sparse arrays.
5. Extract aircraft identity, engines, propulsors, lifting surfaces, mass references, gear, and relevant attachment transforms.
6. Convert source units to SI units.
7. Convert ACF aircraft coordinates into the engine body coordinate convention.
8. Compose local transforms correctly, including engine orientation plus exhaust offset.
9. Derive connected wing chains and candidate wingtip positions from surface topology.
10. Validate all normalized outputs and attach provenance and confidence.
11. Cache successful results and structured validation reports.
12. Fail safely without blocking X-Plane or emitting effects at arbitrary positions.

The subsystem must not:

- call XPLM functions;
- create particle or graphics resources;
- own live engine or weather state;
- make effect-specific formation decisions;
- execute or load code referenced by the aircraft file;
- silently guess the meaning of unknown required records.

## Module boundaries

```text
XPlaneHostAdapter
    supplies loaded ACF path and aircraft-change event
            ↓
AcfProfileService
    owns request lifecycle, cache lookup, and publication
            ↓
AcfFileReader
    bounded file I/O and content hashing
            ↓
AcfTokenizer
    header and property-line tokenization
            ↓
AcfPropertyStore
    raw path/value representation
            ↓
AcfV1200Extractor
    version-specific field extraction
            ↓
AircraftGeometryNormalizer
    units, coordinates, transforms, topology
            ↓
AircraftGeometryValidator
    plausibility, completeness, provenance, confidence
            ↓
Immutable AircraftGeometryProfile
```

## Proposed interfaces

### `IAcfProfileService`

```cpp
class IAcfProfileService {
public:
    virtual ~IAcfProfileService() = default;

    virtual ProfileRequestId requestProfile(
        const std::filesystem::path& acfPath) = 0;

    virtual std::shared_ptr<const AircraftGeometryProfile>
        latestProfile() const = 0;

    virtual ProfileBuildStatus status() const = 0;
    virtual ProfileValidationReport latestReport() const = 0;
};
```

The host submits a path and never waits synchronously for parsing.

### `IAcfFileReader`

```cpp
struct AcfFileImage {
    std::filesystem::path canonicalPath;
    std::string text;
    std::uint64_t byteSize;
    ContentHash contentHash;
    FileTimestamp modifiedTime;
};

class IAcfFileReader {
public:
    virtual Result<AcfFileImage, AcfReadError>
        read(const std::filesystem::path& path) const = 0;
};
```

### `IAcfTokenizer`

```cpp
struct AcfHeader {
    std::string platformMarker;
    int formatVersion;
    std::string fileKind;
};

struct AcfPropertyRecord {
    std::string path;
    std::string rawValue;
    std::uint32_t sourceLine;
};

struct AcfDocument {
    AcfHeader header;
    std::vector<AcfPropertyRecord> properties;
};
```

### `IAcfVersionExtractor`

```cpp
class IAcfVersionExtractor {
public:
    virtual ~IAcfVersionExtractor() = default;
    virtual bool supports(int formatVersion) const = 0;

    virtual Result<RawAircraftDefinition, AcfExtractError>
        extract(const AcfDocument& document) const = 0;
};
```

Version-specific field names stay inside extractors. The rest of the engine must not depend on paths such as `_engn/0/_part_x`.

### `IAircraftGeometryNormalizer`

```cpp
class IAircraftGeometryNormalizer {
public:
    virtual Result<AircraftGeometryProfile, GeometryError>
        normalize(const RawAircraftDefinition& raw) const = 0;
};
```

### `IAircraftGeometryValidator`

```cpp
class IAircraftGeometryValidator {
public:
    virtual ProfileValidationReport validate(
        AircraftGeometryProfile& profile) const = 0;
};
```

Validation may downgrade confidence or disable an incomplete source, but it must not fabricate undocumented geometry.

## Internal representations

### Raw engine definition

```text
RawEngineDefinition
  sourceIndex
  active
  type
  centreAcf
  orientationAcf
  exhaustOffsetLocal
  maximumThrustSource
  propulsorGeometry
  sourceLines
```

### Raw lifting-surface segment

```text
RawLiftingSurfaceSegment
  sourceIndex
  enabled
  rootPositionAcf
  semiLengthSource
  rootChordSource
  tipChordSource
  sweepDegrees
  dihedralDegrees
  incidence
  controlAssignments
  airfoilReferences
  joinMetadata
  sourceLines
```

### Normalized engine definition

```text
EngineGeometry
  stableId
  type
  centreBodyM
  orientationBody
  thrustAxisBody
  exhaustOriginBodyM
  maximumThrustN
  provenance
  confidence
```

### Normalized lifting surface

```text
LiftingSurfaceGeometry
  stableId
  classification
  connectedSegments[]
  rootBodyM
  tipBodyM
  areaM2
  spanM
  meanAerodynamicChordM
  provenance
  confidence
```

## Coordinate contract

The parser must not expose ambiguous `x`, `y`, and `z` values.

The normalized engine body frame is:

- `+X`: aircraft right;
- `+Y`: aircraft up;
- `+Z`: aircraft aft;
- right-handed;
- metres;
- origin and datum explicitly recorded in profile metadata.

The ACF-to-engine transformation must be verified with golden tests and an in-simulator overlay before acceptance.

The 737 fixture confirms engine positions use mirrored lateral values and an aft longitudinal datum. The exact interpretation of CG reference fields and object-datum offsets remains a validation item rather than an assumption.

## Transform composition

For each engine:

```text
engine centre transform
    × engine yaw/pitch orientation
    × local exhaust offset
    = normalized exhaust-origin transform
```

Simply adding the exhaust offset to the centre is not acceptable when orientation is non-zero.

The result must include both:

- exhaust origin;
- exhaust/thrust direction.

## Wing topology strategy

The parser must not derive a wingtip by selecting the largest lateral coordinate.

Required algorithm:

1. Extract active lifting-surface segments.
2. Construct candidate segment endpoints from position, semi-length, sweep, and dihedral.
3. Build a graph using geometric continuity, mirror relationships, join metadata, and compatible airfoil/control assignments.
4. Classify chains into main wing, winglet, horizontal tail, vertical tail, and auxiliary surfaces.
5. Select the main lifting chain using area, span, root proximity, and symmetry evidence.
6. Derive left and right aerodynamic tip points from the accepted topology.
7. Record alternative candidates and confidence in diagnostics.

The 737-800NG fixture includes high-dihedral outer records, so winglet classification must be explicit.

## Parsing strategy

### File format

The initial supported format is ACF `1200 Version`.

Tokenization rules:

- preserve the first three header lines;
- accept property lines beginning with `P `;
- split each property into path and raw value at the first whitespace after the path;
- preserve spaces in string values;
- retain source line numbers;
- ignore blank lines and known structural markers;
- report unknown line types without failing unless they affect required fields.

### Property store

The property store provides:

- exact path lookup;
- prefix enumeration;
- indexed child discovery;
- typed scalar conversion;
- typed array extraction using both discovered indices and `/count` metadata;
- duplicate detection;
- source-line diagnostics.

Sparse arrays are supported. `/count` is evidence, not permission to read missing indices as zero.

## Error model

Errors are classified as:

- `ReadError`: path, access, size, encoding, I/O;
- `HeaderError`: invalid or unsupported header;
- `SyntaxError`: malformed property record;
- `ExtractionError`: required field missing or invalid;
- `NormalizationError`: unit or coordinate conversion failure;
- `ValidationError`: implausible or inconsistent geometry;
- `CacheError`: corrupt or incompatible cache data.

Each error contains:

```text
severity
code
humanMessage
sourcePath
sourceLine
propertyPath
recoverable
suggestedFallback
```

## Fallback policy

1. Use a valid cache for the exact ACF content hash and parser schema.
2. Parse the current `.acf`.
3. Apply a matching, versioned override patch when present.
4. Publish partial geometry only for individually validated sources.
5. Disable effects requiring missing geometry.

No generic hardcoded engine or wingtip positions are permitted as a silent fallback.

## Threading model

### X-Plane main thread

- reads the active aircraft path;
- submits profile requests;
- receives an immutable completed profile through atomic publication;
- never reads or parses the file.

### I/O and parser worker

- canonicalizes and reads the file;
- hashes content;
- tokenizes, extracts, normalizes, validates, and writes cache data;
- never calls XPLM.

### Simulation worker

- consumes only an immutable accepted profile;
- continues existing world-space effects across an aircraft change;
- suspends new geometry-dependent emission while a replacement profile is pending.

## Performance requirements

The `.acf` is approximately 6.35 MB for the initial 737 fixture, so parsing must be designed as a one-time aircraft-load task rather than a frame task.

Targets on the reference Windows system:

- main-thread aircraft-change handling: under 0.10 ms;
- file read and hash: under 100 ms typical;
- tokenization and extraction: under 150 ms typical;
- normalization and validation: under 50 ms typical;
- total uncached profile build: under 300 ms typical, under 1 second hard warning;
- cached profile load: under 50 ms typical;
- zero recurring cost after immutable profile publication.

These are asynchronous latency targets, not frame deadlines.

## GPU responsibility

None.

The parser, normalizer, topology builder, validator, and cache are CPU-only. GPU work begins only after physical effect state is converted into render packets.

## Cache format

The cache is versioned independently from C++ binary layout.

```text
ProfileCacheHeader
  magic
  cacheSchemaVersion
  parserSchemaVersion
  acfFormatVersion
  sourceContentHash
  normalizedCoordinateVersion
  payloadSize
  payloadChecksum
```

The cache is never authoritative; it is deleted and rebuilt on any mismatch or validation failure.

## Security limits

Initial limits:

- maximum file size: 64 MB;
- maximum property records: 1,000,000;
- maximum property path length: 1,024 bytes;
- maximum raw value length: 64 KB;
- maximum discovered engine slots: 64;
- maximum lifting-surface slots: 512;
- reject non-finite numeric values;
- bounded diagnostic count with overflow summary.

## Test plan

### Golden fixture

The user-supplied Boeing 737-800NG ACF is the first private golden fixture and must not be committed unless redistribution permission is confirmed. CI may use a sanitized minimal fixture containing the required structural patterns.

Expected fixture facts include:

- header `1200 Version`;
- name `Boeing 737-800NG`;
- ICAO `B738`;
- two active `JET_2SPOOL` engines;
- mirrored engine lateral positions;
- non-zero engine yaw and pitch;
- local exhaust offsets;
- connected mirrored main-wing segments;
- high-dihedral outer/winglet geometry.

### Required tests

1. Header and version detection.
2. Strings containing spaces.
3. Exact and prefix property lookup.
4. Sparse indexed records.
5. Duplicate paths.
6. Integer, floating-point, boolean, enum, and vector conversion.
7. Engine activation and unused-slot rejection.
8. Engine transform composition.
9. Wing segment endpoint calculation.
10. Connected-chain construction.
11. Main-wing and winglet classification.
12. SI conversion.
13. Coordinate sign and handedness.
14. Truncated and malformed files.
15. Oversized records and limit enforcement.
16. Cache hit, miss, corruption, and schema invalidation.
17. Rapid aircraft switching and cancellation.

## Debug validation tool

The first runtime development build must draw or expose markers for:

- aircraft datum;
- parsed CG reference;
- engine centres;
- transformed exhaust origins;
- thrust axes;
- connected wing chains;
- derived aerodynamic wingtips;
- profile confidence and warnings.

The parser is not accepted for production effect placement until the markers align with the visible 737 from front, rear, side, top, and oblique views.

## Implementation sequence

1. Common result, diagnostic, provenance, transform, and unit types.
2. Bounded file reader and content hashing.
3. ACF 1200 tokenizer and property store.
4. Identity extraction.
5. Engine extraction and transform normalization.
6. Lifting-surface extraction and topology builder.
7. Profile validation and confidence model.
8. Versioned cache.
9. Unit and golden-fixture tests.
10. X-Plane profile service and debug overlay integration.

## Implementation gate

Implementation may start after this specification is accepted. Contrail formation and wake-physics code must not consume ACF records directly; it may consume only the published `AircraftGeometryProfile`.
