# Validation and limitations

This is the common evidence boundary for the complete `huntsman` build
(alias `keyboard-fn-menu`). Do not treat modeled hardware as a physical test.
Commands and dependencies are in [Building](BUILDING.md).

## Automated checks

| Layer | Coverage |
| --- | --- |
| Native C/Python (14 CTest suites) | Shared lifecycle and architecture, NKRO, Schmitt/velocity, menus, MIDI, parallel calibration, complete storage snapshots, GUI/PTY transport, flasher and capture framing |
| Linked Cortex-M33 execution | SDK startup/USB/DMA/I2C paths, descriptor/control transfers, MIDI packets, LED writes, faults, CDC, updater entry and storage integration |
| Original-reference comparison | Selected scan/editor/lighting behavior and flash register transactions; the original fixture is separate and read-only |
| Tk against simulated serial | Real widgets, configuration ACK/readback, capture isolation, bounds and timeout handling; no keyboard opened |
| Sphinx | All public pages build, internal references resolve, source links exist; warnings fail CI |

`python3 tools/run_tests.py` runs 19 audit groups, including the 14 native
suites, with a 300-second total deadline. It requires the optional original
reference and Python/Tk dependencies. Missing dependencies are failures, not
silent skips. Ordinary builds and native tests do not need that reference.

Storage tests cover all supported layouts, settings/calibration preservation,
MIDI+Jankó boot restoration, missing/corrupt saves, fallback generations,
legacy calibration migration, controller faults and all 512 byte-cut points
of an inactive-page write. The model rejects writes outside the two tail slots.

## Physical checks

The connected ANSI keyboard passes application-only computer-initiated updates,
high-speed USB return, and a complete application readback matching the build.
CDC snapshots and pinned A/Q/A scan sessions run without checksum/sequence
failures; keyboard/lighting/MIDI/storage error counters remain zero.

Empty-tail initialization and automatic saves work on hardware. A changed
per-key threshold and velocity-start setting survive an application update and
reboot, with no rewrite when unchanged. Test settings are restored afterward.
Razer primary settings/serial bytes at `0x49000..0x49400` compare unchanged.

Pinned scan delivery measures approximately **1.36 ksample/s**, not 8 kHz.
Firmware and host velocity math still use the declared **8000 Hz** timebase.
GUI telemetry is latest-only at about 30 Hz. Do not infer acquisition rate
from USB speed, CDC baud rate or the nominal timer period.

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
