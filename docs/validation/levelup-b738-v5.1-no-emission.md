# LevelUp B738 Renderer v5.1 no-emission finding

The v5.1 in-sim run remained stable but produced no visible contrails.

## Runtime evidence

- `world_renderer_ready=1`
- one native particle object loaded
- two plugin-managed instances active
- visuals and simulation enabled
- forced dry preview gate open
- formation potential at 1.0
- 933 active wake parcels and 956 emitted parcels
- zero renderer pool drops

The X-Plane log also confirmed that the v5.1.1 particle OBJ loaded after the safe-start gate. No particle-object load error was reported.

## Conclusion

The regression is isolated to the v5.1 particle asset definition. The mixed two-cell asset combined a ribbon particle and billboard particle as two sub-emitters under the same streaming emitter. X-Plane accepted and instantiated the object but produced no visible particles in X-Plane 12.4.3.

## v5.1.2 correction

- retain the proven delayed safe-start object loading
- return to one `BILLBOARD_MODE RIBBON` particle
- return to one streaming sub-emitter
- return to one 256 x 256 texture cell
- retain the wider cool-white v5.1 ribbon density and brighter lighting curves
- defer the billboard halo until it can be tested as a separate emitter/instance
- leave LevelUp exhaust geometry, cooling, nucleation and Wake Fluid Simulation v1 unchanged
