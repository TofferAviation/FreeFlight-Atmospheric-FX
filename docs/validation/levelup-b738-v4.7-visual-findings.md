# LevelUp B738 v4.7 visual findings

The v4.7 in-sim run loaded and executed correctly but failed visual acceptance.

Observed:

- the overlay and report correctly identified Renderer v4.7
- both LevelUp CFM56 streams were present and the wake deformation remained active
- cards rendered as dim brown/green terrain-coloured ropes rather than white ice cloud
- card boundaries and tube-like segmentation remained visible
- the material contribution was too weak for daylight rendering

Diagnostics:

- world renderer ready with all eight assets loaded
- 1,341 samples generated and 1,024 selected
- 317 samples rejected by the global visible-capacity limit
- zero stream breaks, zero continuity trims and zero renderer pool drops
- condensation onset remained approximately 0.236 seconds after exhaust
- first visible segment remained about 35.2 metres behind the exhaust

v4.8 correction:

- alpha buckets raised from 2.8-10 percent to 14-38 percent
- controlled RGB-only LIT luminance raised from 85 to 2,500 nits
- longitudinal cloud density made periodic with no end fade
- visible capacity raised from 1,024 to 1,536 sections
- per-asset pools raised from 192 to 256 instances
- cooling, nucleation, LevelUp geometry and Wake Fluid Simulation v1 unchanged
