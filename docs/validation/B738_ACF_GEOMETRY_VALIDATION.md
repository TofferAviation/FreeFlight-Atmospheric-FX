# Boeing 737-800NG ACF Geometry Validation

Status: **Accepted**

Date: 2026-07-21

## Test aircraft

- Aircraft: Boeing 737-800NG
- ICAO: B738
- ACF format: 1200
- Source file: `737_80NG.acf`
- Test environment: X-Plane 12 on Windows using the Vulkan-safe 2-D geometry overlay

## Automated parser result

- Parse status: OK
- Source size: 6,352,131 bytes
- Properties indexed: 77,220
- Parse time: 49.736 ms
- Diagnostics: 0
- Engines detected: 2
- Main-wing segments detected: 12
- Left and right wingtips derived successfully

## Visual acceptance

The in-simulator overlay was reviewed from an elevated rear-oblique view. The following geometry aligned with the visible aircraft:

- aircraft datum axes;
- both engine centres;
- both exhaust origins;
- both exhaust direction vectors;
- connected main-wing geometry;
- left wingtip;
- right wingtip.

The test confirms that the ACF parser, coordinate conversion, geometry normalization, projection path, and debug-overlay rendering are operational for the Boeing 737-800NG fixture.

## Accepted geometry values

### Engine 1

- Centre: `(-4.959, -1.125, -1.515) m`
- Exhaust: `(-4.913, -1.151, -0.755) m`
- Exhaust direction: `(0.061, -0.035, 0.998)`

### Engine 2

- Centre: `(4.959, -1.125, -1.515) m`
- Exhaust: `(4.913, -1.151, -0.755) m`
- Exhaust direction: `(-0.061, -0.035, 0.998)`

### Derived wingtips

- Left: `(-17.700, 5.281, 6.319) m`
- Right: `(17.700, 5.281, 6.319) m`

## Milestone result

The first real-aircraft ACF geometry fixture is accepted. The engine may now use ACF-derived engine and wing source locations as input to the effect-source generator.

The next milestone is the immutable simulator snapshot and deterministic recorder/replay pipeline. No production contrail physics should depend directly on the ACF parser or XPLM DataRefs; both must publish normalized immutable data contracts first.
