# X-Plane DataRef Catalogue

Status: **Framework — field verification required before implementation**

This document will become the authoritative catalogue of simulator inputs used by the engine. No production subsystem may read a DataRef that is absent from this catalogue.

## Catalogue rules

Every entry must define:

- Engine semantic name.
- X-Plane DataRef name or ordered fallback candidates.
- Data type and array index.
- X-Plane unit.
- Canonical engine unit.
- Sampling frequency.
- Valid range and invalid-value handling.
- Thread ownership.
- Availability by minimum supported X-Plane version.
- Provenance classification: direct, derived, estimated, or unavailable.
- Replay requirement.
- Degradation behaviour when unavailable.
- Validation method and observed test aircraft/weather cases.

## Entry template

| Field | Value |
|---|---|
| Engine semantic | |
| Primary DataRef | |
| Fallback DataRefs | |
| Type/index | |
| X-Plane unit | |
| Engine unit | |
| Sample rate | |
| Expected range | |
| Invalid handling | |
| Provenance | |
| Minimum X-Plane | |
| Replay required | |
| Fallback model | |
| Validation evidence | |

## Required input groups

### Time and lifecycle

- Simulator running time.
- Frame period.
- Pause state.
- Replay state.
- Simulation speed/time acceleration.
- Aircraft-loaded event.
- Scenery/local-coordinate rebase event.

### Aircraft transform and motion

- Local X/Y/Z position.
- Latitude/longitude/elevation.
- Pitch, roll, and heading.
- Local linear velocity.
- Angular velocity or derivable attitude rates.
- Acceleration.
- True airspeed.
- Mach number.
- Angle of attack.
- Sideslip.
- Normal load factor.

### Atmosphere at aircraft

- Temperature.
- Static pressure.
- Air density when available or derivation inputs.
- Dew point or humidity representation.
- Relative humidity over water when available.
- Wind vector or speed/direction.
- Vertical air velocity when available.
- Turbulence.
- Precipitation.
- Icing and cloud-water inputs where available.

### Regional and layered atmosphere

- Weather layer altitudes.
- Temperature by layer.
- Wind by layer.
- Turbulence by layer.
- Shear or values required to derive shear.
- Regional humidity/cloud state where available.

Layer count and altitude positions must be read dynamically rather than hardcoded.

### Propulsion

Per engine where available:

- Running/burning state.
- N1/N2 or normalized power.
- Thrust.
- Fuel flow.
- Engine type.
- Exhaust or turbine temperature proxies.
- Propeller/rotor RPM and power for non-jet sources.

### Aircraft mass and geometry

- Total mass.
- Wingspan.
- Reference wing area.
- Flap/slat/spoiler state.
- Gear state.
- Aircraft path and identity.

Geometry source locations primarily come from the validated aircraft profile, not guessed DataRefs.

### Ground interaction

For future dust, snow, and rotor wash:

- Height above ground.
- Surface type when available.
- Wheel contact/compression.
- Ground speed.
- Precipitation and surface contamination proxies.

### Camera and rendering

Only the host/render adapter may use:

- Camera position and orientation.
- Projection/view data where supported.
- Visibility distance.
- Graphics mode/capability information.

These values are not part of authoritative physical state unless explicitly justified.

## Derived values

Derived quantities require documented equations and input quality propagation:

- Air density from temperature and pressure.
- Relative humidity over ice from available humidity representation.
- Wind vector from speed/direction.
- Wind shear from dynamically located layers.
- Aircraft acceleration from filtered velocity history when direct data is unavailable.
- Approximate thrust or exhaust mass flow from engine proxies.
- Wake circulation from mass, speed, density, and geometry.

## Sampling classes

- **Per host snapshot:** position, orientation, velocity, engine state, local atmosphere.
- **10 Hz:** slowly changing aircraft configuration and derived values.
- **2–5 Hz:** regional weather and layered atmosphere.
- **On event:** aircraft identity, profile reload, scenery rebase, plugin capabilities.

Actual rates remain budget-controlled and must be sufficient for replay fidelity.

## Validation process

1. Verify DataRef existence in the minimum supported X-Plane build.
2. Record values in known scenarios.
3. Compare units and sign conventions against simulator behaviour.
4. Test missing and invalid values.
5. Verify arrays and layer counts dynamically.
6. Add a replay containing the scenario.
7. Mark the entry validated only after evidence is recorded.

## Implementation gate

`XPlaneHostAdapter` implementation may begin only after the minimum required Milestone 1 entries have been filled and reviewed. Placeholder names or assumed units are not acceptable in production code.