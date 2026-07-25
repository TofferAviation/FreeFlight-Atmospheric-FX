# LevelUp B738 v5.3 accepted billboard baseline

The v5.3 in-sim run established the first accepted parcel-driven billboard renderer.

Observed:

- grey/white billboard particles rendered correctly instead of the black Ribbon path
- two LevelUp CFM56 exhaust streams were present
- wake curvature and looping behaviour were visible
- the simulator remained stable

Diagnostics:

- 612 active parcels
- 1,536 generated render samples
- 768 selected and rendered billboard instances
- 768 samples rejected only because the renderer pool was full
- zero stream breaks
- zero renderer pool creation drops
- approximately 31.33 metres from exhaust to first visible condensation
- maximum planner time below 0.66 ms

v5.4 follows this accepted architecture and focuses on fuller cloud volume, overlap, reduced repetition and improved far-wake detail.
