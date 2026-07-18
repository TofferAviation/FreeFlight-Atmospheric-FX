# FFAtmospherics v0.4 standalone product foundation

v0.4 turns the proven Lineage particle prototype into a standalone third-party
product consisting of two cooperating programs:

- `FFAtmo/64/win.xpl` remains the in-simulator effect and physics engine.
- `FFAtmo/FFAtmoCompanion.exe` is the native Windows control application.

The companion is built directly with Win32 and has no browser, Electron,
WebView, Dear ImGui, or separate runtime requirement. Its design implements the
six approved pages: Live Overview, Effects Control, Weather & Realism, Quality
& Performance, Aircraft Profile, and Advanced & Test.

## Live connection

The two programs communicate through X-Plane's preferences folder:

- `Output/preferences/FFAtmo.ini` contains user-controlled settings. The plugin
  detects and applies external changes within 250 ms.
- `Output/preferences/FFAtmo.status.ini` contains read-only plugin, aircraft,
  weather-decision, wake-lifecycle, and particle status. The companion refreshes
  it twice per second.

There is no Apply/restart guessing loop. Changing a companion control writes the
setting immediately. Enabling Preview Mode also enables the master effect switch.

## Telemetry providers

The built-in file bridge is always available and contains the FFAtmospherics
values the UI needs. `app/TelemetryProvider.h` defines a provider interface for
optional future sources. XUIPC can be added behind that interface for richer
simulator telemetry without making XUIPC a requirement for the base product.

## Product layout

```text
X-Plane 12/
  Resources/plugins/FFAtmo/
    FFAtmoCompanion.exe
    64/win.xpl
    assets/FFAtmo_Lineage.obj
    assets/FFAtmo_particles.pss
    assets/FFAtmo_particles.png
    profiles/Lineage1000.ini
```

The companion auto-detects X-Plane when launched from the installed FFAtmo
folder. It also provides a folder picker and remembers the selected root.

## Update boundary

The Advanced page reserves the stable/beta update channel UI, but network
updates remain disabled during development. Once V1 becomes the approved
GitHub baseline, the companion can check a signed manifest, show release notes,
and install while X-Plane is closed. The plugin never replaces its loaded DLL.

## Current scope

This is the first compilable standalone-app source, live bridge, diagnostics
feed, and updater-ready product structure. Visual calibration of the wing sheet,
counter-rotating vortex roll-up, wake density, and breakup remains the next
development track.
