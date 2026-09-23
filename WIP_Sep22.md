# M1 V5 TMR handoff — September 22, 2026

## Status

The M1 port is functional over USB but **not complete or qualified for daily use**.
Work is on `feature/m1-v5-tmr`; `main` has not been changed.

- Installed firmware on the connected keyboard: `b9a6845` (USB-only build,
  `state=clean`), verified over the control port and bound to the physical USB
  device through the ALSA/sysfs check rather than a port name. The user flashed it
  and reported that typing works but sensor calibration does not.
- Diagnosis of that report (read-only session, no settings/flash/boot changes):
  three separate defects, all now fixed and offline-verified in this checkpoint.
  1. **A full acquisition queue stopped the whole application.** The keyboard
     latched a terminal runtime fault (`detail=1`, scanner reason `7` = queue
     overflow) about 14 s after start, with `runtime stats` showing a 40 441 µs
     storage stage and a 40 507 µs loop against a 32-frame (4 ms) queue. The
     application then answered `unsupported command` to every live command, which
     is why keys stopped registering and calibration timed out. A full queue is
     now a counted frame loss: the oldest frame is dropped, the sequence gap is
     reported as a scan loss, and acquisition continues.
  2. **The factory calibration records were read in the wrong domain**, so the
     importer rejected all 82 keys and the board silently fell back to a uniform
     700-count provisional span. The reference stores counts with three extra low
     bits (8× the 12-bit acquisition domain); after shifting, all 82 released
     records match a live released frame within 26 counts and imply 129–930
     counts of travel per key (mean 717).
  3. **The custom calibration press requirement was far too shallow.** A uniform
     128-count requirement is ~5 % of travel, so a light or mid-travel hold was
     accepted as a bottom-out and the reading tracked the finger instead of the
     bottom stop; the hold never completed and the 5 s inactivity abort fired
     ("stuck at amber"). The requirement is now the larger of that drop and five
     eighths of the key's recorded travel span, so a mid-travel hold cannot start
     a measurement and small-span keys still reach their requirement.
- The redistributable artifact remains USB-only: `MT_M1_WIRELESS` (default `OFF`)
  compiles the unverified Bluetooth/2.4 GHz stack out entirely. Wireless work
  needs `-DMT_M1_WIRELESS=ON` and is documented as unqualified.
- Offline state after this checkpoint: native host CTest 18/18, native M1 CTest
  5/5, all nine M1 linked-ARM audits pass in the default USB-only tree, and the
  M1 HAL/live/image audits pass in a `-DMT_M1_WIRELESS=ON` tree. The Huntsman
  ARM audits for calibration, menu and mode pass unchanged (its policy still uses
  the fractional press criterion, so its behaviour is untouched).

## What is verified

- Guarded application flashing has bootloader checksum/readback confirmation.
  Custom USB HID/MIDI, build identity, GUI telemetry and sensor/bounds readback
  have been exercised on hardware.
- A full 82-key calibration was completed on an earlier image and retrieved.
  Private diagnostic files remain excluded from Git. Released bounds: mean
  **2625.67**, population standard deviation **38.63**. Bottom-out bounds: mean
  **1751.76**, population standard deviation **31.02**. These are native sensor
  counts, not normalized control values.
- Live read-only measurements on the connected unit: all 82 keys neutral at rest
  with electrical rest levels 2508–2701 and control readings 4049–4096; factory
  travel spans 129–930 counts; a fresh settings-only journal advances its
  generation after a GUI threshold edit and restore, without claiming calibration.
- Native M1 tests now cover the imported factory domain (including rejection of
  unscaled records), the per-key depth requirement, and queue overflow as a loss;
  the HAL audit overfills the queue and compares delivered sequence numbers with
  the producer count.
- Physical key presses were not possible while the user was away, so the fixes
  above have **not** been re-verified on hardware. Nothing beyond `b9a6845` has
  been flashed; an early startup fault still needs a debugger.

## Remaining work

1. Reflash this checkpoint (the bootloader erases both custom profile slots) and
   re-run the calibration row of `docs/M1_TEST_CHECKLIST.md`: the GUI must now
   report saved calibration from the factory records; if a custom calibration is
   run, press each key fully to its bottom stop and hold it until green.
2. Validate the rest of the build on hardware: ordinary typing, MIDI chords,
   velocity, aftertouch, wheels, sustain, Fn menus and saved-state reboot, with
   and without a performance MIDI reader. Check advancing scan sequences and
   fault counters, and read `runtime stats` after any flash save.
3. Confirm the queue-loss behaviour under a deliberate foreground stall and
   confirm that a scan **loss** (not a fault) is what appears in telemetry.
4. Physically verify Fn+F1–F3 Bluetooth slots/pairing, Fn+F4 2.4 GHz, Fn+F5 USB,
   host delivery, disconnect/reconnect and USB-only MIDI gating (wireless build).
5. Finish and physically qualify power management: charging-pin meaning, battery
   indication, low/critical protection, idle sleep, wake and cable transitions.
   PB10 remains labelled raw high/low, not charging/full.
6. Cable edges after a transport hardware callback or during PHY transition can
   still fail closed. This needs a physical cable session; there is no offline
   model of PHY timing.
7. GUI knob remapping and transport-selection persistence are not implemented.
   Both need a board-owned persisted field in the shared journal, so start them
   as their own checkpoints. Do not describe the port as feature-complete.

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
- Never probe the destructive boot-entry command and never treat a friendly MIDI
  name as device identity. Continue committing/pushing validated checkpoints on
  the feature branch; keep original firmware/disassembly read-only and out of Git.
  The GUI remains the only supported PC application.

See [building](docs/BUILDING.md), [M1 implementation](docs/MONSGEEK_M1.md),
[flashing](docs/DEVICE_FLASHING.md) and [validation limits](docs/VALIDATION.md).
