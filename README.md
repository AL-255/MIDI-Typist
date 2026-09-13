# MIDI-Typist

Portable C11 firmware that turns an analog keyboard into an NKRO keyboard and
expressive USB-MIDI controller.

[Documentation website](https://al-255.github.io/MIDI-Typist/) ·
[User manual](USER_MANUAL.md) · [Build guide](docs/BUILDING.md) ·
[Porting guide](docs/PORTING.md)

The supported physical port is the **Razer Huntsman V3 Pro Mini (LPC5528)**.
It uses official NXP MCUXpresso USB/peripheral drivers and retains the existing
bootloader and computer-initiated updater. A synthetic 104-key desktop port
demonstrates the shared application without NXP dependencies. Other hardware
requires a board port, not this binary.

## Use the keyboard

- NKRO typing, right-side arrows and a green-hinted Fn shortcut layer.
- Fn+Enter switches keyboard/MIDI mode; settings preview while held and act
  on release. Release all keys afterward.
- MIDI notes, per-key velocity, polyphonic aftertouch, pitch/modulation wheels
  and sustain on channel 1. Choose piano or Jankó layout, root/scale and octave.
- Per-key Schmitt thresholds default to **3500 press / 3600 release**;
  lower readback means a deeper press.
- Fn+C calibrates keys in parallel. Fn+Tab adjusts triggers; Fn+K/L changes
  brightness. Fn+R requires Y confirmation before clearing custom settings.
- Committed settings and calibration persist on-device. Release all keys and
  wait for **settings saved** in the GUI before unplugging; saving waits for
  250 ms without changes. Missing/corrupt saves initialize defaults.

See the [illustrated manual](USER_MANUAL.md) for the complete shortcuts,
[note maps](USER_MANUAL.md#default-notes-two-overlapping-playing-ranges),
lighting and menu instructions.

## Configuration GUI

On Linux with Python 3.10+ and Tk:

```sh
python3 tools/keyboard_gui.py          # auto-detect the CDC device
python3 tools/keyboard_gui.py --demo   # preview without hardware
```

The ANSI GUI edits thresholds/mappings, displays velocities, starts calibration,
exports host JSON profiles and flashes application images after confirmation.
Only one program may own CDC. See [GUI operation](docs/KEYBOARD_GUI.md).

## Build from scratch

Install CMake 3.21+, Ninja, a native C compiler, Python 3.10+ and Arm GNU
bare-metal tools. The pinned official NXP source subset is included; neither
the original updater EXE nor extracted firmware is needed to build.

```sh
git clone https://github.com/AL-255/MIDI-Typist.git
cd MIDI-Typist
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset huntsman
cmake --build --preset huntsman
```

Output: `build-keyboard-fn-menu/huntsman_firmware.bin`, exactly 131072 bytes.
`huntsman` and `keyboard-fn-menu` are equivalent; `firmware` is USB-only.
Use `simulator` for the SDK-free desktop port.
[Build prerequisites and tests](docs/BUILDING.md) ·
[Validation scope](docs/VALIDATION.md)

## Flashing and safety

Use the [custom firmware flashing tool](https://github.com/AL-255/Huntsman-V3-Pro-Mini-Flasher)
or the configuration GUI's **Flash application** action. For the bundled CLI:

```sh
git submodule update --init third_party/huntsman_updater
sudo python3 tools/flash_application.py build-keyboard-fn-menu/huntsman_firmware.bin
```

Choose **application only**, leaving secondary firmware disabled. Normal
updates preserve compatible saves; `--reset-settings` explicitly deletes
custom settings and calibration. Builds, tests and documentation CI never flash.

Custom storage writes only **0x78000 and 0x78200**. Razer primary settings and
serial data, bootloader, factory/security data and the optical ASIC firmware
are not write targets. Returning to stock may reclaim the custom tail space.
Keep device dumps private. See [storage boundaries](docs/DEVICE_CONFIG_STORAGE.md).

## Development and documentation

Start with [architecture](docs/ARCHITECTURE.md), then the
[board-porting guide](docs/PORTING.md). Shared algorithms remain independent of
the MCU SDK; each board owns acquisition, USB, lighting and safe storage.

The [documentation guide](docs/README.md) links all technical references.
[Sphinx build and publishing](docs/DOCUMENTATION.md) explains local previews,
strict link checks and GitHub Pages CI. The same Markdown is readable on GitHub.

Project code is [GPL-2.0](LICENSE); vendor files retain their upstream
[licenses and provenance](third_party/ORIGINS.md).
