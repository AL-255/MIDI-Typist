# Per-key velocity and apply-all thresholds

The `huntsman` build provides GUI telemetry, normalized float velocity, per-key
bottom-out velocity windows and the gated interval pop filter. See
[MIDI design](MIDI_DESIGN.md) and [current validation](VALIDATION.md).

```sh
cmake --preset huntsman
cmake --build --preset huntsman
python3 tools/keyboard_gui.py        # auto-detects the 1532:02b0 CDC port
```

Use `build-keyboard-fn-menu/huntsman_firmware.bin` for an authorized
application-only flash. Use the matching GUI from this checkout.

## GUI

**Apply thresholds to all keys** uses the current press/release entry fields.
After confirmation, it sends a single `cfg all ID PRESS RELEASE` command.
Firmware validates the pair and sensor count before changing anything, updates
every active sensor in one main-loop operation, increments the configuration
revision once, and clears key/velocity state once. The GUI waits for the ACK
and verifies all threshold pairs in the returned snapshot. Invalid requests
change neither configuration nor capture state. Host thresholds and committed
Fn-menu choices save automatically after neutral and 250 ms without changes.

Key tiles show current raw readback and latest completed normalized velocity
(`v0.000`–`v1.000`). Selecting a key shows its velocity, completed-fit count and
release-armed/pending state. A prior result remains displayed while a later
capture is pending; the count identifies completed captures. Disable keyboard
output while tuning if desired: **velocity capture continues with HID disabled**.
The GUI never computes actual-device velocity from its decimated snapshots.

## Data structure and scan flow

The shared [raw engine](../firmware/app/src/keyboard_raw.c) owns one
`keyboard_velocity_t` per configured sensor slot: `MT_KEY_CAPACITY`,
65 in the Huntsman build and 128 by default for other ports.

| Storage | Purpose |
| --- | --- |
| Ten uint16 window samples + count | The current fit window, starting at the triggering sample |
| Collecting flag (telemetry: pending) | Set from the trigger until the window closes |
| Release-armed flag | A new press may register only after raw exceeds this key's release threshold |
| Float32 velocity + validity flag | Latest normalized filtered estimate for this key |
| uint32 completion counter | Number of completed fits, wrapping naturally |

The struct is 32 bytes on this target: 2080 bytes for all 65 sensors, no heap,
shared per-key capture buffer, or variable-size event queue. Normalization uses
the MCU's floating-point support. Calibration has separate hold registers.

For every valid full scan, each sensor is processed independently:

1. Rearm if `raw > release`. If its Schmitt state transitions up -> down
   (`raw < press`) and it is armed, start a new window at this triggering
   sample and disarm; an unfinished earlier window is discarded — the newest
   press always owns the window.
2. Otherwise, while a window is open, append the sample. Close the window
   when ten samples are collected or when a sample crosses below the shared
   **bottom-out threshold** of 1500 (that sample is excluded — except when the
   window holds nothing else, where it is kept so a trigger at the floor still
   yields a one-interval fit).
3. On close, calculate the fit and update that key's result/counter.
4. Independently deliver the ordinary HID down/up transition when keyboard
   output is armed. HID key-down is not delayed for velocity acquisition.

```text
scan          trigger    +1    +2   ...    +9   (ten samples, or cut at raw<1500)
collecting      set  ---------------------------- closed
fit samples      x0     x1     x2   ...    x9
```

A release before the window closes does not truncate it; it only re-arms the
key. A trigger at or below the bottom-out floor still produces a fit from the
trigger sample and the closing readback, so a deep MIDI trigger point (see
[MIDI design](MIDI_DESIGN.md#midi-trigger-point)) never leaves a press
unmeasured. Different keys never share sample history, arming state, pending
flags or output registers.

The calculation uses the same raw estimator as `press_velocity()` in
`tools/last_key_stream.py`, followed by MCU-side normalization:

```text
intervals = [x0-x1, x1-x2, ..., x(n-2)-x(n-1)]
if the window holds more than five samples:
    discard the interval furthest from median(intervals), earliest on ties
raw_velocity = d(x)/count * layout.sample_hz    (total drop / kept intervals)
velocity = clamp(raw_velocity / 4500000, 0, 1)
```

Shorter windows skip the median filter entirely, so their estimate is exactly
`d(x)/count`. Very fast presses typically fit on three to six readbacks.

The Huntsman descriptor declares **8000 Hz**; another board supplies its own
acquisition rate. The host capture tool retains its fixed Huntsman 8 kHz
assumption, so it matches the MCU only at that declared rate. Positive
raw velocity means decreasing readback (pressing); negative raw estimates
normalize to zero. Zero is a valid result. Fractional means are retained before
normalization; see [filter details](MIDI_FILTER.md).
These are not calibrated millimeters/second. MIDI maps the normalized float
to attack velocity 1–127. A descriptor's rate does not establish measured
hardware cadence. See [sampling contracts](PORTING.md#3-acquire-real-analog-samples).

Startup/invalid scans, USB reset and configuration/enable changes invalidate
results, cancel all open windows and require a new observed release for each
key. Completion counters are retained until application restart. No fit spans
an invalid scan or a configuration change. The existing global neutral guard
for HID output is separate from these independent per-key velocity gates.

MIDI waits for the strike's window to close before transmitting Note On.
A trigger at/below the floor still uses the next readback for a one-interval
estimate; short windows have less noise rejection. See [filter edge cases](MIDI_FILTER.md).

## Telemetry

The [GUI wire layout](TELEMETRY.md#gui-snapshot-stream-gui) defines velocity,
completion counters and ready/valid/pending bits. Firmware computes every
received valid scan; GUI telemetry is latest-only, not a velocity event log.
MIDI consumes estimates independently of GUI snapshots.

## Validation and image

See [Building](BUILDING.md) and [Validation](VALIDATION.md). Tests cover
concurrent slopes, equality/rearming, window cuts, rapid retriggers, invalid
input, atomic threshold edits, serialization and GUI ACK/readback.
