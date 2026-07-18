# UI integration boundary in v0.4

The primary product UI is now the native Windows program in `app/main.cpp`.
It does not render inside X-Plane and therefore remains independent of Vulkan,
Metal, OpenGL, VR focus, and XPLM window input routing.

`src/Panel.cpp` is retained as the renderer-agnostic six-page ImGui panel source
for a possible future compact in-sim view. Both front ends share the same
`EffectSettings` schema. The companion writes `FFAtmo.ini`; the plugin polls it
and publishes `FFAtmo.status.ini` through `CompanionBridge`.

This split gives the standalone app full diagnostics and update ownership while
the plugin remains small and flight-loop focused.
