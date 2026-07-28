# LevelUp B738 v5.4 rejected visual experiment

Renderer Foundation v5.4 compiled and ran, but the in-sim visual result was rejected.

Observed in the supplied video:

- oversized opaque cotton-like billboard blobs
- collapsed near-field twin streams
- hard knots and abrupt density changes
- exaggerated closed loops that did not resemble real wake roll-up
- visible temporal reshaping as pooled slots were reassigned

Diagnostic report:

- 922 active parcels
- 2,586 generated render samples
- 1,024 selected and rendered billboard instances
- 1,562 samples rejected by the visible-capacity limit
- 12 stream breaks
- zero XPLM pool creation drops
- approximately 35.04 metres from exhaust to first visible condensation
- maximum planner time about 1.64 ms

Root design mistakes:

1. Billboard diameter was driven by planner section length as well as wake width, allowing sparse samples to become extremely large clouds.
2. Pool slots were assigned by selected-list position every frame rather than by persistent render ID, so cloud identity was not temporally stable.
3. Texture and instance alpha combined into excessive optical density.
4. Random axial and cross-section offsets amplified wake curvature before the cloud field was stable.

v5.4.1 recovery policy:

- persistent render-ID to XPLM-instance ownership
- physical wake-width sizing only
- 10-metre maximum puff diameter
- substantially lower texture and instance alpha
- reduced random offsets, especially in the near field
- passive wake-turn, descent and swirl-candidate diagnostics
- no additional visual swirl force until the stable cloud-field test passes
