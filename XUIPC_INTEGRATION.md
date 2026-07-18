# Optional XUIPC integration

The v0.4 companion uses `FileBridgeTelemetry` by default. This provider reads
FFAtmo's own status feed and therefore works for every installation.

`app/TelemetryProvider.h` intentionally separates the UI from its telemetry
source. A later XUIPC adapter should implement `TelemetryProvider`, load the
XUIPC runtime dynamically, and return unavailable when it is not installed.
The provider selector can then prefer XUIPC for additional simulator values and
fall back to `FileBridgeTelemetry` for FFAtmo-specific wake and particle state.

No XUIPC SDK files or redistributables are included in this source package.
They should only be added after confirming the API version, redistribution
terms, and the exact data items required by the V1 UI.
