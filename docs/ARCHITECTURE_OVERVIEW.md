# Architecture Overview

## Architectural style

Atmospheric Effects Engine v1 is a hybrid real-time simulation system built around immutable simulator snapshots, a worker-owned physical world model, modular effect modules, and renderer-neutral output.

The engine deliberately avoids using visual particles as authoritative simulation state. Physical state is represented by sources, Lagrangian parcels, vortex filaments, fields, and effect-specific state. Renderers reconstruct visible effects from that state.

## End-to-end data flow

```text
X-Plane DataRefs and SDK events
              |
              v
      XPlaneHostAdapter
              |
      immutable SimulatorSnapshot
              |
              v
       EnvironmentModel  +  AircraftStateModel
              |                     |
              +----------+----------+
                         v
               EffectSourceGenerator
                         |
                 EffectSource list
                         |
                         v
                EffectOrchestrator
        +----------------+----------------+
        |                |                |
  ContrailEffect   Future effects   Shared services
        |                         Trail / vortex / advection /
        |                         turbulence / microphysics
        +----------------+----------------+
                         v
                  SimulationFrame
                         |
                  RenderPacketBuilder
                         |
                         v
                    RenderPacket
             +-----------+-----------+
             |                       |
    XPLM compatibility          Advanced renderer
       backend                  ribbon / volume
```

## Module boundaries

### 1. XPlaneHostAdapter

The only subsystem allowed to call XPLM APIs.

Responsibilities:

- Resolve and read DataRefs.
- Receive aircraft, scenery, pause, and lifecycle messages.
- Collect a coherent per-tick `SimulatorSnapshot`.
- Convert X-Plane coordinate and timing information into canonical engine values.
- Submit SDK-safe rendering resources and commands.
- Report plugin lifecycle and diagnostics.

Forbidden responsibilities:

- Effect physics.
- File or network I/O.
- Blocking synchronization.
- Aircraft-specific effect logic.

### 2. EnvironmentModel

Converts raw weather inputs into a coherent `EnvironmentState` containing canonical SI units, quality flags, derived density, relative humidity over water and ice, wind vectors, turbulence metrics, and atmosphere validity.

It records the provenance of each value: direct, derived, estimated, or unavailable.

### 3. AircraftStateModel

Converts simulator aircraft state into a renderer- and aircraft-independent `AircraftState` containing transform, velocity, acceleration, angular motion, aerodynamic state, and normalized propulsion state.

### 4. AircraftProfileRegistry

Loads and validates versioned aircraft profiles. Profiles define engine outlets, wing tips, lifting surfaces, rotors, wheel contact regions, characteristic dimensions, and integration overrides.

Profiles are data, not code. Aircraft-specific DataRefs are accessed through declared integration adapters rather than scattered conditions.

### 5. EffectSourceGenerator

Produces generic `EffectSource` records from `AircraftState`, `EnvironmentState`, and the active profile.

Examples:

- Jet exhaust source.
- Wingtip circulation source.
- Lifting-surface condensation source.
- Rotor downwash source.
- Wheel dust source.
- Smoke source.

### 6. EffectOrchestrator

Owns effect-module registration, dependencies, update schedules, memory budgets, quality settings, and lifecycle.

It exposes shared services but does not contain contrail-specific rules.

### 7. Shared simulation services

#### LagrangianTrailService

Owns persistent world-space parcels and connected trail chains. Supports creation, ageing, advection, splitting, merging policy, spatial indexing, compression, and retirement.

#### VortexFilamentService

Owns paired or general vortex filaments. Computes induced velocity, circulation decay, core growth, descent, turbulence distortion, and instability approximations.

#### AdvectionService

Combines ambient wind, wind shear, vertical velocity, wake-induced velocity, buoyancy, settling, and turbulence into parcel motion.

#### MicrophysicsService

Provides water/ice phase evolution, condensation, freezing, sublimation, mixing, dilution, and optical-property estimates.

#### TurbulenceService

Provides deterministic, spatially coherent multiscale deformation. Random values are seeded by world position, simulation time, scenario seed, and effect identity to prevent frame-to-frame flicker.

#### SpatialIndex

A hashed world-space grid used for visibility, active simulation regions, neighbour queries, weather history, culling, and state retirement.

#### BudgetManager

Assigns CPU, GPU, memory, update-rate, distance, and representation budgets to modules. It reduces fidelity gradually under pressure.

### 8. Effect modules

Every effect module implements an interface equivalent to:

```text
IAtmosphericEffect
  describeCapabilities()
  configure()
  ingestSources()
  simulate()
  contributeRenderState()
  reset()
  serializeDiagnostics()
```

An effect may depend on shared services but may not access XPLM, UI state, graphics APIs, or another effect's private state.

### 9. RenderPacketBuilder

Converts immutable simulation state into renderer-neutral primitives:

- Plume segments.
- Ribbon control points.
- Billboard clusters.
- Particle emitter requests.
- Sparse volume bricks.
- Lighting parameters.
- Debug primitives.

### 10. Render backends

#### XplmParticleBackend

Compatibility and fallback renderer. It may translate render packets into X-Plane particle systems and instances but must not own effect physics.

#### RibbonBillboardBackend

Primary research renderer for connected persistent trails. Intended to deliver continuous shape, stable near/far LOD, and lower overdraw than large independent particles.

#### SparseVolumeBackend

Optional high-quality renderer for nearby chunks only. Adoption requires a separate compatibility and performance decision gate.

#### DebugRenderer

Displays sources, trail nodes, vortex cores, velocity vectors, bounds, optical depth, age, culling state, and budgets.

### 11. Replay and diagnostics

The replay subsystem records input snapshots, configuration changes, profile identity, random seed, timing, and schema versions. The same recording drives headless physics tests, visual comparison, and performance profiling.

### 12. Companion application

The companion provides configuration, profile editing, telemetry, debug controls, replay management, update management, and diagnostics export through a versioned protocol.

The companion is never required for live simulation correctness.

## Dependency direction

Dependencies point inward toward stable contracts:

```text
X-Plane host -> core contracts <- effect modules
render backends -> render contracts <- simulation core
companion IPC -> versioned control contracts <- plugin
```

The simulation core does not depend on XPLM, ImGui, DirectX, OpenGL, Vulkan, update services, or the companion executable.

## State ownership

- X-Plane host owns current raw simulator access.
- Simulation worker owns mutable physical state.
- Published simulation frames are immutable.
- Render backends own GPU and XPLM rendering resources.
- Companion owns presentation and user configuration drafts.
- Profile registry owns immutable validated profile definitions.

## Failure behaviour

- Missing atmosphere values reduce model confidence and activate documented fallbacks.
- Companion disconnection leaves the last valid configuration active.
- Advanced-renderer failure falls back to the compatibility renderer.
- Worker overruns cause reuse of the previous completed frame, never a main-thread wait.
- Teleports, aircraft reloads, and large time discontinuities explicitly reset or rebase persistent state.
- Invalid profiles are rejected with diagnostics and a safe baseline profile.