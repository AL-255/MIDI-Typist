# Device flashing

Open **Device flashing** in the GUI. The firmware workspace is separate from
live keyboard configuration; it does not need a working MIDI-Typist application
to detect a Razer keyboard or its bootloader.

## Choose a keyboard and destination

The model dropdown contains **Razer Huntsman Pro Mini V3** (the
Huntsman V3 Pro Mini, RZ03-0499) and **MonsGeek FUN60 PRO Wired**.
Huntsman supports application flashing. FUN60 supports read-only USB inventory;
its flashing actions remain disabled until the complete port is validated.
Detection shows product, reported serial,
software build when available, USB VID/PID, physical port, speed and USB revision.
The firmware identity includes its embedded full Git commit and clean/dirty
state. Build hints and SysEx replies must match the selected board target;
the GUI never labels one board with another board's configuration identity.
Use that commit when identifying a build; a dirty build also contains
uncommitted changes, and `unknown` means no Git metadata was available.
Use **Read firmware details…** for additional HID version/capability information;
Linux may request authorization for raw USB access.

| Connected state | Available actions |
| --- | --- |
| Razer application | Install MIDI-Typist; restore/reinstall a supplied Razer application |
| Razer bootloader | Install MIDI-Typist; restore a supplied Razer application directly |
| MIDI-Typist on Huntsman | Reflash MIDI-Typist; restore a supplied Razer application |
| FUN60 application or bootloader candidate | Read-only identification; flashing disabled |
| Missing, ambiguous or unrecognized device | Flashing disabled |

Exactly one supported Huntsman must be connected. Bootloader mode has no
application serial/version query; unavailable fields are labelled rather than
guessed. MIDI-Typist currently exposes a custom identifier, not the factory
serial. Its HID compatibility version is distinct from its SysEx software build.

FUN60 inventory accepts application PID `3151:502D`, not the wireless or other
model PIDs. `3151:502A` is shown as **SKU unverified** because that bootloader
PID alone does not establish the model. USB revision is not presented as a
software version. Inventory reads Linux sysfs only: it does not send factory
commands, enter IAP or ask for elevated access. Factory bootloader entry can
erase settings and erases the application before USB enumeration; see the
[FUN60 erase boundaries](MONSGEEK_FUN60_PRO.md#firmware-update-contract).

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

All enabled Huntsman actions use the supplied updater's **application-only** path. Neither the
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

`flash_huntsman.py` owns Linux detection and the Razer-specific application
updater. Before any write it revalidates the device token and confirmed image
hash. Updater opens remain bound to the confirmed physical USB location through
bootloader/application re-enumeration. Multiple matching devices are rejected.
The private `device_flash_service.py` worker is the GUI's privilege boundary,
not a separate supported desktop application.

`flash_fun60.py` supplies the read-only inventory adapter. Both its GUI action
list and private-worker write entry point refuse flashing; bypassing the
disabled button does not invoke IAP. The separate `monsgeek_iap.py` protocol
library has offline tests but no enabled device-writing adapter.

Native tests exercise all six transition combinations, changed/ambiguous devices,
wrong images and excluded regions using mocks. Tk tests cover tab selection,
actions, confirmation, board-specific provenance, disabled FUN60 writes and
validation invalidation without device access.
The live GUI reflash path is checked on a connected MIDI-Typist keyboard.
Stock restoration and an initially bootloader-only device are covered offline,
not claimed as separate physical conversion tests. See [validation](VALIDATION.md).
