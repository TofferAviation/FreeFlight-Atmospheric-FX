# Snapshot Recorder Controlled Test Plan

## Aircraft

Use the validated Boeing 737-800NG (`B738`) fixture.

## Duration

Target 15–20 minutes. The recording rate is 20 Hz.

## Before recording

1. Remove or disable the previous geometry-debug overlay unless it is needed.
2. Load the 737 completely and wait until the aircraft is stable.
3. Use a weather setup that includes visible wind and a cold cruise-level atmosphere.
4. Confirm `Plugins > FFAtmo Engine Recorder` is present.
5. Select **Start Recording**.

## Flight sequence

Use **Add Event Marker** before each major phase when practical.

1. **Ground idle — 60 seconds**
   - Engines running at idle.
   - Aircraft stationary.
   - Move flaps from zero to a take-off setting and back once.

2. **Take-off and climb — 3 to 5 minutes**
   - Advance from idle to high thrust.
   - Rotate and establish a stable climb.
   - Include at least one bank of approximately 20–30 degrees.

3. **Cold level segment — 3 minutes**
   - Level at a cold altitude available in the test setup.
   - Hold approximately constant speed and thrust for at least 60 seconds.
   - Make one coordinated left or right turn.

4. **Time and lifecycle checks — 2 minutes**
   - Pause for approximately 10 seconds, then resume.
   - Use 2× simulator time for approximately 20 seconds, then return to 1×.
   - Enter and exit X-Plane replay briefly when safe.

5. **Descent and low-power segment — 3 minutes**
   - Descend with reduced thrust.
   - Change flap or slat configuration during approach.
   - Include one moderate turn.

6. **Position discontinuity — once**
   - After the main flight data is captured, reposition the aircraft through X-Plane's map or flight setup, or reload the same aircraft.
   - Continue recording for at least 20 seconds afterward.

7. **Finalize**
   - Select **Stop and Finalize** before closing X-Plane.
   - Wait for the spoken confirmation or the finalization message in `Log.txt`.

## Files to return

From:

```text
X-Plane 12/Resources/plugins/FFAtmoEngineRecorder/
```

send:

```text
recordings/<timestamp>-B738.ffar
reports/<timestamp>-B738-dataref_validation.txt
reports/<timestamp>-B738-recording_summary.txt
```

Also send:

```text
X-Plane 12/Log.txt
```

## Acceptance checks

The summary should report:

- `status=OK`
- `clean_end_chunk=1`
- `snapshot_count` greater than zero
- `dropped_snapshot_count=0`

The DataRef report should be inspected for:

- unresolved required inputs;
- unexpected array lengths;
- non-finite samples;
- engine values that never update during thrust changes;
- pause, replay, and time-acceleration values that fail to change;
- weather values that remain implausibly constant during altitude changes.
