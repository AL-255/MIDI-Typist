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
The clean USB-only application build passes the flasher's offline `custom`
image check (header, vectors, padded size and profile boundary). No M1 is
currently enumerated for the hardware half of that check.

## Known blocker

A prior on-device runtime fault reported `detail=0x00000001`,
`scan=0x00000007` after a scan queue overflow. The current source retains the
newest 32 complete frames when the foreground falls behind, counts dropped
frames and exposes a sequence gap. The application invalidates ordinary input
until neutral and explicitly faults lossless capture. ADC, bank-order and DMA
faults still stop the scanner. This recovery has offline tests but has **not**
been flashed or shown to resolve the underlying foreground stall on hardware.
The flash-save owner pauses acquisition before its long write, so a long
`TIMING_STORE` stage alone does not establish that the write filled the queue.
The foreground linked-ARM check covers loss detection even when the next frame
sequence appears consecutive; physical timing is still unmeasured.
The current `runtime stats` response includes queue depth and its peak since
scanner initialization, allowing the next hardware run to detect near-overflow
before an error is counted.

Calibration and flash persistence also require further hardware validation.
The 82-key calibration previously retrieved from hardware had released mean
**2625.67** (population SD **38.63**) and bottom-out mean **1751.76**
(population SD **31.02**) in native sensor counts. The factory bootloader
erases the custom profile/calibration slots during reflash; this export is
diagnostic, not an importable backup. Recalibration is expected after flashing.

## Next safe checkpoint

1. With the M1 connected, verify its USB-bound firmware identity, then use the
   guarded updater once and require the bootloader's checksum/readback verdict.
2. Read `runtime stats` during idle and pressed-key operation. Compare queue
   peak, error/loss counts and foreground stage maxima to locate the stall;
   confirm overflow recovery and explicit loss reporting on hardware.
3. Test ordinary typing, MIDI input, Fn menus, saved-state reboot and live
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
