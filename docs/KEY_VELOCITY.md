# Per-key velocity and apply-all thresholds

The `huntsman` build provides HKG6, normalized float velocity, per-key
bottom-out velocity windows and the gated interval pop filter. See
[MIDI design](MIDI_DESIGN.md) and [current validation](CALIBRATION.md#validation-status).

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
change neither configuration nor capture state. Settings remain RAM-only.

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

MIDI buffers each new note and emits the Note On when its key's window closes,
so the attack velocity is the completed estimate of that press; a window that
closes without a fit still releases the buffered note with the last value.
Press thresholds below the bottom-out 1500 make every press bottom out at the
trigger, so no fit can complete — keep press thresholds above 1500 for
meaningful velocity.

## Telemetry

`stream gui` sends **HKG6**, version 6, 1152 bytes, at most once per 33 ms.
Bytes 7..446 contain raw samples, thresholds and keyboard state; see
[the full layout](MIDI_PROTOCOL.md#hkg6-telemetry).

| Offset | Field |
| --- | --- |
| 0 | `HKG6` |
| 4 | uint16 length = 1152 |
| 6 | Version = 6 |
| 447 | 65 float32 normalized velocities |
| 707 | 65 uint32 completed-fit counters |
| 967 | 65 state bytes: release-armed=1, result-valid=2, pending velocity=4, calibration hold=8 |
| 1032 | MIDI and calibration fields; see [full protocol](MIDI_PROTOCOL.md) |
| 1148 | uint32 sum of the preceding 574 little-endian uint16 words |

Unused sensor slots are zero. Values are little-endian IEEE-754 float32.
The stable USB-owned buffer is 1152 bytes, separate from the latest unsent
snapshot. Whole/compact streaming retains its existing 640-byte batching limits
and existing semantics. Dedicated USB SRAM allocation is unchanged.

Velocity computation processes every received valid scan, not every GUI frame.
**GUI telemetry is latest-only**, not a lossless velocity event log: several
captures of a key between snapshots increment its counter, but only the latest
result is displayed. MIDI performance separately consumes completed estimates
for Note On events; GUI frames are not the MIDI event source.

## Validation and image

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
cmake --build --preset huntsman --target audit-lighting
```

Coverage: 65 simultaneous different slopes; equality/release gating; bottom-out
window cuts; newest-press window ownership under rapid retriggers; 16,640
randomized per-key observations checked against a full-history oracle;
invalid/config cancellation; velocity with HID disabled; actual ARM DMA ->
estimates -> HKG6 readback and MIDI Note On velocity; atomic all-key command and
invalid-request rejection; FS/HS larger-frame transport and pending-buffer
immutability; legacy decoder compatibility; GUI button/ACK/readback tests via
PTY and a private virtual display; USB/updater/lighting/stream regressions.

These tests use synthetic hardware responses and do not establish physical
press behavior or new-image stability on the board.
