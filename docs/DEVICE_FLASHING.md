# Device flashing

Open **Device flashing** in the GUI. The firmware workspace is separate from
live keyboard configuration; it does not need a working MIDI-Typist application
to detect a Razer keyboard or its bootloader.

## Choose a keyboard and destination

The model dropdown offers **Razer Huntsman Pro Mini V3** (the
Huntsman V3 Pro Mini, RZ03-0499) and **MonsGeek M1 V5 TMR (experimental)**.
The M1 has a separate [experimental conversion path](#monsgeek-m1-experimental-conversion).
The conversion instructions below apply to Huntsman. Detection shows product, reported serial,
software build when available, USB VID/PID, physical port, speed and USB revision.
The firmware identity includes its embedded full Git commit and clean/dirty
state. Use that commit when identifying a build; a dirty build also contains
uncommitted changes, and `unknown` means no Git metadata was available.
Use **Read firmware details…** for additional HID version/capability information;
Linux may request authorization for raw USB access.

| Connected state | Available actions |
| --- | --- |
| Razer application | Install MIDI-Typist; restore/reinstall a supplied Razer application |
| Razer bootloader | Install MIDI-Typist; restore a supplied Razer application directly |
| MIDI-Typist | Reflash MIDI-Typist; restore a supplied Razer application |
| Missing, ambiguous or unrecognized device | Flashing disabled |

Exactly one supported Huntsman must be connected. Bootloader mode has no
application serial/version query; unavailable fields are labelled rather than
guessed. MIDI-Typist currently exposes a custom identifier, not the factory
serial. Its HID compatibility version is distinct from its SysEx software build.

## Review and flash

1. Refresh the device and choose the destination.
2. Browse for an image, or use the local complete-build image for MIDI-Typist.
3. Validate the image. Review size, SHA-256 and source description.
4. Confirm that the image belongs to the selected keyboard model.
5. Choose **Review and flash…**, check the device and image again, and confirm.
6. Keep the keyboard connected. The GUI shows programming progress and a
   bounded operation log, verifies application return, then refreshes identity.

The GUI closes its configuration connection before flashing. Reconnect it
after a MIDI-Typist update. Razer firmware does not support this GUI's custom
configuration channel. An error never triggers an automatic flashing retry;
refresh device state before deciding what to do next.

## Accepted images and restoration limits

- MIDI-Typist: the Huntsman application `.bin` or contiguous Intel `.hex` from
  the complete build. The application must identify the matching board and current SysEx control
  implementation; older custom images are rejected.
- Razer: a user-supplied `DeviceUpdater.resources`, application `.hex`, or
  complete 128 KiB application `.bin`. Resource VID/PID metadata must match.
- Installer `.exe` files are not accepted or executed. Select the extracted
  application/resources instead. No Razer firmware is bundled, downloaded,
  copied into the repository or redistributed by this feature.

Checks cover image size, initial stack/reset vectors, HEX checksums/address
coverage, available model metadata and the confirmed payload hash. They do not
authenticate the publisher or prove that an arbitrary raw image is stock
firmware. Obtain the correct firmware from a trusted source.

All actions use the supplied updater's **application-only** path. Neither the
bootloader nor secondary optical firmware is flashed. Razer primary settings,
serial-number storage and security data are not write targets. Compatible
custom settings/calibration are retained by an ordinary custom update.
Razer firmware may reclaim custom tail storage when it runs. Restoration means
restoring the supplied application, not rewriting every factory region.

## Adapter design and tests

`flash_models.py` defines device, image, action and adapter contracts. The Tk
view in `keyboard_flash_tab.py` only renders these contracts. Register future
models in `adapters()` and implement their discovery, identity, image validation,
allowed transitions and updater boundary in a separate adapter.
Each adapter declares its inspectable modes; remembered image paths are scoped
to the model and destination. `flash_monsgeek.py` verifies M1 ID2949 through
vendor HID before allowing its guarded factory-to-IAP transition.

`flash_huntsman.py` owns Linux detection and the Razer-specific application
updater. Before any write it revalidates the device token and confirmed image
hash. Updater opens remain bound to the confirmed physical USB location through
bootloader/application re-enumeration. Multiple matching devices are rejected.
The private `device_flash_service.py` worker is the GUI's privilege boundary,
not a separate supported desktop application.

Native tests exercise all six transition combinations, changed/ambiguous devices,
wrong images and excluded regions using mocks. Tk tests cover tab selection,
actions, confirmation and validation invalidation without device access.
The live GUI reflash path is checked on a connected MIDI-Typist keyboard.
Stock restoration and an initially bootloader-only device are covered offline,
not claimed as separate physical conversion tests. See [validation](VALIDATION.md).

## MonsGeek M1 experimental conversion

**Flashing, reset-to-IAP recovery and high-speed USB diagnostics work on the test
keyboard. Factory calibration validation blocks keyboard startup. Do not install
this trial for normal use.**

Select the M1 model and use **Read firmware details…** to confirm internal
ID2949 for factory firmware, or the embedded build target over the selected
device's USB-bound MIDI control port for custom firmware. Verified factory
devices offer installation/restoration; custom devices offer reflash/restoration.
Configuration disconnects before custom inspection or flashing. Choose
`build-m1-hal/m1_development.bin` for the current trial, or supply your own
ID2949 factory `.bin`. A factory file may be application-only or boot-prefixed;
only its application slice from `0x5000` through at most `0x28000` is sent.
No vendor image is bundled. Linux needs libusb and the GUI Python dependencies.

**Factory entry resets stock user settings.** It preserves sensor-calibration
pages `0x08032000/0x08032800` and bootloader code. The bootloader erases the
application and both custom save slots before enumerating. The adapter binds
the transition to the same physical port, serializes 64-byte writes without
automatic retries, and requires the bootloader's checksum/readback verdict.
That verdict confirms transfer, not working keyboard functionality.

**The trial deliberately keeps reset-to-IAP recovery armed.** Before starting
peripherals, it programs only the IAP magic word at `0x08004800`, from SRAM,
and refuses a nonblank metadata page. It does not erase bootloader metadata.
If this startup step completes, the next reset/power cycle enters the factory
updater and erases the trial application and custom saves. Both power-cycle and
software-requested return to the factory bootloader have been observed.

A pre-existing shared bootloader PID is not sufficient model/recovery proof,
so the GUI does not offer direct recovery of an unidentified bootloader.
The trial's SysEx `bootloader` command permits a controlled reset only while
the recovery flag is armed. Full transport switching and power management
remain incomplete.
