# M1 V5 TMR handoff — September 22, 2026

## Status

The M1 port is functional over USB but **not complete or qualified for daily use**.
Work is on `feature/m1-v5-tmr`; `main` has not been changed.

- Last user-confirmed installed firmware: `ab4b048`. The user confirms MIDI
  notes, Jankó and Fn+V now work. This is not a worst-case timing qualification.
- Latest firmware-code checkpoint: `9c6db9b` plus the M1 profile-RESET work on
  top of it. It includes system-menu observation ownership, directional battery
  filtering, safe cancellation of an Fn transport selection that has not yet
  called the hardware adapter, a fix for Fn+Tab/Fn+Caps editor entry that the
  observation-only change had silently broken, and M1 custom-profile RESET.
  None of it has been flashed to the confirmed working keyboard.
- The observation-only change also left two audits red and one M1 HAL guard set
  stale at handoff; both are corrected and the whole offline suite passes again.
- The incomplete broader cable-bounce experiment was discarded at handoff.
  No experimental changes from it remain in the source.
- The previously used USB path `3-2.1` was absent at handoff. Rediscover and
  verify the device's SysEx build identity before any further hardware action.

## What is verified

- Guarded application flashing has bootloader checksum/readback confirmation.
  Custom USB HID/MIDI, build identity, GUI telemetry and sensor/bounds readback
  have been exercised on hardware.
- A full 82-key calibration was completed and retrieved. Private diagnostic
  files remain excluded from Git. Released bounds: mean **2625.67**, population
  standard deviation **38.63**. Bottom-out bounds: mean **1751.76**, population
  standard deviation **31.02**. These are native sensor counts, not normalized
  control values, and are not evidence that the installed image retains them.
- The Fn+V fault involved repeated engine rearm/reset work while a modal menu
  owned input. Menus now observe scans without rearming performance input;
  leaving a menu requires fresh neutral input. The user confirms the fix works.
- M1 custom-profile RESET (Fn+R, or `cfg clean` from the GUI) erases only the two
  custom pages, blank-verifies each erase, returns defaults with the factory
  electrical bounds and refuses a denied gate or failed erase. It is verified
  offline only; no reset has been performed on hardware.
- Native M1 tests, complete application builds, the linked-ARM audits and strict
  Sphinx documentation builds pass for the retained changes: 31/31 offline audit
  groups. Linked-ARM tests model peripheral completions; they
  do not prove physical timing, radio delivery or electrical behavior.

## Remaining work

1. Validate the combined latest build on hardware: ordinary typing, MIDI chords,
   velocity, aftertouch, wheels, sustain, Fn menus and saved-state reboot, with
   and without a performance MIDI reader. Check advancing scan sequences and
   fault counters, not merely USB enumeration.
2. Physically verify Fn+F1–F3 Bluetooth slots/pairing, Fn+F4 2.4 GHz, Fn+F5 USB,
   host delivery, disconnect/reconnect and USB-only MIDI gating.
3. Finish and physically qualify power management: charging-pin meaning,
   battery indication, low/critical protection, idle sleep, wake and cable
   transitions. PB10 remains labelled raw high/low, not charging/full.
4. Cable edges after a transport hardware callback or during PHY transition
   can still fail closed. Handle these ownership transitions without hiding
   genuine peripheral faults, retrying ambiguous operations or losing RAM state.
5. GUI knob remapping and transport-selection persistence are not implemented.
   Do not describe the port as feature-complete.

## Safety and continuation

- Preserve bootloader, factory settings and factory calibration. Custom profiles
  use only `0x08027000` and `0x08027800`; the application image must end below
  `0x08027000`. Factory settings start at `0x08028000`, calibration pages at
  `0x08032000` and `0x08032800`.
- The factory bootloader erases custom profile/calibration slots when reflashing.
  Private bounds exports are diagnostic, **not importable backups**. Expect
  recalibration after a flash; do not claim saved bounds were restored.
- Normal boot leaves the IAP flag blank. Explicit update writes only verified
  magic at `0x08004800` after guarded shutdown. Early startup faults can require
  a debugger; power cycling is not guaranteed recovery.
- Continue committing/pushing validated checkpoints on the feature branch.
  Keep original firmware/disassembly read-only and out of Git. The GUI remains
  the only supported PC application; configuration uses MIDI SysEx, not CDC.

See [building](docs/BUILDING.md), [M1 implementation](docs/MONSGEEK_M1.md),
[flashing](docs/DEVICE_FLASHING.md) and [validation limits](docs/VALIDATION.md).
