# LevelUp B738 Renderer v5.0 startup crash

The first native-particle proof crashed X-Plane during startup before the user aircraft was delivered to the plugin.

Observed log sequence:

- FFAtmo Contrail Debug loaded successfully
- the plugin reported that the ACF was not available yet and waited for `XPLM_MSG_PLANE_LOADED`
- X-Plane crashed immediately afterwards during startup
- no particle-renderer object-loaded callback or ready message was reached

Root cause:

`ContrailParticleRenderer::start()` called `XPLMLoadObjectAsync()` from the early plugin-enable path. This requested OBJ/particle-system parsing while X-Plane's world/scenery loader was not yet ready.

v5.0.1 correction:

- startup only registers the four particle instance datarefs and records the asset path
- no object-loading API is called during plugin startup or enable
- empty renderer updates reset the startup gate and cannot load assets
- the renderer waits for non-empty live contrail samples on three consecutive flight-loop updates
- only then is the tiny particle OBJ loaded synchronously
- the two particle instances are created only after that deferred load succeeds
- the existing LevelUp geometry, nucleation model and wake physics remain unchanged

The safe-start source compiled successfully and passed the complete deterministic test suite. In-sim validation is split into two gates:

1. Confirm that X-Plane reaches the loaded LevelUp cockpit without crashing.
2. Only after startup passes, enter Forced Dry Preview and evaluate the ribbon appearance.
