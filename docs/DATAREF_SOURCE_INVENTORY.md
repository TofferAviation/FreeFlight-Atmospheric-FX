# X-Plane DataRef Source Inventory

Status: **Source-listed; runtime validation pending**  
Source snapshot: user-supplied `DataRefs.txt`, header dated 24 June 2026.

This document records the first catalogue pass for Atmospheric Engine v1. An entry being present here means its name, type, and documented unit were found in the supplied X-Plane DataRef list. It does **not** yet mean the value has been validated in a live simulator, on a supported X-Plane build, or with third-party aircraft.

The production `XPlaneHostAdapter` must treat every DataRef as read-only even when X-Plane marks it writable.

## Selection rules

1. Prefer direct values at the aircraft position over regional approximations.
2. Preserve the original DataRef value and provenance in recordings before converting to engine units.
3. Query array lengths through the SDK; do not assume the documented length is permanent.
4. Do not use sea-level relative humidity as local cruise-altitude humidity.
5. Aircraft geometry and effect source positions come from validated aircraft profiles unless a DataRef is explicitly approved as a fallback.
6. Deprecated or replacement-marked DataRefs are excluded from primary contracts.

## Time and lifecycle

| Engine semantic | Primary DataRef | Type | X-Plane unit | Proposed sampling | Status |
|---|---|---:|---|---|---|
| Host frame period | `sim/operation/misc/frame_rate_period` | float | seconds | Every snapshot | Source-listed |
| Simulator uptime | `sim/time/total_running_time_sec` | float | seconds | Every snapshot | Source-listed |
| Current-flight time | `sim/time/total_flight_time_sec` | float | seconds | Every snapshot | Source-listed |
| Paused | `sim/time/paused` | int | boolean | Every snapshot | Source-listed |
| Requested time scale | `sim/time/sim_speed` | int | ratio | Every snapshot | Source-listed |
| Achieved time scale | `sim/time/sim_speed_actual` | float | ratio | Every snapshot | Source-listed |
| Replay active | `sim/time/is_in_replay` | int | boolean | Every snapshot | Source-listed |

## Aircraft transform and motion

| Engine semantic | Primary DataRef | Type | X-Plane unit | Proposed sampling | Status |
|---|---|---:|---|---|---|
| Local position X | `sim/flightmodel/position/local_x` | double | metres | Every snapshot | Source-listed |
| Local position Y | `sim/flightmodel/position/local_y` | double | metres | Every snapshot | Source-listed |
| Local position Z | `sim/flightmodel/position/local_z` | double | metres | Every snapshot | Source-listed |
| Latitude | `sim/flightmodel/position/latitude` | double | degrees | Every snapshot | Source-listed |
| Longitude | `sim/flightmodel/position/longitude` | double | degrees | Every snapshot | Source-listed |
| Elevation MSL | `sim/flightmodel/position/elevation` | double | metres | Every snapshot | Source-listed |
| Local pitch | `sim/flightmodel/position/theta` | float | degrees | Every snapshot | Source-listed |
| Local roll | `sim/flightmodel/position/phi` | float | degrees | Every snapshot | Source-listed |
| Local true heading | `sim/flightmodel/position/psi` | float | degrees | Every snapshot | Source-listed |
| Earth-relative pitch | `sim/flightmodel/position/true_theta` | float | degrees | Every snapshot | Source-listed; role to verify |
| Earth-relative roll | `sim/flightmodel/position/true_phi` | float | degrees | Every snapshot | Source-listed; role to verify |
| Earth-relative heading | `sim/flightmodel/position/true_psi` | float | degrees | Every snapshot | Source-listed; role to verify |
| Orientation quaternion | `sim/flightmodel/position/q` | float[4] | quaternion | Every snapshot | Source-listed; ordering to verify |
| Local velocity X | `sim/flightmodel/position/local_vx` | float | m/s | Every snapshot | Source-listed |
| Local velocity Y | `sim/flightmodel/position/local_vy` | float | m/s | Every snapshot | Source-listed |
| Local velocity Z | `sim/flightmodel/position/local_vz` | float | m/s | Every snapshot | Source-listed |
| Local acceleration X | `sim/flightmodel/position/local_ax` | float | m/s² | Every snapshot | Source-listed |
| Local acceleration Y | `sim/flightmodel/position/local_ay` | float | m/s² | Every snapshot | Source-listed |
| Local acceleration Z | `sim/flightmodel/position/local_az` | float | m/s² | Every snapshot | Source-listed |
| Roll rate | `sim/flightmodel/position/Prad` | float | rad/s | Every snapshot | Source-listed |
| Pitch rate | `sim/flightmodel/position/Qrad` | float | rad/s | Every snapshot | Source-listed |
| Yaw rate | `sim/flightmodel/position/Rrad` | float | rad/s | Every snapshot | Source-listed |
| True airspeed | `sim/flightmodel/position/true_airspeed` | float | m/s | Every snapshot | Source-listed |
| Ground speed | `sim/flightmodel/position/groundspeed` | float | m/s | Every snapshot | Source-listed |
| Angle of attack | `sim/flightmodel/position/alpha` | float | degrees | Every snapshot | Source-listed |
| Sideslip angle | `sim/flightmodel/position/beta` | float | degrees | Every snapshot | Source-listed |
| Height AGL | `sim/flightmodel/position/y_agl` | float | metres | Every snapshot | Source-listed |
| Normal load factor | `sim/flightmodel/forces/g_nrml` | float | g | Every snapshot | Source-listed; sign to verify |

## Atmosphere at the aircraft

| Engine semantic | Primary DataRef | Type | X-Plane unit | Proposed sampling | Status |
|---|---|---:|---|---|---|
| Ambient temperature | `sim/weather/aircraft/temperature_ambient_deg_c` | float | °C | Every snapshot | Source-listed |
| Leading-edge temperature | `sim/weather/aircraft/temperature_leadingedge_deg_c` | float | °C | 10 Hz | Source-listed; future condensation use |
| Static/barometric pressure | `sim/weather/aircraft/barometer_current_pas` | float | Pa | Every snapshot | Source-listed |
| Air density | `sim/weather/rho` | float | kg/m³ | Every snapshot | Source-listed |
| Speed of sound | `sim/weather/aircraft/speed_sound_ms` | float | m/s | 10 Hz | Source-listed |
| Wind vector X | `sim/weather/aircraft/wind_now_x_msc` | float | m/s | Every snapshot | Source-listed |
| Wind vector Y | `sim/weather/aircraft/wind_now_y_msc` | float | m/s | Every snapshot | Source-listed |
| Wind vector Z | `sim/weather/aircraft/wind_now_z_msc` | float | m/s | Every snapshot | Source-listed |
| Effective wind speed | `sim/weather/aircraft/wind_now_speed_msc` | float | m/s | Every snapshot | Source-listed; validation cross-check |
| Effective wind direction | `sim/weather/aircraft/wind_now_direction_degt` | float | degrees true, from direction | Every snapshot | Source-listed; validation cross-check |
| Thermal vertical rate | `sim/weather/aircraft/thermal_rate_ms` | float | m/s | 10 Hz | Source-listed; meaning limits to verify |
| Rain on aircraft | `sim/weather/aircraft/precipitation_on_aircraft_ratio` | float | ratio 0–1 | 10 Hz | Source-listed |
| Snow on aircraft | `sim/weather/aircraft/snow_on_aircraft_ratio` | float | ratio 0–1 | 10 Hz | Source-listed |
| Hail on aircraft | `sim/weather/aircraft/hail_on_aircraft_ratio` | float | ratio 0–1 | 10 Hz | Source-listed |
| Gravity | `sim/weather/aircraft/gravity_mss` | float | m/s² | On location/planet change | Source-listed |

### Humidity decision

`sim/weather/aircraft/relative_humidity_sealevel_percent` is explicitly a **sea-level** value and is therefore not accepted as local humidity for contrail formation. The current candidate is to interpolate the temperature and dew-point profiles at aircraft altitude, then derive vapour pressure, relative humidity over water, and relative humidity over ice in the Environment Model.

The mapping between aircraft dew-point layers and their altitude array must be verified in the live simulator before this contract is frozen.

## Regional and layered atmosphere

| Engine semantic | Primary DataRef | Type | X-Plane unit | Proposed sampling | Status |
|---|---|---:|---|---|---|
| Atmospheric level altitudes | `sim/weather/region/atmosphere_alt_levels_m` | float[] | metres MSL | 2 Hz/on weather change | Source-listed |
| Wind-layer altitudes | `sim/weather/region/wind_altitude_msl_m` | float[] | metres MSL | 2 Hz/on weather change | Source-listed |
| Wind-layer speeds | `sim/weather/region/wind_speed_msc` | float[] | m/s | 2 Hz | Source-listed |
| Wind-layer directions | `sim/weather/region/wind_direction_degt` | float[] | degrees true, from direction | 2 Hz | Source-listed |
| Shear speeds | `sim/weather/region/shear_speed_msc` | float[] | m/s | 2 Hz | Source-listed |
| Shear directions | `sim/weather/region/shear_direction_degt` | float[] | degrees | 2 Hz | Source-listed |
| Turbulence profile | `sim/weather/region/turbulence` | float[] | simulator scale 0–10 | 2 Hz | Source-listed |
| Dew-point profile | `sim/weather/region/dewpoint_deg_c` | float[] | °C | 2 Hz | Source-listed |
| Temperature-layer altitudes | `sim/weather/region/temperature_altitude_msl_m` | float[] | metres MSL | 2 Hz | Source-listed |
| Temperature profile | `sim/weather/region/temperatures_aloft_deg_c` | float[] | °C | 2 Hz | Source-listed |
| Cloud type | `sim/weather/region/cloud_type` | float[] | blended enum | 2 Hz | Source-listed |
| Cloud coverage | `sim/weather/region/cloud_coverage_percent` | float[] | ratio 0–1 | 2 Hz | Source-listed |
| Cloud bases | `sim/weather/region/cloud_base_msl_m` | float[] | metres MSL | 2 Hz | Source-listed |
| Cloud tops | `sim/weather/region/cloud_tops_msl_m` | float[] | metres MSL | 2 Hz | Source-listed |
| Tropopause temperature | `sim/weather/region/tropo_temp_c` | float | °C | 2 Hz | Source-listed |
| Tropopause altitude | `sim/weather/region/tropo_alt_m` | float | metres | 2 Hz | Source-listed |
| Weather source | `sim/weather/region/weather_source` | int | enum | On change | Source-listed |

## Propulsion

| Engine semantic | Primary DataRef | Type | X-Plane unit | Proposed sampling | Status |
|---|---|---:|---|---|---|
| Engine count | `sim/aircraft/engine/acf_num_engines` | int | count | On aircraft load | Source-listed |
| Engine type | `sim/aircraft/prop/acf_en_type` | int[] | enum | On aircraft load | Source-listed |
| Engine running | `sim/flightmodel/engine/ENGN_running` | int[] | boolean | Every snapshot | Source-listed |
| N1 | `sim/flightmodel/engine/ENGN_N1_` | float[] | percent | Every snapshot | Source-listed |
| N2 | `sim/flightmodel/engine/ENGN_N2_` | float[] | percent | Every snapshot | Source-listed |
| Fuel flow | `sim/flightmodel/engine/ENGN_FF_` | float[] | kg/s | Every snapshot | Source-listed |
| Thrust | `sim/flightmodel/engine/POINT_thrust` | float[] | N | Every snapshot | Source-listed; engine/point mapping to verify |
| Effective throttle | `sim/flightmodel/engine/ENGN_thro_use` | float[] | ratio | Every snapshot | Source-listed |
| EGT | `sim/flightmodel2/engines/EGT_deg_cel` | float[] | °C | 10 Hz | Source-listed |
| ITT | `sim/flightmodel2/engines/ITT_deg_cel` | float[] | °C | 10 Hz | Source-listed |
| Jetwash velocity | `sim/flightmodel2/engines/jetwash_mtr_sec` | float[] | m/s | Every snapshot | Source-listed; future exhaust/ground interaction |
| Exhaust velocity | `sim/flightmodel2/engines/engn_exhaust_speed_msc` | float[] | m/s | Every snapshot | Source-listed; future exhaust module |

## Aircraft mass, configuration, and identity

| Engine semantic | Primary DataRef | Type | X-Plane unit | Proposed sampling | Status |
|---|---|---:|---|---|---|
| Total mass | `sim/flightmodel/weight/m_total` | float | kg | 10 Hz | Source-listed |
| Total fuel mass | `sim/flightmodel/weight/m_fuel_total` | float | kg | 2 Hz | Source-listed |
| Actual flap ratio | `sim/flightmodel/controls/flaprat` | float | ratio 0–1 | Every snapshot | Source-listed |
| Actual slat ratio | `sim/flightmodel/controls/slatrat` | float | ratio 0–1 | Every snapshot | Source-listed |
| Aircraft UI name | `sim/aircraft/view/acf_ui_name` | byte[] | string | On aircraft load | Source-listed |
| Aircraft ICAO | `sim/aircraft/view/acf_ICAO` | byte[] | string | On aircraft load | Source-listed |
| Aircraft relative path | `sim/aircraft/view/acf_relative_path` | byte[] | string | On aircraft load | Source-listed |

### Geometry decision

The supplied list does not expose a clean, authoritative own-aircraft wingspan and reference-wing-area pair suitable for wake physics. `sim/aircraft/parts/acf_semilen_JND` contains detailed wing-segment geometry but is not accepted as the primary product contract without extensive aircraft validation.

Version 1 therefore keeps wingspan, wing area, engine locations, wingtips, lifting surfaces, and rotors in the versioned aircraft profile. Geometry DataRefs may later act as profile-generation hints, never silent runtime authority.

## Immediate live-validation matrix

The first DataRef validation flight must record at least these cases:

1. Cold cruise at contrail altitude.
2. Warm low-altitude flight where contrails must not form.
3. Strong crosswind and wind-shear weather.
4. Turbulent weather.
5. Engine idle to high-thrust transition.
6. Climb, level flight, turn, descent, pause, replay, and time acceleration.
7. Aircraft reload and position teleport.

For every candidate, validation must capture:

- DataRef resolution success or failure.
- Runtime type and array length.
- Minimum, maximum, and non-finite counts.
- Update frequency and stale-value duration.
- Sign convention and coordinate convention.
- Agreement with an independent cockpit, weather, or derived cross-check.
- Behaviour on the X-Crafts Lineage 1000 and at least one default X-Plane aircraft.

## Open questions before contract freeze

1. Exact altitude mapping for `sim/weather/aircraft/dewpoint_deg_c`.
2. Whether regional weather profiles remain sufficient for persistent trail parcels kilometres behind the aircraft.
3. Quaternion component order and preferred orientation representation.
4. `POINT_thrust` indexing for aircraft with multiple thrust points per engine.
5. Reliability of generic engine DataRefs on deeply customised third-party aircraft.
6. Best direct source for spoiler/speedbrake state.
7. Minimum supported X-Plane 12 build and SDK version.

No unresolved item blocks recorder development, but unresolved values must be marked unavailable or estimated rather than silently trusted.