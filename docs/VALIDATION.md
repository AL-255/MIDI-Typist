# Validation and limitations

This is the common evidence boundary for the complete `huntsman` build. Do not treat modeled hardware as a physical test.
Commands and dependencies are in [Building](BUILDING.md).
The separate [M1 backend](MONSGEEK_M1.md#verification-limits) has a physically
checked read-only identity query, official-SDK ARM library builds, and native
82-key application/scanner plus GUI tests. Native tests also cover Fn transport
controls, USB-only MIDI, the battery curve/filter, sleep-request policy and radio packet codec.
Linked Cortex-M4 tests execute clock, wired-startup, battery-input, USB power-down, RTC sleep,
scan, LED and SPI3 radio HALs with official SDK drivers; register effects and completion
events are modeled. These do not validate an installable
M1 firmware or peripheral timing.
The composite USB audit also drives real SDK endpoint/control routines through
shared SysEx into the 82-key GUI decoder, including configuration ACK/readback,
bounded control requests, ownership, suspend and session reset at both USB speeds.
It executes the SDK hardware initializer and reset IRQ, with deferred attachment,
bounded clock/counter/reset/flush failures, shutdown/restart and PHY power-down
handoff. Oscillator readiness and cycle progression are modeled, not measured.
The foreground coordinator audit connects scripted scan/battery/LED boundaries
to the actual application, USB class and GUI decoder. It covers remapping,
MIDI, capture loss, unsupported storage actions, session reset, stale input,
independent timer/sequence wrap and release draining before explicit restart.
It does not measure real acquisition or foreground execution time.

## Automated checks

| Layer | Coverage |
| --- | --- |
| Native C/Python | Shared defaults/alternate-initializer builds, lifecycle and architecture, NKRO, Schmitt/velocity, menus, MIDI, parallel calibration, complete storage snapshots, GUI/SysEx transport, device-flashing adapters, current-only rules and capture framing |
| Linked Cortex-M4 execution | M1 clock waits/failures, wired rail ordering, ADC rank/bank/DMA configuration, battery sampling, SPI LED/radio handling and coexistence, USB power-down guards/timeout, RTC setup/backup preservation and scripted sleep/resume; no analog timing, physical wake or radio-peer simulation |
| Linked Cortex-M33 execution | SDK startup/USB/DMA/I2C paths, descriptor/control transfers, MIDI packets, LED writes, faults, MIDI SysEx, updater entry and storage integration |
| Original-reference comparison | Selected scan/editor/lighting behavior and flash register transactions; the original fixture is separate and read-only |
| Tk against simulated MIDI | Real widgets, configuration ACK/readback, capture isolation, flashing-tab actions/confirmation, bounds and timeout handling, resolved typography (antialiased or native-pixel bitmap faces) and a settings panel that scrolls in a small window; no keyboard opened |
| Sphinx | All public pages build, internal references resolve, source links exist; warnings fail CI |

`python3 tools/run_tests.py` runs 23 audit groups, including native Huntsman
and M1 tests, with a 300-second total deadline. It requires the optional original
reference and Python/Tk dependencies. Missing dependencies are failures, not
silent skips. The M1 group also compiles the HAL/services against the pinned
Artery SDK. Ordinary builds and native tests do not need that reference.

Storage tests cover all supported layouts, settings/calibration preservation,
keyboard mappings including disabled/international/modifier destinations,
MIDI+Jankó boot restoration, missing/corrupt saves, fallback generations,
unsupported-schema rejection, controller faults and all 512 byte-cut points
of an inactive-page write. The model rejects writes outside the two tail slots.

## Physical checks

The current Huntsman image builds and passes linked ARM execution audits, but
the remapping, 30-byte HID and MTG3/SysEx version 3 integration has not been flashed or tested
on physical hardware. Offline USB tests are not evidence of physical
enumeration, acquisition cadence or host/DAW interoperability.

Remapping tests exercise every accepted usage, duplicate-source ownership,
held-key edits, immutable Fn shortcuts, GUI dropdown/ACK/readback, and compiled
ARM save/reboot. These are offline tests, not a physical unplug/replug check.

M1 physical access is limited to its vendor identity query: ID2949, factory
v4.08, USB high speed. No M1 configuration, bootloader-entry or flash commands
have been sent. There is no installable M1 image.

Velocity uses each board's declared scan rate, not USB delivery timestamps.
The GUI's measured capture throughput does not establish the acquisition rate.
Huntsman flash tests restrict writes to the two custom tail pages, but modeled
flash behavior does not establish electrical power-loss recovery. No reset of
a user's calibration is part of validation.

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
