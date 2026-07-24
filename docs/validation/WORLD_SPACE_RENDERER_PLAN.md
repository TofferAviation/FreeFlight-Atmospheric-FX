# World-space Contrail Renderer Plan

- Use XPLM object instances rather than window-phase parcel discs.
- Draw depth-aware alpha-textured cloud puffs in local world coordinates.
- Pool instances and update them only from the flight loop.
- Add airborne and minimum-airspeed gating to forced preview modes.
- Add deterministic wake-roll-up displacement before production calibration.
- Keep a small status overlay but remove parcel connector lines.
