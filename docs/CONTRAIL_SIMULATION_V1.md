# Atmospheric Engine v1 Contrail Formation and Decay Baseline

Status: **Implemented engineering baseline**

## Scope

This milestone adds the first deterministic contrail simulation to the offline replay pipeline. It is intentionally separated from X-Plane rendering so the same recorded flight and settings can be evaluated repeatedly.

Implemented behavior:

- engine-running and N1-gated exhaust sources;
- pressure, altitude, temperature, humidity, and engine-load formation potential;
- one independently tracked parcel stream per recorded engine;
- emission into the engine-owned geodetic East/Up/North world frame;
- wind advection using recorded X-Plane local wind components;
- initial wake-pair descent that decays with parcel age;
- turbulence-assisted trail spreading;
- dry-air sublimation below 100% humidity relative to ice;
- bounded growth support above 100% humidity relative to ice;
- optical-depth and normalized ice-mass visibility removal;
- bounded active-parcel capacity;
- frozen physics during X-Plane pause and replay;
- deterministic aggregate timeline and hash;
- text summary and CSV export.

## Output files

Running `FFAtmoReplayRunner` now creates:

```text
<recording>-runner-summary.txt
<recording>-normalized.csv
<recording>-contrail-summary.txt
<recording>-contrail-timeline.csv
```

## Units and calibration warning

`normalizedIceMass` is a stable visual-equivalent engineering unit. It is not calibrated physical ice mass in kilograms.

The current formation-temperature ceiling is a tunable pressure/engine-load mixing-line approximation. It is not yet a complete aircraft-specific Schmidt-Appleman implementation. This is deliberate: the current milestone establishes deterministic lifecycle behavior, source accounting, decay, coordinate continuity, and measurable output before introducing aircraft-efficiency and fuel-property contracts.

## Determinism contract

Given the same:

- runner build;
- `.ffar` replay;
- simulation settings;
- compiler/platform contract;

the simulation must produce the same parcel accounting, timeline, and deterministic hash.

## Automated coverage

Tests currently cover:

- cold high-altitude formation;
- warm-air formation rejection;
- non-persistent dry-air parcel loss;
- ice-supersaturated preservation/growth behavior;
- pause and X-Plane replay time freezing;
- deterministic repeatability;
- bounded parcel-capacity behavior;
- finite aggregate outputs.

## Deferred

- calibrated aircraft engine efficiency;
- fuel hydrogen content and emission indices;
- soot activation and ice-crystal number concentration;
- physically calibrated ice water content;
- vortex-pair circulation from aircraft mass and geometry;
- shear-driven persistent spreading and contrail cirrus;
- engine exhaust locations from the accepted ACF geometry profile;
- live X-Plane rendering.
