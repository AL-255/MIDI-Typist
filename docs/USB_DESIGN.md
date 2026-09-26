# Huntsman USB integration

The complete `huntsman` application exposes NKRO HID, two-cable USB-MIDI 1.0 and the
updater control HID at VID:PID `1532:02b0`. It uses unmodified NXP USB
classes, DCI/IP3511 and peripheral drivers; integration wrappers are
project-owned platform code in [nxp_lpc55](../firmware/platform/nxp_lpc55).
Descriptors and updater integration live in the
[Huntsman board port](../firmware/boards/huntsman_v3_pro_mini).
The shared application uses send callbacks and has no USB/SDK dependency.
New MCUs provide their own stack and descriptors; see [porting](PORTING.md).

| Interface | Function |
| --- | --- |
| 0 | Report-only NKRO keyboard HID |
| 1, 2 | MIDI audio control and streaming |
| 3 | Control-only updater HID, 90-byte reports |

Cable 0 is performance MIDI; cable 1 is GUI control. An audio IAD groups
interfaces 1 and 2. The configuration is 184 bytes with two embedded jack
associations per endpoint. See [SysEx framing](TELEMETRY.md#sysex-envelope).

The keyboard report is 30 bytes: eight modifier bits (E0…E7), one reserved
byte, a 220-bit bitmap for keyboard/keypad usages 04…DF, and four padding bits.
Physical-key remapping happens in the shared application before submission;
the USB driver only transmits the completed report.

## Memory and control-transfer safety

USB SRAM is Device memory: unaligned halfword/word accesses are unsafe.
Application compilation uses `-mno-unaligned-access`, and `__wrap_memcpy`
uses words only when both pointers are aligned, followed by byte tails.
Compiler flags alone cannot change prebuilt libc assembly.

Send-busy flags are set before submission. Interrupt masking spans state
checks, buffer preparation and submission; pending USB buffers remain
immutable until completion. Standard configuration/interface queries support
all four alternate-zero interfaces. The keyboard advertises report-only HID,
not an unimplemented boot-keyboard protocol.

Updater boot-entry reset is deferred until the accepted command's EP0 IN
zero-length status completion, followed by 20 ms. A new SETUP or bus reset
before acknowledgment cancels the request. A later unrelated completion
cannot extend the deadline. Entry does not require a subsequent GET_REPORT.
The supplied updater programs only the application region.

## Board startup

The application follows the original 96 MHz clock branch and supplies the
recovered 16 MHz reference to NXP USB PLL/PHY initialization. It does not add
the external-system-clock prerequisite from other original clock branches.
The PHY workaround preserves the reference's frame-read, disconnect,
FORCE_FS/change-bit and reconnect write order. CTIMER3 has priority 0 and USB
priority 1. Register-sequence tests do not establish oscillator or analog PHY
timing.

## Validation

Use `audit-usb` as documented in [BUILDING.md](BUILDING.md). It executes linked
Cortex-M33 startup, vector dispatch, clocks, descriptors, full/high-speed
control and endpoint paths, reset deferral and aligned copies with modeled
peripherals. It does not execute the bootloader or model electrical reconnect.

The complete image builds and passes modeled USB, SysEx, remapping and storage
audits. Its 30-byte HID/MTG4 contract has not been flashed or physically tested.
Updater HID remains interface 3. Custom writes remain restricted to the two
tail pages.
See [validation status](VALIDATION.md).
These checks are not USB certification or proof of all MIDI/DAW behavior.
