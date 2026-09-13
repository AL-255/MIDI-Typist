# MIDI-Typist

For everyday use, read the illustrated, self-contained [keyboard user manual](USER_MANUAL.md).

Portable C11 firmware for turning analog keyboards into NKRO keyboards and
expressive MIDI controllers. The application is separate from board-specific
scanning, lighting, USB, storage and updater integration.

The supported physical board is the **Razer Huntsman V3 Pro Mini (LPC5528)**,
using the official NXP MCUXpresso USB/peripheral drivers. Its existing
bootloader and computer-initiated application updater are retained.
A 104-key desktop reference port demonstrates the same application without
NXP dependencies; this is not a universal binary for unported keyboards.

Use `cmake --preset huntsman` / `cmake --build --preset huntsman` for the
keyboard, or `simulator` for the desktop port. Existing presets remain valid.
See [architecture](docs/ARCHITECTURE.md), [adding a board](docs/PORTING.md),
[scheduling/FreeRTOS](docs/SCHEDULING.md), and [building from scratch](docs/BUILDING.md).
The feature guide and host GUI below describe the Huntsman port.

## Port to another platform

Follow the [platform porting guide](docs/PORTING.md). It includes a runnable
desktop example, a board build-manifest pattern, a C lifecycle adapter,
ADC/key/LED contracts, USB and storage responsibilities, RTOS integration,
and a hardware acceptance checklist. Add your board under `firmware/boards`;
do not copy the shared algorithms or introduce SDK headers into `firmware/app`.

The [documentation index](docs/README.md) separates user, build, porting and
host-integration reading paths. GUI layout and Huntsman flash/protocol formats
are board-specific; portability does not make the Huntsman binary safe for
an unported device.

## Huntsman feature overview

**Current build: `huntsman` (alias `keyboard-fn-menu`).** It provides a lit Fn system menu,
calibrated trigger-point editing, backlight brightness controls, NKRO typing,
43-note MIDI mapping with velocity/aftertouch, parallel calibration and a
read-only CDC flash dumper. Calibration alone persists in two reserved tail
pages, preserving serial-number storage.

Native portability/regression tests and compiled hardware checks are described
in [validation status](docs/CALIBRATION.md#validation-status).
Build/test commands never flash hardware. See [the documentation guide](docs/README.md).

## Use the keyboard

On boot, the application starts in standard NKRO keyboard mode. Optical
scanning and travel-reactive lighting start after USB configuration; the GUI
is not required for keyboard or MIDI operation.

Normal backlighting is **on at rest and dims as a key is pressed**. Keyboard
mode lights every key; MIDI mode lights only keys with a configured note.
Enter's mode marker, octave-shift hints, Fn previews and calibration feedback
remain separate indicators. MIDI velocity and aftertouch are unchanged.

- Press below the per-key press threshold; release above its release threshold.
  Defaults are **3500 / 3600**. Equality holds the current state.
- **Right Alt / Menu / Right Ctrl / Right Shift** send **Left / Down / Right / Up**
  in keyboard mode, not their original modifier/Menu actions.
- Hold **Fn**: keyboard shortcuts light **green**, settings **C, Tab, Caps, K,
  L and R** light white, and Enter shows the target mode color. Other keys
  go dark. In MIDI mode only the Enter/K/L/R/Left Shift/E/S settings are available.
- **Fn+Esc** sends backtick; **Fn+1–0, -, =** send **F1–F12**;
  **Fn+Backspace** sends **Delete**. **Fn+Y/P/N/M/H/J** send
  **Insert / Print Screen / End / Page Down / Home / Page Up**.
  These are ordinary held keyboard shortcuts: keep Fn held to repeat taps.
- Hold **Fn+C** to preview `CALIBRATION`, then release to start. **Fn+Tab**
  previews `TRIGGER` and opens the editor on release:
  release Fn, choose **1–0** for levels 1–10, then **Escape** or **Fn+Tab** to
  apply and exit. The selected number is green; the white number-row bar shows
  travel. Commit updates all raw Schmitt pairs using each key's calibration,
  with the original fixed-threshold exceptions. Release all keys to resume.
- **Fn+K / Fn+L** lowers/raises brightness across the original 20 levels.
  The `LIGHT-`/`LIGHT+` preview repeats while held; release changes one step.
  Keep Fn held and tap K/L repeatedly; each release changes another step.
  Fn-menu choices, including brightness and the trigger point, are stored in
  the two authorized tail pages and survive a power cycle; Fn+R clears them.
  See [Fn menu details](docs/FN_MENU.md).
- All Fn settings options **preview while held and execute once on release of
  either key** (RESET opens confirmation). Except for repeated K/L taps while
  Fn stays held, release all keys to rearm. **Fn+Enter** switches mode only on
  release; the chord is consumed rather than sent as Enter.
- While **Fn+Enter** remains held, the target word **MIDI** or **KEYBOARD**
  lights blue (MIDI) or green (KEYBOARD), matching Enter's target-mode hint,
  at 30%, highlighting each letter at 100% for 0.2 seconds in
  sequence, with a 0.5-second pause between repeats. Releasing either key
  immediately ends the animation and executes the action (visible on the next LED update).
  Enter then remains green for keyboard or blue for MIDI, at the same full
  channel intensity as unpressed note keys, scaled by global brightness.
- **Fn+Tab** in MIDI mode opens a raw trigger page: ten steps move the press
  threshold for all keys between the 3500 default and the velocity window's
  bottom-out floor; release thresholds are preserved.
- **Fn+V** opens the transmitted-velocity start editor in MIDI mode: a
  ten-step bar on the number row where `1` is 0% (measured velocity) and `0`
  is 100% (every note at full velocity); Escape leaves the page.
- **Fn+J** toggles the built-in **Jankó** playing layout in MIDI mode: the
  letter, number and punctuation rows adopt a staggered whole-tone mapping
  without touching the configured notes, and the Fn+Left Shift lower-row mute
  is inactive while it is on. The J hint turns green while active; see the
  [user manual](USER_MANUAL.md#play-the-built-in-jankó-layout).
- **Fn+R** previews `RESET`; release opens `RESET?`, with **Y green / N red**
  at full brightness. Release all keys, then press **Y** to clear our two
  saved-calibration pages or **N** to cancel. Confirmation consumes these keys;
  a pre-held Y cannot confirm, and Y+N together cancels. A confirmed reset
  restores default thresholds, mappings, keyboard mode, octave and brightness
  once all keys are released. Factory/serial data remains untouched. Recalibrate
  afterward; deletion cannot be undone on-device. **Fn+Caps** previews `RAPID`
  and enters the compatibility editor on release.
- In MIDI mode, **Fn+Left Shift** toggles the Caps and Shift rows' notes off/on.
  Hold to preview `LOWER-OFF` / `LOWER-ON`, then release to apply and release
  all keys to resume. Muted notes go dark; the Esc/Tab rows and bottom-row
  controls remain available. Enter keeps its blue mode marker, but any note
  assigned to it is muted too. Mappings are retained. The setting survives
  mode switches, but restart/RESET enables both groups. It is not saved in JSON.
- **Fn+E** previews `KEY`, then opens root selection on release. Release all
  keys, hold an upper piano-row root key to preview its name, and release to
  apply. **Fn+S** similarly previews `SCALE` and opens scale selection:
  **J** major, **I** natural minor, **D** Dorian, **H** Phrygian, **Y** Lydian,
  **M** Mixolydian, **L** Locrian, **P** major pentatonic, **O** minor
  pentatonic, **T** chromatic (12T). Choices are dim white, the current choice
  green, and Escape red (cancel). Only in-scale, mapped, enabled, in-range
  notes can play and light up; control/mode markers stay visible. Default is
  C/chromatic. Root/scale survive mode switches, but not restart/RESET, and
  are not stored in host profiles. See [the user manual](USER_MANUAL.md#choose-a-root-and-scale).
- In MIDI mode, **Right Alt lowers the octave** and **Right Ctrl raises it**.
  These act once per press. Existing held notes keep their original pitch.
  Right Alt blinks blue for a negative shift; Right Ctrl blinks blue for a
  positive shift. Larger shifts blink faster, from a 1.2-second cycle at ±1
  to a 0.12-second cycle at ±10. Zero shift restores steady blue.
- **Left Windows** is modulation (CC1). **Left Ctrl bends pitch down** and
  **Left Alt bends pitch up**. Each wheel is linear from raw **3800 = 0%** to
  **1000 = 100%**, clamped outside that range, independent of key thresholds
  and calibration. Opposing pitch inputs combine; equal pressure cancels to
  center. Your instrument determines the pitch-bend range in semitones.
  These five controls use Enter's full-intensity blue, scaled by global brightness;
  only the active octave indicator blinks.
- **Space** is sustain: channel-1 **CC64 = 127** on press and **0** on release.
  It uses Space's Schmitt thresholds (default press below 3500, release above
  3600), not the wheel endpoints. Space is steady blue at Enter's brightness,
  remains available with any root/scale or row filter, and cannot map to a note.
  Fn/menu entry, mode changes and fault cleanup release sustain.
- MIDI uses **channel 1**, Note On/Off, strike velocity and independent
  **polyphonic key pressure (aftertouch)**. Ordinary HID typing is suppressed
  in MIDI mode. Connect the keyboard's MIDI input to a software instrument
  that accepts channel 1 and polyphonic aftertouch.
- Unmapped keys are silent in MIDI mode. Fn, octave, wheel and sustain controls are
  reserved; Enter may be mapped but still participates in the mode chord.

MIDI note names in this project use **C0 = note 12; C4 = note 60**. Some music
applications display different octave labels for the same MIDI number.
The default mapping is:

| Keyboard | MIDI note | Number | Keyboard | MIDI note | Number |
| --- | --- | ---: | --- | --- | ---: |
| Tab | C5 | 72 | Left Shift | C4 | 60 |
| 1 | C#5 | 73 | A | C#4 | 61 |
| Q | D5 | 74 | Z | D4 | 62 |
| 2 | D#5 | 75 | S | D#4 | 63 |
| W | E5 | 76 | X | E4 | 64 |
| E | F5 | 77 | C | F4 | 65 |
| 4 | F#5 | 78 | F | F#4 | 66 |
| R | G5 | 79 | V | G4 | 67 |
| 5 | G#5 | 80 | G | G#4 | 68 |
| T | A5 | 81 | B | A4 | 69 |
| 6 | A#5 | 82 | H | A#4 | 70 |
| Y | B5 | 83 | N | B4 | 71 |
| U | C6 | 84 | M | C5 | 72 |
| 8 | C#6 | 85 | K | C#5 | 73 |
| I | D6 | 86 | , | D5 | 74 |
| 9 | D#6 | 87 | L | D#5 | 75 |
| O | E6 | 88 | . | E5 | 76 |
| P | F6 | 89 | / | F5 | 77 |
| - | F#6 | 90 | ' | F#5 | 78 |
| [ | G6 | 91 | Right Alt | octave − | — |
| = | G#6 | 92 | Right Ctrl | octave + | — |
| ] | A6 | 93 | Left Ctrl | pitch bend − | — |
| Backspace | A#6 | 94 | Left Alt | pitch bend + | — |
| Backslash | B6 | 95 | Left Windows | modulation | CC1 |

Left Shift remains a normal modifier in keyboard mode. All mappings remain
GUI-editable except Fn and the octave/wheel controls. Loading a JSON profile
can overwrite these defaults with that profile's saved mappings.

## Configuration GUI

On Linux, with Python 3 and Tk installed:

```sh
python3 tools/keyboard_gui.py
# Explicit port override; the default auto-detects USB 1532:02b0:
python3 tools/keyboard_gui.py --device /dev/ttyACM0
# Offline preview; never opens the keyboard:
python3 tools/keyboard_gui.py --demo
```

On startup, on **Detect**, and whenever **Connect** is pressed with an empty
or `auto` device field, the GUI scans `/sys/class/tty/ttyACM*` for the first
port whose USB ancestry reports `idVendor 1532` / `idProduct 02b0`.

Click **Connect**, then click a key on the physical ANSI layout. The GUI shows
live raw samples, down/up state, firmware-calculated velocity, current mode,
octave and device-confirmed settings.

- Set the selected key's press/release pair, then **Apply to selected key**.
- **Apply thresholds to all keys** changes every pair atomically on the MCU.
- Choose a note name, MIDI number 0–127, or **Off**, then **Apply MIDI mapping**.
  The key captions display the confirmed mapping; Fn and octave/wheel keys are
  reserved controls. Note mappings can be edited in either performance mode.
- **Save profile…** exports confirmed thresholds and MIDI mappings to host JSON.
  **Load + apply profile…** disables output, applies and checks each setting,
  then restores the prior enable state. Older threshold-only JSON still loads.
- Enable/disable controls govern both keyboard and MIDI output; raw monitoring
  and velocity calculations continue. Configuration edits release output and
  require a fresh neutral frame before input resumes.
- **Hold first 20 pts of keystroke** freezes the bottom-right plot into a
  per-keystroke capture: engaging it switches the device to a full-rate
  per-key stream (every optical scan frame, ~1.35 k samples/s on this
  hardware) and holds the first 20 samples after the selected key's trigger,
  with the firmware's velocity fit reproduced from the same window, until the
  next press. See [the GUI guide](docs/KEYBOARD_GUI.md#keystroke-hold-mode).

The updater used for flashing is vendored as the `third_party/huntsman_updater`
git submodule; run `git submodule update --init third_party/huntsman_updater`
once after cloning. The GUI's **Flash application...** control and
`tools/flash_application.py` both drive it through `tools/firmware_flasher.py`.

**Calibration endpoints and the Fn-menu settings save to the device; host
`cfg set`/`cfg all` threshold edits and MIDI mappings remain RAM-only.** In keyboard mode use **Fn+C** or the GUI's **Calibrate keys
→ device flash** button. Release all keys; wait 500 ms (purple → blue), then
fully press and hold blue keys for one second until green. Multiple keys can
be held together; moving/releasing one does not reset the others. Green keys
may stay held while you calibrate the rest. Include Fn and modifiers. Five seconds
of inactivity or GUI cancellation discards the attempt. Completion saves all
endpoints; the GUI shows progress and saved generation.

Only pages **0x78000 and 0x78200**, independently verified unused and FF-filled,
are write targets. Serial-number/primary settings pages are never erased.
The application-image reservation remains unused. Returning to stock may
reclaim the tail space and discard custom calibration.
See [calibration instructions](docs/CALIBRATION.md) and [storage design](docs/DEVICE_CONFIG_STORAGE.md).

Close other serial monitors before connecting; only one tool should own CDC.
Use your system's serial-port permissions rather than running the GUI as root.
The graphical layout currently supports ANSI/61 keys; firmware scan/MIDI logic
also handles the recovered ISO/62 and JIS/65 layouts.

## Build from scratch

Requirements: Arm GNU bare-metal tools (`arm-none-eabi-gcc`, tested **14.2.1**),
CMake **3.21+**, Ninja, a native C compiler, Python **3.10+**, and Tk for the GUI.
NXP sources are already vendored at pinned official SDK revisions; no updater
EXE, original firmware, SDK installation or network download is needed to
compile the application.

```sh
git clone git@github.com:AL-255/MIDI-Typist.git
cd MIDI-Typist

cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests

cmake --preset huntsman
cmake --build --preset huntsman
```

Outputs in `build-keyboard-fn-menu/`: `huntsman_firmware.elf`, `.hex` and
`.bin`. The binary is exactly **131072 bytes**. The linker and post-build
validator enforce application/config boundaries, vectors and USB descriptors.

**Use `huntsman`, not `firmware`, for the complete application.**
The `firmware` preset is intentionally USB-only.
See [clean builds, dependencies and testing](docs/BUILDING.md).

## Scan/debug tools

```sh
# Twenty post-trigger samples, bottom-out velocity estimate, then exit:
python3 -u tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3600
# Rearm above the threshold and capture again until Ctrl+C:
python3 -u tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3600 --repeat
# See numeric and compact ANSI visualization options:
python3 tools/decode_scan_stream.py --help
```

The raw stream and compact capture modes are documented in
[scan streaming](docs/SCAN_STREAM.md) and [last-key capture](docs/LAST_KEY_STREAM.md).
Selecting a CDC display does not select keyboard/MIDI performance mode.

## Design and validation

- [Fn system menu, trigger editor and brightness](docs/FN_MENU.md)
- [MIDI state machine, encoding, timing, safety and tradeoffs](docs/MIDI_DESIGN.md)
- [MIDI mapping and interval pop-filter behavior](docs/MIDI_FILTER.md)
- [Device telemetry: streams, fields and text replies](docs/TELEMETRY.md)
- [Command acknowledgments and profile format](docs/MIDI_PROTOCOL.md)
- [Parallel calibration and its physical save/readback record](docs/CALIBRATION.md)
- [Tail-page storage, Fn-menu persistence and serial-number protection](docs/DEVICE_CONFIG_STORAGE.md)
- [Build and test instructions](docs/BUILDING.md)
- [Optical/keyboard recovery](docs/KEYBOARD_RECOVERY.md)
- [Travel lighting and calibration limitations](docs/TRAVEL_LIGHTING.md)
- [Independent velocity registration](docs/KEY_VELOCITY.md) and
  [firmware normalization](docs/NORMALIZED_VELOCITY.md)
- [USB integration and safety](docs/USB_DESIGN.md)
- [SDK source origins and licenses](third_party/ORIGINS.md)

Velocity collects a window from the triggering sample onward — ten readbacks
maximum, closed early below the shared bottom-out threshold of 1500 — and
divides the total drop by the interval count before firmware-side 0…1
normalization. Windows longer than five samples discard the interval furthest
from their median first; ties discard the earliest interval.
The host capture tool uses the same estimator and prints fractional counts/s.
See [filter details and limitations](docs/MIDI_FILTER.md).

Velocity assumes **8000 scans/s**, as requested; this is not proof of an actual
8 kHz hardware readback rate. Five actual subsequent samples are always used,
so real elapsed
latency and the velocity scale depend on the actual scan cadence. Aftertouch
is normalized optical travel, not a calibrated force measurement.

Offline tests exercise C logic, Tk with a simulated CDC device, and the linked
ARM USB/scan/lighting paths with synthetic hardware replies. They do not prove
electrical behavior, physical LED colors, real-time throughput, DAW integration
or complete hardware recovery. See [current validation](docs/CALIBRATION.md)
for the build and remaining verification limits.

## Updating and safety

Use the [custom firmware flashing tool](https://github.com/AL-255/Huntsman-V3-Pro-Mini-Flasher)
to install this application's firmware. Select the application `.bin` and leave
secondary-firmware flashing disabled.

The USB composite device exposes NKRO HID, USB-MIDI, CDC ACM and the existing
90-byte updater HID interface (VID:PID `1532:02b0`). Use the supplied
`../updater/` application-only implementation for an explicitly authorized
hardware update. Do not use a generic programmer at address zero or treat the
RAM execution address `0x20000000` as a physical flash address.

Do not modify the bootloader, serial-number/primary settings, factory/security
regions or secondary optical-controller firmware. Calibration owns only the
two documented tail pages; there is no arbitrary flash-write command.
Manual bootloader recovery is expensive and is
not a test strategy. The original extraction remains read-only and is not
distributed in this repository; original-firmware address references in design notes
are evidence, not flash-write targets. Application code is GPL-2.0; vendored
SDK files retain their upstream licenses.
