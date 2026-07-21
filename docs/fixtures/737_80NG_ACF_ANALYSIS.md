# Boeing 737-800NG ACF Analysis

Status: **Initial parser fixture analysis**  
Source file: user-supplied `737_80NG.acf`  
ACF header: `1200 Version`  
Aircraft name: `Boeing 737-800NG`  
ICAO: `B738`  
Writer version: `124001`  
Aircraft version string: `XP12 FM 2.0.3`

## Purpose

This report validates the decision to derive aircraft-relative effect-source geometry from the loaded `.acf` file. The original aircraft file is not committed to the repository; this document records only the fields and normalized results required to design and test the parser.

## Engine geometry found

The file contains two active `JET_2SPOOL` engine records.

| Engine | ACF centre X | ACF centre Y | ACF centre Z | Yaw | Pitch | Exhaust offset | Max thrust |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0 | -16.2700 ft | -5.7400 ft | 55.2500 ft | +3.5° | +2.0° | (0, 0, +2.5 ft) | 26,300 N |
| 1 | +16.2700 ft | -5.7400 ft | 55.2500 ft | -3.5° | +2.0° | (0, 0, +2.5 ft) | 26,300 N |

Assuming the exhaust offset is expressed in engine-local ACF coordinates, the nominal exhaust-origin positions before orientation correction are:

| Engine | X | Y | Z |
|---|---:|---:|---:|
| 0 | -16.2700 ft / -4.9591 m | -5.7400 ft / -1.7496 m | 57.7500 ft / 17.6022 m |
| 1 | +16.2700 ft / +4.9591 m | -5.7400 ft / -1.7496 m | 57.7500 ft / 17.6022 m |

The parser must apply the engine yaw and pitch to the local exhaust offset rather than simply adding all offsets component-by-component. The table above is therefore a diagnostic baseline, not the final runtime transform.

## Wing geometry found

The `.acf` contains a chain of lifting-surface segments rather than a single wingspan value. The main left/right wing uses mirrored segment records, including:

| Segment pair | Root lateral position | Semi-length | Root chord | Tip chord | Sweep | Dihedral |
|---|---:|---:|---:|---:|---:|---:|
| 0 / 1 | 0.0000 ft | 19.1600 ft | 25.4900 ft | 13.5800 ft | 27.5° | 10.0° |
| 2 / 3 | ±16.7323 ft | 19.9803 ft | 14.4357 ft | 9.5801 ft | 25.4° | 9.0° |
| 4 / 5 | ±34.5472 ft | 21.6864 ft | 9.5801 ft | 5.5774 ft | 25.6° | 12.7° |
| 6 / 7 | ±56.2336 ft | 8.1693 ft | 3.6089 ft | 1.3123 ft | 30.8° | 77.0° |

The high-dihedral outer pair is consistent with an upward wingtip/winglet region. Additional records describe detailed winglet and auxiliary geometry, so selecting the largest lateral coordinate is not sufficient by itself. The parser must build connected lifting-surface chains and classify main wing, winglet, tail, and auxiliary surfaces.

## Reference and aircraft values found

- Aircraft CG reference: `acf/_cgY = -2.05`, `acf/_cgZ = 60.22`.
- Empty mass: `91,514.04` source units.
- Maximum mass: `174,700` source units.
- Aircraft name and ICAO are available directly from the ACF.
- Engine type, position, orientation, thrust limit, propulsor properties, and exhaust offset are available per engine.
- Wing segments expose position, semi-length, root/tip chord, sweep, dihedral, control-surface assignment, airfoil references, and detailed geometry arrays.

Mass and geometry units must be normalized using the ACF format contract and verified against Plane Maker/runtime values before being accepted as production physics inputs.

## Architectural conclusion

The `.acf` should be the primary geometry source. The generated `AircraftGeometryProfile` becomes a normalized cache of parsed ACF data, not a manually authored replacement for it.

Manual aircraft integration is retained only as a small override layer for cases where:

- a third-party aircraft places visual engines or rotors differently from its flight-model definition;
- plugin-controlled geometry is absent from the base ACF;
- a developer intentionally supplies more accurate effect-source locations;
- an ACF version or custom record cannot yet be interpreted safely.

## Parser requirements revealed by this fixture

1. Read and validate the ACF header/version before interpreting fields.
2. Parse indexed property paths without assuming active count equals declared array capacity.
3. Distinguish active engines from unused engine slots.
4. Compose engine-centre, orientation, and exhaust-offset transforms correctly.
5. Construct connected wing-segment chains from mirrored records.
6. Derive wingtips from surface topology, not a hardcoded wingspan.
7. Preserve source values and normalized SI values for diagnostics.
8. Expose confidence and provenance for every derived source point.
9. Allow optional versioned override patches without replacing the ACF-derived profile.
10. Validate all derived positions with an in-simulator debug overlay before enabling production effects.

## First visual validation target

For the initial 737-800NG parser test, the debug renderer must show:

- both engine centres;
- both transformed exhaust origins and thrust axes;
- the connected main-wing segment chain;
- left and right derived wingtip points;
- the aircraft datum and CG reference;
- any parser warnings or overridden values.

The result is accepted only when these markers align with the visible aircraft in X-Plane from several camera angles.
