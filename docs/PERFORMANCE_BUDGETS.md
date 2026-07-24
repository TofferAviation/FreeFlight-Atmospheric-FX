# Performance Budgets

These are Version 1 engineering targets. They are measured targets, not marketing guarantees. Every subsystem must report its actual cost and degrade gracefully when its allocation is exceeded.

## Reference conditions

Budgets are evaluated under:

- One supported twin-engine aircraft.
- A persistent contrail history of at least 20 minutes.
- Typical cruise speed and weather.
- High quality preset.
- 60 FPS target with acceptable operation down to 20 FPS.
- Windows 64-bit reference platform.

Additional stress scenarios cover manoeuvres, turbulence, long persistence, multiple effects, time acceleration, and repositioning.

## Main-thread CPU budget

| Work | Average target | Warning threshold | Hard design limit |
|---|---:|---:|---:|
| DataRef collection and snapshot assembly | 0.25 ms | 0.50 ms | 0.75 ms |
| Command and lifecycle administration | 0.10 ms | 0.25 ms | 0.50 ms |
| Completed-frame swap and render submission preparation | 0.15 ms | 0.35 ms | 0.75 ms |
| Total engine main-thread overhead, excluding backend draw cost | 0.50 ms | 0.90 ms | 1.50 ms |

The X-Plane thread never waits for simulation, I/O, companion, or update work.

## Simulation-worker CPU budget

High preset target:

| Subsystem | Average target |
|---|---:|
| Environment and aircraft state models | 0.15 ms |
| Source generation | 0.10 ms |
| Contrail formation and microphysics | 0.35 ms |
| Trail advection and topology | 0.45 ms |
| Vortex filament update and induced velocity | 0.40 ms |
| Turbulence sampling | 0.20 ms |
| Spatial indexing and culling preparation | 0.20 ms |
| Render-packet construction | 0.15 ms |
| Total | 2.00 ms |

Short spikes may reach 4 ms, but repeated spikes trigger adaptive quality reduction.

## GPU budget

High preset targets:

- Compatibility particle backend: ≤ 1.0 ms average.
- Ribbon/billboard backend: ≤ 1.5 ms average.
- Optional nearby volumetric refinement: additional ≤ 1.0 ms average.
- Total effect rendering: ≤ 2.0 ms average, ≤ 3.5 ms at the 99th percentile on the reference system.

GPU timing must be collected per pass where the API permits it.

## Memory budget

High preset:

| Category | Target maximum |
|---|---:|
| Physical simulation state | 64 MB |
| Spatial index and environment history | 32 MB |
| Render packets and CPU staging | 32 MB |
| GPU buffers and textures | 96 MB |
| Replay ring buffer and diagnostics | 24 MB |
| Reserved headroom | 8 MB |
| Total | 256 MB |

Persistent state must remain bounded by budget, not only by effect age.

## Contrail capacity targets

Version 1 should support, at High:

- At least 20 minutes of persistent trail history for the user's aircraft under ordinary sampling.
- At least two distinct young engine trails.
- Thousands of Lagrangian parcels or an equivalent compressed representation.
- Paired wake filaments over the coherent-wake region.
- Coarser representation for old and distant trail regions.

The exact parcel count is an implementation detail controlled by error metrics and budget.

## Quality tiers

### Low

- Reduced source and trail sampling.
- Shorter simulated and rendered distance.
- Compatibility particle backend.
- Lower turbulence octaves.
- No volumetric refinement.
- Aggressive old-trail compression.

### Medium

- Moderate trail sampling.
- Ribbon or compatibility backend depending on support.
- Basic coherent turbulence.
- Moderate persistence and lighting.

### High

- Full Version 1 physical model.
- Dense near-field trail sampling.
- Ribbon/billboard rendering.
- Near/far representation transitions.
- Long persistence within memory budget.

### Ultra

- Higher trail resolution.
- Additional instability detail.
- Optional local GPU refinement.
- Longer visible range and improved lighting.
- Never changes formation physics solely to increase visual density.

## Adaptive degradation order

When budgets are exceeded, reduce cost in this order:

1. Reduce debug visualisation.
2. Reduce distant render detail.
3. Lower old-trail update frequency.
4. Compress old trail chains.
5. Reduce turbulence octaves for distant parcels.
6. Increase trail-node spacing away from the camera.
7. Disable optional volumetric refinement.
8. Shorten visual range while retaining physical state where affordable.
9. Retire optically insignificant, distant state.

Do not:

- Stall the main thread.
- Delete all effects abruptly.
- Change weather authority.
- Merge physically distinct trails merely to save draw calls.
- Make persistent state unbounded at Ultra.

## Telemetry requirements

Each frame or simulation tick records:

- Main-thread collection time.
- Simulation time by module.
- Render preparation time.
- GPU time by pass.
- Active parcel and filament counts.
- Spatial-cell counts.
- Allocated and used memory.
- Number and reason for LOD transitions.
- Worker overruns and dropped snapshot counts.
- Render-frame reuse count.
- Reset and numerical-fault counts.

## Performance acceptance tests

- 60-minute cruise with persistent contrails.
- Strong turbulence and wind shear.
- Repeated turns and climbs.
- 20 FPS host simulation.
- 120 FPS host simulation.
- Pause and resume.
- Time acceleration within supported policy.
- Aircraft reload ten times.
- Teleport/reposition ten times.
- Companion disconnect/reconnect.
- Renderer fallback after forced advanced-backend failure.

A milestone cannot pass only because the average frame looks acceptable; 95th and 99th percentile timings and memory growth must also pass.