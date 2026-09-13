# Huntsman USB integration

The complete `huntsman` application exposes NKRO HID, USB-MIDI 1.0, CDC ACM and the
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
| 4, 5 | CDC control and data |

## Memory and control-transfer safety

USB SRAM is Device memory: unaligned halfword/word accesses are unsafe.
Application compilation uses `-mno-unaligned-access`, and `__wrap_memcpy`
uses words only when both pointers are aligned, followed by byte tails.
Compiler flags alone cannot change prebuilt libc assembly.

Send-busy flags are set before submission. Interrupt masking spans state
checks, buffer preparation and submission; pending USB buffers remain
immutable until completion. Standard configuration/interface queries support
all six alternate-zero interfaces. The keyboard advertises report-only HID,
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

The latest complete image passes computer-initiated updater entry, application-only
flashing, high-speed USB return and live CDC telemetry. Full application
readback matches the binary. Custom tail pages hold settings; the separate
Razer settings/serial region remains unchanged.
See [validation status](VALIDATION.md).
These checks are not USB certification or proof of all MIDI/DAW behavior.
