# Validation and limitations

This is the common evidence boundary for the complete `huntsman` build. Do not treat modeled hardware as a physical test.
Commands and dependencies are in [Building](BUILDING.md).

## Automated checks

| Layer | Coverage |
| --- | --- |
| Native C/Python (15 CTest suites) | Shared defaults/alternate-initializer builds, lifecycle and architecture, NKRO, Schmitt/velocity, menus, MIDI, parallel calibration, complete storage snapshots, GUI/SysEx transport, device-flashing adapters, current-only rules and capture framing |
| Linked Cortex-M33 execution | SDK startup/USB/DMA/I2C paths, descriptor/control transfers, MIDI packets, LED writes, faults, MIDI SysEx, updater entry and storage integration |
| Original-reference comparison | Selected scan/editor/lighting behavior and flash register transactions; the original fixture is separate and read-only |
| Tk against simulated MIDI | Real widgets, configuration ACK/readback, capture isolation, flashing-tab actions/confirmation, bounds and timeout handling, resolved typography (antialiased or native-pixel bitmap faces) and a settings panel that scrolls in a small window; no keyboard opened |
| Sphinx | All public pages build, internal references resolve, source links exist; warnings fail CI |

`python3 tools/run_tests.py` runs 19 audit groups, including the 15 native
suites, with a 300-second total deadline. It requires the optional original
reference and Python/Tk dependencies. Missing dependencies are failures, not
silent skips. Ordinary builds and native tests do not need that reference.

Storage tests cover all supported layouts, settings/calibration preservation,
MIDI+Jankó boot restoration, missing/corrupt saves, fallback generations,
unsupported-schema rejection, controller faults and all 512 byte-cut points
of an inactive-page write. The model rejects writes outside the two tail slots.

## Physical checks

The complete image is flashed through the GUI's application-only backend and
enumerates at 480 Mbps with NKRO, two MIDI cables and updater HID, without CDC.
The READY identity and read-only `git` command match the build's generated
commit/state metadata. Queries during GUI snapshots and a 501-sample capture
return the same identity without interrupting the stream.
Linux RtMidi connects to the dedicated control port and receives all 61 sensors.
A per-key threshold edit and restoration pass ACK/readback; storage returns to
saved state. Three two-second pinned captures deliver about 1.34 ksample/s each
with no reported sequence/CRC loss or scan/LED errors. Closing/reopening the
control connection, changing the captured sensor and switching back to snapshots
work without resetting USB.

Kernel logs show the expected bootloader/application reconnect and MIDI 1.0
binding. The control-only updater HID has no interrupt endpoint by design.
These checks do not verify every physical key, DAW, or electrical power cycle.
No reset of the user's calibration is part of this test. The storage implementation and offline write guards restrict writes to the
two custom tail pages. A readback comparison of the primary settings/serial
window at 0x49000–0x493ff checks that those protected bytes are unchanged.

Firmware and host velocity math use the declared **8000 Hz** timebase, not a
measured delivery rate. GUI telemetry is latest-only at about 30 Hz. Measure
acquisition separately; USB speed and the nominal timer period
do not establish sustained 8 kHz scanning.

Factory restoration and the initially bootloader-only flow have offline adapter
coverage; they are not separate physical conversion tests.

## Not established

- Electrical power-cut recovery, flash endurance and physical unplug/replug
  restoration in MIDI+Jankó mode; automated boot/interruption tests cover logic.
- Calibrated distance, force or velocity in physical units.
- Physical LED color/animation timing, worst-case interrupt/stack margins,
  sustained 8 kHz acquisition, USB certification or comprehensive DAW support.
- Full stock-firmware feature equivalence, factory restoration from an
  incomplete dump, or compatibility with unported keyboards/MCUs.

Do not reset a user's calibration just to test recovery. Keep dumps and their
error maps outside version control; unreadable flash bytes are not backups.
