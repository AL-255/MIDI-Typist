# M1 V5 TMR handoff — updated September 25, 2026

## Current status

The Monsgeek M1 V5 TMR port is **work in progress**, not qualified for daily use.
The active branch is `feature/m1-v5-tmr`; `main` remains Huntsman-only.
The last user-confirmed working image was `ab4b048`: USB keyboard, MIDI notes,
Jankó and Fn+V worked. Later source changes have not been flashed or confirmed
on hardware. No device flash was performed during this handoff.

The default M1 build is USB-only (`MT_M1_WIRELESS=OFF`). Bluetooth and 2.4 GHz
support can be compiled in, but remain physically unqualified. The guarded
flasher validates image bounds and readback. It does not prove that the new
application will boot or scan correctly.

## Known blocker

A prior on-device runtime fault reported `detail=0x00000001`,
`scan=0x00000007` after a scan queue overflow. The current source retains the
newest 32 complete frames when the foreground falls behind, counts dropped
frames and exposes a sequence gap. The application invalidates ordinary input
until neutral and explicitly faults lossless capture. ADC, bank-order and DMA
faults still stop the scanner. This recovery has offline tests but has **not**
been flashed or shown to resolve the underlying foreground stall on hardware.

Calibration and flash persistence also require further hardware validation.
The 82-key calibration previously retrieved from hardware had released mean
**2625.67** (population SD **38.63**) and bottom-out mean **1751.76**
(population SD **31.02**) in native sensor counts. The factory bootloader
erases the custom profile/calibration slots during reflash; this export is
diagnostic, not an importable backup. Recalibration is expected after flashing.

## Next safe checkpoint

1. Investigate the foreground stall responsible for scan queue overflow.
   Confirm the recovery and explicit loss report on hardware.
2. Build the default USB-only firmware and run the relevant M1 audits. Record
   the exact Git build identity in the artifact and commit/push the checkpoint.
3. Flash only after validating application bounds and bootloader readback;
   test ordinary typing, MIDI input, Fn menus, saved-state reboot and live
   scan/fault counters on hardware. Repeat calibration if the bootloader erased
   it. Physical power, radio and cable-edge behavior remain separate work.

Preserve the bootloader and factory flash. Custom profiles occupy only
`0x08027000` and `0x08027800`; the application ends below `0x08027000`.
Factory settings begin at `0x08028000`, with factory calibration pages at
`0x08032000` and `0x08032800`. Keep the original firmware/disassembly read-only
and excluded from Git. The GUI is the supported PC application, communicating
through USB MIDI SysEx rather than CDC.

Further details: [M1 board](docs/MONSGEEK_M1.md),
[validation limits](docs/VALIDATION.md),
[flash procedure](docs/M1_TEST_CHECKLIST.md).
