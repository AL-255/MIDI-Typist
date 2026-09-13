# Keyboard configuration GUI

The Linux/POSIX Tk GUI targets the Huntsman ANSI layout and the complete
`huntsman` firmware. It edits thresholds/mappings, displays per-key velocity,
starts parallel calibration, exports JSON profiles and can explicitly flash
an application. ISO/JIS editing is rejected rather than mislabelling keys.

## Build and run

Install Python 3.10+ and Tk; no pip packages are needed for the GUI itself.
Build/install firmware using [Building](BUILDING.md), then run:

```sh
python3 tools/keyboard_gui.py
python3 tools/keyboard_gui.py --device /dev/ttyACM0
python3 tools/keyboard_gui.py --demo
```

Auto-detection chooses the first CDC port under USB `1532:02b0`. Use an
explicit path when several boards are connected. Your user needs serial-device
access. Close other readers: one owner controls the CDC stream.
Normal configuration does not need root; confirmed flashing may use PolicyKit.

Connect and select a key. The drawing uses recovered sensor identities and
60% key geometry, with Fn immediately right of Space and Right Alt next.
Tiles show raw values, down state and latest velocity. The panel shows
thresholds, mapping/control role, waveform, last submitted HID report,
calibration and storage status. Submission is not proof of host receipt.

## Key behavior

- Defaults: press 3500 / release 3600. Valid pairs are
  `1 ≤ press < release ≤ 4095`; readbacks can reach 4096.
- Key-down is `raw < press`; key-up is `raw > release`. Equality retains state.
- Fresh neutral input is required after boot, settings changes, enable,
  USB reset or invalid/stale scans. Every sensor must exceed its release value.
- Scanning and standalone HID/MIDI do not require the GUI to remain open.
  An optical fault releases output; a lighting-only fault does not stop scanning.
- Keyboard-mode Right Alt/Menu/Right Ctrl/Right Shift send Left/Down/Right/Up.
  [Fn shortcuts](FN_MENU.md#keyboard-shortcuts) use green hints.
- Fn, Left Ctrl/Windows/Alt, Right Alt/Ctrl and Space are reserved MIDI controls.
  Other keys accept note numbers 0…127, names (including flats), or Off.

Disable output while tuning if desired, enter values and apply to one key or
confirm **Apply thresholds to all keys**. The MCU all-key operation is atomic.
The GUI verifies matching request ID, success result and readback. Stale,
rejected or timed-out operations are not silently retried.

Per-key velocity is calculated in firmware and continues while HID is disabled.
Telemetry displays the last completed normalized value and count, not every
strike between snapshots. See [velocity registers](KEY_VELOCITY.md).

Jankó shows effective note captions without altering stored mappings. It
bypasses lower-row mute; root/scale and octave still apply. Otherwise the GUI
shows assigned notes, which may be muted/filtered on the device.
Wheels use fixed 3800…1000 endpoints. Space uses its editable Schmitt pair.

**Calibrate keys → device flash** starts the keyboard-only routine. Release all
keys for 500 ms, then fully hold blue keys for one second; parallel holds are
amber and completed keys green. Completion saves; cancellation/timeout discards
staged data. Ordinary edits are disabled while collecting. See [calibration](CALIBRATION.md).

## Flashing from the GUI

**Flash application** stops the connection, validates a 131072-byte image,
shows its SHA-256 and asks for confirmation. It then calls the bundled
application-only updater through `tools/firmware_flasher.py`. Validation,
digest and upload use one immutable copy; a changed image is rejected.

Initialize the updater submodule first:

```sh
git submodule update --init third_party/huntsman_updater
```

Raw USB requires root or suitable permissions. The GUI uses `pkexec` when
available; otherwise follow its access instructions. Nothing flashes without
confirmation, and tests/builds never flash. Compatible settings/calibration are
retained; firmware initializes missing/corrupt saves. Reconnect after completion.
Razer primary settings/serial, bootloader, factory/security and ASIC firmware
are outside the application write path.

## Fn+Tab and Fn+V settings

**Trigger point** mirrors MIDI Fn+Tab: ten raw levels from 1500 to 3599.
The GUI sends individual per-key commands, keeping each release threshold and
checking every result; this batch is not the atomic MCU `cfg all` operation.
The selected-key panel also shows the nearest trigger level.

**Velocity start** mirrors Fn+V via `cfg velocity`, accepted in either
performance mode. Level 1 preserves measured velocity; level 10 forces maximum.
The GUI cannot switch keyboard/MIDI mode; use Fn+Enter.

Both changes save automatically. Keyboard-mode Fn+Tab instead converts
calibrated travel levels to raw pairs; committing replaces custom pairs.
Fn+Caps editor compatibility does not enable raw rapid-trigger operation.

## Keystroke hold mode

**Hold first 20 pts of keystroke** switches CDC from GUI snapshots to a
pinned-sensor HKL1 stream: every acquired value of the selected sensor.
Keyboard/MIDI performance continues, but telemetry and editing pause.
Stream switching waits for queued configuration ACK/readbacks first.

A crossing below threshold becomes sample zero; the view holds twenty points
**including that trigger**, unlike the CLI's next twenty. A new trigger replaces
the previous capture; changing sensor clears it. An early release does not
truncate the capture. The plot shows raw ticks, threshold lines, trigger and
the velocity fit.

The host reproduces the same up-to-ten-sample bottom-out window and filter,
using the fixed 8000 Hz assumption, to draw its fitted line and attributed
velocity. The measured arrival rate is shown separately. Ordinary GUI velocity
still comes directly from firmware; `--demo` uses simulated snapshots and
does not establish real capture timing.

The 16384-sample host buffer fails on overflow. Sequence/checksum failures also
end capture; no samples from different keys or missing intervals are joined.
Toggle capture off to restore GUI telemetry. See [HKL1](LAST_KEY_STREAM.md).

## Profiles and persistence

Committed per-key thresholds, mappings, output enable and Fn-menu settings save
with calibration in the two custom tail pages. Release all keys, leave no menu
open, and allow 250 ms without changes. Status shows **pending**, **saved** or
**failure**. A configuration ACK proves RAM application, not durability.

Fn+R/explicit `cfg clean` deletes custom settings/calibration. Compatible updates
keep them; missing/corrupt saves initialize defaults. See [storage](DEVICE_CONFIG_STORAGE.md).

Host JSON exports thresholds and mappings, not calibration or all menu settings.
Import validates the entire file before sending commands, temporarily disables
output and checks each edit. The batch is not atomic: a failure can leave
already-confirmed changes and disabled output. Inspect and retry deliberately.
Version-1 imports leave MIDI mappings unchanged; version 2 includes them.

## CDC protocol

The GUI first stops any old stream and queries `version`, then selects
`stream gui`. Text replies cannot interleave with binary output.
Commands have one outstanding nonzero decimal ID; snapshots carry the latest
ACK/result. Malformed IDs receive no ACK. See [commands and JSON](MIDI_PROTOCOL.md).

The [wire layout](TELEMETRY.md#gui-snapshot-stream-gui) defines 1152-byte
latest-only snapshots, at most one per 33 ms. GUI gaps are expected.
After initial synchronization, framing/checksum errors fail the connection.
Pinned `stream key THRESHOLD SESSION SENSOR` uses 20-byte HKL1 records and
requires continuity; stream changes discard the previous unsent session.

## Validation

[Building](BUILDING.md) documents native, PTY and real-Tk tests. They cover
geometry, atomic edits, readback/ACK, profile failures, capture isolation,
overflow, timeouts and flash confirmation without opening hardware.
See [physical evidence and limitations](VALIDATION.md).
