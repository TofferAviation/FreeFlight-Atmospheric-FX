# Core Data Contracts

This document defines the semantic contracts that separate X-Plane input, physical simulation, effect modules, and rendering. Exact C++ layout is deferred until interface review.

## Contract rules

1. Physical quantities use SI units unless the field explicitly states otherwise.
2. Coordinate frames and handedness are explicit in every transform-bearing structure.
3. Time values distinguish simulator time, monotonic engine time, and wall-clock time.
4. Structures crossing threads are immutable after publication.
5. Optional and estimated values carry validity and provenance metadata.
6. Serialization formats are versioned independently from C++ binary layout.
7. Render contracts may approximate physical state but may not modify it.

## Common types

### QuantityQuality

```text
Unavailable
Estimated
Derived
Direct
Validated
```

### SampleMetadata

```text
quality
sourceIdentifier
sampleTime
ageSeconds
confidence [0..1]
```

### Coordinate conventions

The core uses a documented right-handed local-world frame expressed in metres. The host adapter performs conversion from X-Plane local coordinates. Geodetic position is retained separately for rebasing and replay.

## SimulatorSnapshot

An immutable, coherent sample collected by the X-Plane host adapter.

```text
SimulatorSnapshot
  schemaVersion
  sequenceNumber
  simulatorTimeSeconds
  monotonicTimeSeconds
  deltaTimeSeconds
  paused
  replaying
  timeAcceleration

  geodeticPosition
  localWorldTransform
  linearVelocityWorldMps
  accelerationWorldMps2
  angularVelocityBodyRadps
  pitchRollHeading

  rawAtmosphere
  rawPropulsion
  rawAerodynamics
  rawGroundInteraction

  aircraftIdentity
  simulatorBuild
  dataValidityMask
```

Requirements:

- All fields represent one collection cycle.
- No pointers to X-Plane memory.
- No XPLM handles.
- Missing values remain missing rather than silently becoming zero.

## EnvironmentState

Canonical atmosphere at one spatial and temporal sample.

```text
EnvironmentState
  positionWorld
  geodeticPosition
  temperatureK
  staticPressurePa
  airDensityKgM3
  dynamicViscosityPaS
  relativeHumidityWater
  relativeHumidityIce
  waterVapourMixingRatio
  windWorldMps
  verticalAirVelocityMps
  turbulenceIntensity
  turbulenceLengthScaleM
  precipitationRate
  icingPotential
  contrailFormationPotential
  contrailPersistencePotential
  provenancePerField
```

The environment model may also produce an `EnvironmentFieldView` for sampling cached atmosphere along persistent trail regions.

## AircraftState

Simulator-independent aircraft state.

```text
AircraftState
  aircraftId
  profileId
  worldTransform
  linearVelocityWorldMps
  accelerationWorldMps2
  angularVelocityBodyRadps
  trueAirspeedMps
  mach
  angleOfAttackRad
  sideslipRad
  loadFactor
  massKg
  wingspanM
  referenceAreaM2
  engines[]
  liftingSurfaces[]
  rotors[]
  groundContacts[]
```

### EngineState

```text
EngineState
  stableId
  type
  running
  worldTransform
  exhaustDirectionWorld
  thrustN
  fuelFlowKgps
  exhaustMassFlowKgps
  exhaustTemperatureK
  shaftPowerW
  normalizedPower
  valueProvenance
```

## AircraftProfile

Versioned data defining physical source locations and aircraft integration.

```text
AircraftProfile
  schemaVersion
  profileId
  displayName
  aircraftMatchRules
  characteristicDimensions
  engineDefinitions[]
  liftingSurfaceDefinitions[]
  rotorDefinitions[]
  groundContactDefinitions[]
  integrationOverrides[]
  calibrationMetadata
```

Profile parsing must reject unknown required fields, invalid units, duplicate stable IDs, and impossible geometry.

## EffectSource

Generic physical source consumed by effect modules.

```text
EffectSource
  sourceId
  sourceType
  ownerAircraftId
  worldTransform
  linearVelocityWorldMps
  angularVelocityWorldRadps
  activation [0..1]
  characteristicRadiusM
  massFlowKgps
  moistureFlowKgps
  sensibleHeatFlowW
  temperatureK
  circulationM2ps
  impulseNsPerSecond
  composition
  environmentSample
  sourceFlags
```

Not every field applies to every source. Applicability is defined by `sourceType` and capability flags.

## EffectModuleContext

Read-only services supplied to an effect module during simulation.

```text
EffectModuleContext
  simulationTime
  deltaTime
  qualityTier
  allocatedBudget
  environmentSampler
  trailService
  vortexService
  advectionService
  turbulenceService
  microphysicsService
  spatialIndex
  diagnosticsSink
```

Modules receive interfaces, not concrete global singletons.

## TrailParcel

Authoritative Lagrangian state for persistent plume material.

```text
TrailParcel
  parcelId
  effectId
  chainId
  positionWorldM
  velocityWorldMps
  ageSeconds
  radiusM
  volumeM3
  dryAirMassKg
  waterVapourMassKg
  liquidWaterMassKg
  iceMassKg
  temperatureK
  turbulentEnergy
  opticalDepthEstimate
  phaseFlags
  environmentCellId
```

A trail chain contains topology and neighbouring parcel references but does not imply a rendering primitive.

## VortexFilamentSegment

```text
VortexFilamentSegment
  segmentId
  filamentId
  endpointAWorldM
  endpointBWorldM
  circulationM2ps
  coreRadiusM
  ageSeconds
  diffusionRate
  instabilityAmplitude
  active
```

The vortex service exposes induced-velocity sampling; effect modules do not duplicate Biot–Savart or reduced-order wake calculations.

## SimulationFrame

Immutable completed physical state published by the simulation worker.

```text
SimulationFrame
  schemaVersion
  frameNumber
  simulationTime
  environmentSummary
  aircraftSummary
  effectStateViews[]
  spatialIndexView
  diagnosticsSummary
  resetGeneration
```

`SimulationFrame` may use immutable shared storage internally, but consumers cannot mutate it.

## RenderPacket

Renderer-neutral visual reconstruction for one frame.

```text
RenderPacket
  schemaVersion
  sourceSimulationFrame
  cameraParameters
  plumeSegments[]
  ribbonChains[]
  billboardClusters[]
  particleEmitterRequests[]
  volumeBricks[]
  lightInteractionData[]
  debugPrimitives[]
  representationTransitions[]
```

A render packet contains no XPLM, OpenGL, Vulkan, Direct3D, or platform handles.

## ConfigurationSnapshot

Immutable validated settings used by simulation and rendering.

```text
ConfigurationSnapshot
  schemaVersion
  revision
  enabledEffects
  physicalModelSettings
  qualitySettings
  rendererSettings
  debuggingSettings
  profileOverrides
```

Configuration changes are validated off the simulation path and applied atomically at a defined tick boundary.

## ResetEvent

Explicit reset semantics avoid accidental persistence across discontinuities.

```text
ResetEvent
  reason
  generation
  simulatorTimeBefore
  simulatorTimeAfter
  oldAircraftId
  newAircraftId
  preserveEligibleState
```

Reasons include aircraft reload, teleport, large time jump, scenery reload, plugin restart, profile change, replay seek, and numerical fault.

## Interface review exit criteria

The data-contract milestone is complete when:

- Every field has a unit, frame, validity rule, and owner.
- No core contract contains an XPLM or graphics type.
- Replay serialization can represent every required input.
- A dummy effect can consume sources and publish renderer-neutral state.
- Configuration and reset behaviour are unambiguous.