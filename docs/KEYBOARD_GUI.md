# Keyboard configuration GUI

The Linux/POSIX Tk GUI selects board geometry from the device's build target.
Live configuration supports Huntsman ANSI and the experimental M1's 82-key 75%
layout. Preview M1 without hardware using `--demo --board MG-M1V5TMR`.
The GUI edits thresholds/mappings, displays per-key velocity,
starts parallel calibration, exports JSON profiles and can explicitly flash
an application. ISO/JIS editing is rejected rather than mislabelling keys.

## Build and run

Install Python 3.10+, Tk and the pinned MIDI dependency:

```sh
python3 -m venv build-gui-venv
build-gui-venv/bin/pip install -r tools/requirements-gui.txt
build-gui-venv/bin/python tools/keyboard_gui.py
build-gui-venv/bin/python tools/keyboard_gui.py --demo
```

The GUI is the only supported PC application. Select the paired MIDI control
port; auto-detection works when exactly one board matches. The dropdown lists
multiple boards. `--device "PORT NAME"` also selects one explicitly. Linux
ALSA truncates long port names; the GUI recognizes the Huntsman's second
cable. Use the first, performance cable in the DAW. No serial node is exposed.

Only one GUI owner is supported; a fresh handshake replaces the previous
session. MIDI access does not normally need root on a desktop session.
Confirmed flashing uses raw USB and may request PolicyKit authorization.
M1 live telemetry and control-owner handoff are checked on Linux hardware;
Huntsman integration and other RtMidi backends have separate
[validation limits](VALIDATION.md).

An internal child process owns the open native MIDI ports. It runs only while
the GUI or its flashing worker needs a control connection; it is not a service
or another user application. Capture queues are bounded and overflow terminates
the capture. Native open/send failures do not cause automatic command retries.
Disconnect has a bounded shutdown window, then terminates and reaps a stuck
MIDI child before permitting another owner. It never terminates a flash worker
or resets the keyboard. Failure to release the owner blocks reconnect/flashing.
Host queue, polling and shutdown limits come from `defaults.h`.

Connect and select a key. The drawing uses recovered sensor identities and
the selected board's geometry (60% Huntsman or 75% M1).
Tiles show control-domain readings, down state and latest velocity; M1 readings
use per-key travel normalization. The panel shows
thresholds, mapping/control role, waveform, last submitted HID report,
calibration and storage status. Submission is not proof of host receipt.
Board guidance comes from the verified build target, not the key count. The M1
configuration page displays a recovery warning: reset/power cycling enters the
factory bootloader and erases the trial application and custom settings; reflashing
also erases custom saves. Its status distinguishes **written to flash** from
reboot persistence. Calibration and profile-apply confirmations repeat this limit.
Unsaved bounds are labelled **unsaved**, not factory calibration. M1 help lists
Fn transport/pairing/battery controls and marks custom-profile RESET unavailable.

On M1, the settings panel also shows estimated battery percentage, external or
battery power, low/critical warnings and the raw charger-pin state. Unknown,
stale and disconnected readings are explicit. Charger polarity is unverified,
so raw high/low must not be interpreted as charging/full. Power readback updates
once per second while configuration is idle and pauses during full-rate capture.
Boards without this capability display that power telemetry is unavailable.
See the [power-status contract](TELEMETRY.md#power-status).
The M1 display/readback and capture coexistence are physically checked over USB;
battery-percentage accuracy and charging behavior remain unverified.

Text uses the best family the platform's Tk build can really render: the
Windows system UI face, the macOS system face, or
Cantarell/Adwaita/Ubuntu/Noto/DejaVu on a normal Linux desktop, antialiased
and in point sizes. Tk does not substitute a missing family, so no widget
hard-codes a generic `sans` or `monospace` name, and the plot gutter is
measured from the resolved monospace face.

A Tk build without fontconfig (some conda packages) can only use X11 core
bitmap fonts, and a bitmap is crisp only at its native pixel size. The GUI
then switches worlds: Lucida for labels and titles, Terminus for numbers and
other fixed columns, each requested in exact pixels rather than letting Tk
scale a face it does not have. Antialiased text needs a Tk built against
fontconfig/Xft, which the distribution `python3` provides.
See [GUI access and troubleshooting](BUILDING.md#gui-access-and-troubleshooting).

The window opens at a comfortable screen size. The whole configuration page
scrolls when a large keyboard layout or wrapped status text exceeds the window;
the settings and footer remain reachable at 900×700. The bottom-left settings
panel has its own scrollbar for fields and shortcut help. Mouse-wheel events
belong to the innermost scroll region under the pointer; arrow keys and Page
Up/Down work when its canvas has focus. Use the page scrollbar to move between
the keyboard drawing, settings and footer.

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

For keyboard mode, select a physical tile, choose the **Keyboard** keycode
dropdown, then **Apply keycode**. It offers Disabled, keyboard/keypad usages
04…DF, and modifiers E0…E7; uncommon usages have hexadecimal labels. Host OS
support determines how a usage is interpreted. Consumer-page/media controls
and macros are not keyboard keycodes and are not offered.
Fn is locked. All Fn shortcuts/settings remain attached to their physical keys,
even if the base output is disabled or remapped. MIDI mappings and calibration
are independent. Duplicate destinations are supported: releasing one source
does not release an output still held by another. Edits release outputs and
wait for neutral. Check the device-confirmed mapping in the selected-key panel,
then wait for **settings saved** before unplugging.

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
staged data. Ordinary edits are disabled while collecting or waiting to save.
A latched storage fault disables new calibration; the status includes its error
code. A failed hardware resume can follow a successful flash write, so the error
message does not promise rollback to the previous saved record.
See [calibration](CALIBRATION.md).

## Flashing from the GUI

Open the **Device flashing** tab to identify the connected keyboard, choose
MIDI-Typist or a supplied Razer application, validate the image and review the
confirmation. It supports application and bootloader states, including custom
reflashing. See [Device flashing](DEVICE_FLASHING.md) for accepted files,
protected regions, permission requirements and restoration limits.
The **MonsGeek M1 V5 TMR (experimental)** option verifies the factory model
before offering a trial conversion, or checks USB-bound SysEx identity for custom
reflashing. Live 82-key telemetry and released-key 8 kHz acquisition are checked;
pressed-key performance and complete wireless/power operation remain unverified;
see [M1 flashing limits](DEVICE_FLASHING.md#monsgeek-m1-experimental-conversion).
The M1 status line identifies the selected USB/Bluetooth-slot/2.4 GHz keyboard
transport and whether it is ready, waiting for a host, switching, or pairing
requested/searching. Searching is not confirmation of a paired host. USB control
connection does not imply that keyboard output is routed to USB. Select the
transport with Fn+F1–F5; MIDI remains USB-only. Holding Fn+F1–F4 for three
seconds changes the preview to a pairing request, issued on release; see the
[M1 transport controls](MONSGEEK_M1.md#power-and-transport-components).
On an ordinary wireless host disconnect the GUI remains usable over USB. When
the host returns, release all keys before typing; offline key holds and knob
movement are discarded rather than replayed. Radio faults remain distinct from
waiting for a host and may require the documented recovery workflow.
If an experimental backend reports `Boot failed: …` or `Runtime failed: …` over SysEx, the GUI shows
the failure and leaves configuration disabled. This is not a connected keyboard
snapshot; recovery must use the flashing workflow supported by that backend.

M1 key readouts, thresholds and captures use normalized travel: 4096 released,
1 pressed. Calibration candidates retain electrical ADC+1 units. An unsaved
calibration flag can mean provisional startup bounds, not a failed settings save.

Initialize the updater submodule first:

```sh
git submodule update --init third_party/huntsman_updater
```

Raw USB requires root or suitable permissions. The GUI uses `pkexec` when
available; otherwise follow its access instructions. Nothing flashes without
confirmation, and tests/builds never flash. Compatible settings/calibration are
retained; firmware initializes missing/corrupt saves. The tab refreshes device identity after completion; reconnect configuration separately.
Razer primary settings/serial, bootloader, factory/security and ASIC firmware
are outside the application write path.

Backends may report **stored bounds (read-only)** without writable calibration
support. Calibration buttons stay disabled; this does not mean other edits were
saved. **Settings not confirmed saved** must not be treated as persistence.

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

**Hold first 20 pts of keystroke** switches control telemetry from GUI snapshots to a
pinned-sensor HKL1 stream: every acquired value of the selected sensor.
Keyboard/MIDI performance continues, but telemetry and editing pause.
Stream switching waits for queued configuration ACK/readbacks first.

A crossing below threshold becomes sample zero; the view holds twenty points
**including that trigger**. A new trigger replaces
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
Only version-4 profiles containing the exact board `target`, numeric `layout`,
and complete per-key thresholds/keyboard/MIDI mappings are accepted. Cross-board imports
and earlier profile formats are rejected before any commands are sent.

## MIDI SysEx protocol

The configuration status and flashing tab display the firmware's build-time Git
commit and clean/dirty state, alongside its project version and board target.
They come from the connected device, not the GUI checkout. See
[provenance commands](TELEMETRY.md#text-replies) for the read-only `git` query.

The GUI establishes a fresh session with HELLO/READY, receives the build
identity, then selects `stream gui`. Text and binary payloads have separate
SysEx message types and can coexist without corrupting one another.
Commands have one outstanding nonzero decimal ID; snapshots carry the latest
ACK/result. Malformed IDs receive no ACK. See [commands and JSON](MIDI_PROTOCOL.md).

The [wire layout](TELEMETRY.md#gui-snapshot-stream-gui) defines count-aware MTG4
latest-only snapshots (Huntsman: at most one per 33 ms). GUI gaps are expected.
After initial synchronization, framing/checksum errors fail the connection.
Pinned `stream key THRESHOLD SESSION SENSOR` uses 20-byte HKL1 records and
requires continuity; stream changes discard the previous unsent session.

## Validation

[Building](BUILDING.md) documents native, MIDI mock and real-Tk tests. They cover
geometry, atomic edits, readback/ACK, profile failures, capture isolation,
overflow, timeouts, resolved typography, settings-panel scrolling and flash
confirmation without opening hardware.
See [physical evidence and limitations](VALIDATION.md).
