# Standalone keyboard and configuration GUI

The `huntsman` firmware provides GUI telemetry:
standalone Schmitt keyboard, MIDI, normalized per-key velocity and parallel
calibration. This GUI is written against the current 1152-byte layout, has no
version-gated controls and shows the connected build identity in its status
line.
See [current validation](CALIBRATION.md#validation-status).
The GUI never flashes the application or enters the bootloader; completing
calibration saves endpoints to the two authorized tail pages.
It is a Huntsman ANSI host tool, not automatic layout discovery for arbitrary
MIDI-Typist ports. The shared command parser is portable; physical drawings and
GUI telemetry framing requires matching board support. See [host integration](PORTING.md#5-add-lighting-storage-and-host-integration).

## Build and run

From the repository root, using the existing pinned NXP SDK/toolchain setup:

```sh
cmake --preset huntsman
cmake --build --preset huntsman
python3 tools/keyboard_gui.py                # auto-detects the CDC port
python3 tools/keyboard_gui.py --device /dev/ttyACM1   # explicit node override
```

Current application: `build-keyboard-fn-menu/huntsman_firmware.bin`, exactly 131072 bytes,
linked at `0x20000000`, sha256
`b12fda5194db2ea42cf891c898c618b903e4b56eb9534659df095e659d31394d`
(flashed with the sibling updater's application-only path and verified live:
GUI telemetry, pinned-sensor stream at ~1.35 k samples/s, stream switch-back,
bottom-out velocity windows at the shared 1500 threshold, and a physical
Fn+J Jankó toggle reported through the new telemetry bit). Original
bootloader/update transport is unchanged.

The Linux GUI uses Python's standard library and Tk (`python3-tk` must be
installed). No pip packages are required. Your user needs access to the CDC
device. Close the decoder, serial terminals and other CDC readers first: the
GUI owns the stream while connected. It does not request root privileges.
`python3 tools/keyboard_gui.py --demo` previews the UI without device access.

By default the GUI auto-detects the keyboard: it scans `/sys/class/tty/ttyACM*`
and walks each port's USB ancestry until it finds `idVendor`/`idProduct`; the
first port whose USB device is `1532:02b0` (the Huntsman V3 Pro Mini
application descriptor) and whose `/dev` node exists is selected. Detection
also runs when clicking **Detect** and whenever **Connect** is pressed with an
empty or `auto` device field. With several matching boards connected,
detection picks the first port in name order — use `--device` or the device
field to choose explicitly. If no `1532:02b0` CDC port is found (wrong cable,
missing udev permissions or `/dev` node, bootloader mode), the GUI reports it
and the field stays available for a manual path.

Click **Connect**, then select a drawn key. The diagram uses the recovered
61-sensor ANSI mapping and standard 60% key positions/sizes, including the
6.25-unit spacebar. Fn is immediately right of Space, followed by right Alt;
their sensor IDs and threshold associations stay attached to
their respective keys. Each key shows its latest raw value; orange means its
sensor is in the down state. The selected-key panel shows thresholds read back
from the device, a recent-value plot and the last USB-submitted NKRO report.
An application submission is not proof that the computer received a report.
The GUI explicitly rejects ISO/JIS profile editing rather than mislabeling keys.

Use **Disable keyboard** while tuning to avoid typing into the GUI or another
application. Enter both thresholds and click **Apply to selected key**.
Confirmation requires a matching command ID, success result and matching
threshold readback. Stale/disconnected telemetry disables editing. The GUI
never silently retries a timed-out/rejected command.

## Key behavior

- Every valid scan updates every sensor independently; GUI frame rate does
  not control keyboard scanning or HID processing.
- Up -> down when `raw < press`; down -> up when `raw > release`.
  Equality and the interval between thresholds retain the previous state.
- Default for every sensor: **press 3500, release 3600**.
- Allowed configuration: `1 <= press < release <= 4095`. Readbacks themselves
  may reach 4096. A release threshold of 4096 could never be exceeded.
- Down/up transitions use the recovered physical-key/action maps and existing
  16-byte NKRO keyboard report, including modifiers and keyboard FN actions.
  Right Alt/Menu/Right Ctrl/Right Shift send Left/Down/Right/Up; captions retain
  physical names. See [Fn keyboard shortcuts](FN_MENU.md#keyboard-shortcuts)
  for the green-hinted function and navigation layer. MIDI mappings are separate.
  Multiple keys can remain down together. Host key repeat is controlled by
  the operating system, not additional firmware down edges.
- Scanning starts automatically after USB configuration. Keyboard reporting
  is enabled by default and **does not require CDC or the GUI to be open**.
- Startup, re-enable, USB reset, invalid/stale scans and threshold changes
  clear host key state and require a fresh valid frame with **all** sensors
  strictly above their release thresholds before reporting resumes. Settings
  above an idle sensor's value can prevent arming; lower that threshold in
  the GUI. Release values must be chosen with sufficient idle margin.
- An optical transport fault releases keys and does not retry initialization
  or cycle GPIOs. A lighting-only fault does not disable a healthy scanner or
  keyboard. Busy HID transfers retry the current report on subsequent service
  calls; this is not a lossless transition-recording protocol.

The FN+Tab/FN+Caps modal editors use the shared hold-preview/release-entry menu,
then retain the original editor event behavior. Escape exits; releasing FN alone does
not exit. A dirty **actuation** commit now converts the original normalized
press/release thresholds through each sensor's calibration into the raw
Schmitt pairs shown in the GUI. This replaces per-key custom pairs, takes effect
atomically and requires neutral before reporting resumes. Rapid-trigger editor
compatibility does not enable rapid-trigger behavior in the raw Schmitt engine.
The GUI reports editor mode; ordinary edits are rejected while editing, while
Disable keyboard cancels the pending edit. See [Fn menu](FN_MENU.md).
Consumer/media/profile actions outside the keyboard HID remain unsupported.

Fn+Enter previews the target mode and toggles on release. Fn+R previews RESET,
then opens RESET? on release. Release all keys, then press green Y to clear
saved calibration or red N to cancel. Confirmed clearing restores application
defaults once all keys are neutral. The GUI observes calibration generation 0
afterward; simply opening or cancelling confirmation does not change storage.
MIDI mapping controls use note names or
numbers; Fn, Left Ctrl/Windows/Alt, Right Alt/Ctrl and Space are reserved controls.
While the built-in Jankó layout is active (Fn+J in MIDI mode) the status line
adds `JANKÓ layout (Fn+J)` and every key caption shows the layout's note
instead of the configured mapping; the mapping itself is unchanged and returns
as soon as the layout is switched off. The flags byte's bit 6 carries this
state in telemetry.
The GUI labels Right Alt/Ctrl octave −/+, Left Ctrl/Alt bend −/+ and Left
Windows modulation, and Space sustain. Space uses its editable Schmitt pair
to send CC64 127/0. Wheels use fixed 3800…1000 endpoints, not GUI Schmitt
thresholds or calibration bounds. A host profile assigning notes to a reserved
control is rejected before applying anything; existing files are not rewritten.
Per-key velocity is a
firmware-calculated 0–1 float, with an [interval pop filter](MIDI_FILTER.md).
**Apply thresholds to all keys** sends one atomic MCU update. The nominal
8 kHz velocity assumption is not a measured acquisition rate.

**Calibrate keys → device flash** starts the same routine as Fn+C in keyboard
mode. Release all keys for the 500 ms rest capture, then fully hold one or more
blue keys for one second. Each active hold is amber; completed keys are green.
The GUI shows progress, the number being held, inactivity time, saved generation
and errors. It disables ordinary edits during calibration; Cancel remains
available. Completion of all keys saves, while cancellation/5 s inactivity
discards staged results. See [calibration](CALIBRATION.md) for details.

## Flashing from the GUI

**Flash application...** writes a built image through the vendored updater
submodule (`third_party/huntsman_updater`) using its application-only DFU path.
The device is released to the flasher first, so an active connection is stopped;
the button then shows programming progress and the cold-boot result. The
confirmation lists the image, its size and sha256, states that only the
application region is written (bootloader, factory/security data, primary
settings and serial-number storage are never touched) and that the device is
cleared afterwards. Nothing flashes without that confirmation, and neither the
build nor any test flashes implicitly.

Flashing needs raw USB access. The GUI elevates just the flash through
`pkexec` when it is not already root; without PolicyKit it asks for the GUI to
be started with `sudo` or for suitable udev rules. `tools/flash_application.py`
performs the identical steps from a terminal, and both share
`tools/firmware_flasher.py`, which is exercised offline (image validation,
submodule lookup, options, progress and status reporting) with the updater call
replaced.

## Fn+Tab and Fn+V settings

The GUI mirrors the two on-device MIDI settings with ordinary commands, so the
device needs no extra state:

- **Trigger point** selects one Fn+Tab level (1 = bottom-out floor 1500 … 0 =
  release − 1, 3599) and applies it as 61 individual `cfg set` commands. Each
  key keeps its release threshold, and every readback is checked.
- **Velocity start** sets the Fn+V level through `cfg velocity` and reads the
  applied value back from telemetry offset 6. The device accepts it in either
  performance mode: the GUI cannot toggle MIDI mode, which Fn+Enter does.

The velocity start is stored with the other Fn-menu settings, so it survives a
power cycle just like the on-device Fn+V choice. Per-key trigger writes stay in
RAM: only the keyboard's own Fn+Tab step is stored. The per-key detail panel also names the Fn+Tab level nearest
to the selected key's press threshold, so a hand-edited pair still shows where
it sits on the bar, and the status line carries the connected build identity
reported by `version` (for example `build v0.1.0-RZ03-0499`).

## Keystroke hold mode

Checking **Hold first 20 pts of keystroke** above the bottom-right plot switches
it from the scrolling recent-value waveform to a frozen per-keystroke capture,
for tuning the velocity sensitivity curve. Engaging the mode switches the
device CDC stream from GUI telemetry to the per-key **HKL1 stream**, which
carries the selected sensor's raw value on **every optical scan frame** — the
fastest rate the keyboard produces. On this hardware that measures about
1.35 k samples/s (the GUI shows the measured rate, e.g.
`KEYSTROKE CAPTURE 1,453 samples/s`); the nominal 8 kHz figure remains only the
firmware's velocity assumption, not the acquisition rate. While the mode is
active the device serves one stream at a time, so the keyboard drawing and
status telemetry pause, and configuration buttons disable; typing and MIDI are
unaffected. Toggling the mode off (or changing the selected key) re-arms the
stream for the new key or returns to GUI telemetry.

In this mode the plot updates only when the selected key is triggered: a down
edge (raw crossing below the press threshold, the stream threshold) becomes
sample 0, and the following full-rate samples fill the capture until 20 points
are held. A vertical axis on the left carries raw-value ticks (0–4000) with
gridlines; the press/release reference lines are labeled at the right edge.
The orange dot marks the triggering sample and the bottom axis numbers the 20
sample slots. Releasing the key does not truncate the capture, and a new down
edge always restarts it — the latest keystroke wins. Changing the selected key
or toggling the mode clears the capture.

Because the capture is full-rate, the GUI reproduces the firmware's velocity
window exactly: the triggering point plus the following readbacks, cut before
the first sample below the shared bottom-out threshold of 1500 (ten maximum),
total drop divided by the interval count, with the median interval filter only
when more than five samples were collected. A dashed green **fitted line**
anchored at the trigger point marks the measured velocity as a straight slant
across the fitted window (counts/s converted back to raw counts per sample at
the assumed 8 kHz). The status shows both the raw result and its 0–1
normalization, e.g. `velocity 0.0889 [0–1] (400,000 counts/s; assumed
8 kHz)`, so each held raw fall can be compared with the velocity value the
device reports for the same keystroke. The plot holds its points and the
attributed velocity until the next trigger. Requires the firmware with the
pinned-sensor `stream key` argument; in `--demo` mode (no device) the capture
falls back to the 33 ms telemetry frames with device-fit attribution instead.

## Profiles and persistence

Thresholds, MIDI mappings and keyboard enable state edited over CDC are
**RAM-only**. Closing the GUI leaves them active; unplugging/restarting
restores the keyboard's own choices. Save/load JSON profiles on the computer.
Nothing writes bootloader, factory calibration, ASIC firmware or unreviewed
flash storage. Calibration endpoints and the Fn-menu settings (trigger level
and source, MIDI trigger step, velocity start, Jankó layout, lower-row mute,
brightness, root/scale, octave, performance mode) persist on-device in the two
documented unused tail pages; see [device storage](DEVICE_CONFIG_STORAGE.md).
Host JSON profiles do not contain calibration, performance mode or octave.
They also do not contain Fn+Left Shift's MIDI lower-row mute. The mute survives
mode switches and a power cycle and leaves the displayed mappings intact. A Caps/Shift
row key can show an assigned note in the GUI yet be muted by Fn+Left Shift; hold the
combo to preview `LOWER-ON`, then release to restore it. `menu status` reports
the flag over text CDC; the GUI telemetry format is unchanged.
Fn+E/Fn+S also select MIDI root/scale filters (stored, cleared by RESET). These are not GUI
mapping edits or JSON fields. Assigned notes can be silent/dark because of
the current filter; the GUI still shows their assignments and raw down state.
Use `menu status` for root/scale names, or T in the Fn+S menu for chromatic.

Load validates the entire ANSI profile before sending anything, disables
keyboard output, applies all 61 pairs and any version-2 MIDI mappings with individual readback, then restores
the preceding enable state. This is not an atomic transaction: a failure
cancels remaining commands, leaving confirmed changes in place and normally
leaving keyboard output disabled. Reconnect, inspect and load again explicitly.

## CDC protocol

Commands are newline-delimited ASCII; all arguments are decimal:

```text
version
stream gui
stream key THRESHOLD [SESSION [SENSOR]]
cfg get ID
cfg set ID SENSOR PRESS RELEASE
cfg all ID PRESS RELEASE
cfg midi ID SENSOR NOTE
cfg velocity ID LEVEL
cfg calibrate ID
cfg calcancel ID
cfg enable ID 0
cfg enable ID 1
```

`ID` is a nonzero uint32; `SENSOR` is the raw sensor index (0..60 for ANSI).
`version` is the one console query the GUI sends: it answers the build identity
(`build=v0.1.0-RZ03-0499`) before the binary stream starts, because text replies
and telemetry cannot share the CDC endpoint.
Well-formed IDs receive result 1 (accepted) or 2 (rejected) in telemetry.
Malformed/unparseable IDs receive no acknowledgment. Hosts must serialize
commands and wait for matching ACKs: only the last acknowledgment is retained.

`stream gui` selects latest-only 1152-byte telemetry snapshots, at most one per 33 ms.
`stream key THRESHOLD [SESSION [SENSOR]]` selects the lossless 20-byte HKL1
per-key stream, one record per optical scan frame; with `SENSOR` 0..64 the
session is pinned to that sensor (raw streamed regardless of crossings),
omitting it or passing 255 keeps the first-press auto-selection. The keystroke
hold mode uses the pinned form; the two stream modes are mutually exclusive on
the device and switching flushes the previous session. See
[the current wire layout](MIDI_PROTOCOL.md#hkg6-telemetry) and
[calibration fields](CALIBRATION.md#gui-protocol).
Pending USB payloads remain immutable; only the unsent snapshot is replaced.
Old pending stream bytes may precede the first GUI frame after switching.
The GUI resynchronizes only before its first frame, then requires valid framing
and checksums. GUI sequence gaps are expected, unlike lossless last-key capture.

## Validation

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
python3 -B tools/test_keyboard_gui_tk.py
cmake --build --preset huntsman --target audit-lighting audit-calibration
```

The last command includes USB, keyboard, stream and lighting ARM execution
audits. Optional audit dependencies (Unicorn/pyelftools) and Xvfb for the Tk
test are not needed to run the application or GUI. All tests are offline;
synthetic ASIC/LED responses do not establish electrical behavior or physical
key/threshold usability on the board.

Coverage includes strict threshold boundaries, hysteresis noise, simultaneous
keys/modifiers, per-key edits, neutral arming, rejected configuration, no-CDC
operation, FN/keyboard actions and editor telemetry, USB-reset/invalid/timeout
releases, USB/MIDI/updater regression, pending-USB buffer ownership, FS/HS GUI
framing, JSON validation, PTY command acknowledgments and cancellation,
keystroke-hold capture, full-rate stream mode switching and velocity
reproduction, USB VID/PID device detection, pinned-sensor HKL1 sessions
(ARM-executed), and real Tk geometry/selection/window-resize tests. Live
hardware verification (application flashed via the sibling updater): the
`version` identity handshake after a stale stream (`v0.1.0-RZ03-0499`), GUI
telemetry framing, `cfg velocity` writes with telemetry readback in keyboard
mode, per-key `cfg set` trigger writes with the release threshold preserved,
the settings mirror (`settings=saved`, `settings_gen` incrementing) and its
survival across a reflash with `--keep-settings` on the board's storage pages,
the flashing script's cold boot returning the device to defaults, hold-mode stream engagement at the
measured optical rate (~1.35 k samples/s), velocity window capture and GUI
telemetry resume. Physical Fn+Tab and Fn+V presses, which need a person at the
board, remain a manual check.
