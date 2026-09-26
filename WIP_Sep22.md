# M1 V5 TMR handoff — updated September 26, 2026

## Current status

The Monsgeek M1 V5 TMR port is **work in progress**, not qualified for daily use.
The active branch is `feature/m1-v5-tmr`; `main` remains Huntsman-only.
The connected M1 was reflashed with the clean USB-only image `1c3dd52`.
The factory bootloader accepted its checksum and flash readback; the device
re-enumerated on the same port and reported that embedded Git identity.
The user confirms the Fn+R confirmation and Y action work on this image.
Typing, MIDI performance, save/reboot, power and wireless behavior have not
been rechecked on it.
After about 144 seconds of operation, a read-only `runtime stats` reply showed
scan sequence 1,154,143, HAL errors 0, queue depth 1/peak 4 of 32, and three
loss/gap events. The frame and store stage maxima were about 40.8 and 40.4 ms,
consistent with a guarded flash-save path. Its scanner pause means this alone is
not evidence of a queue overflow. This is one snapshot, not a load soak.

The default M1 build is USB-only (`MT_M1_WIRELESS=OFF`). Bluetooth and 2.4 GHz
support can be compiled in, but remain physically unqualified. The guarded
flasher validates image bounds and readback. It does not prove that the new
application will boot or scan correctly.
The clean USB-only application build passes the flasher's offline `custom`
image check (header, vectors, padded size and profile boundary). The connected
M1 completed the guarded flash and boot-identity check.
The M1 flash worker now rejects missing elevated/raw-USB access before sending
either boot-entry command; this guard is verified offline, not on hardware.

## Known blocker

A prior on-device runtime fault reported `detail=0x00000001`,
`scan=0x00000007` after a scan queue overflow. The current source retains the
newest 32 complete frames when the foreground falls behind, counts dropped
frames and exposes a sequence gap. The application invalidates ordinary input
until neutral and explicitly faults lossless capture. ADC, bank-order and DMA
faults still stop the scanner. This recovery is installed and has offline tests,
but has **not** been shown to resolve the underlying foreground stall on hardware.
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
diagnostic, not an importable backup. The reflash erased the custom slots;
recalibration is required before claiming restored calibrated behavior.

## Next safe checkpoint

1. Compare repeated `runtime stats` samples during pressed-key and MIDI-chord
   operation. Track queue peak, error/loss deltas and foreground stage maxima;
   confirm overflow recovery and explicit loss reporting under an actual load.
2. Test ordinary typing, MIDI input, other Fn menus, saved-state reboot and live
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
