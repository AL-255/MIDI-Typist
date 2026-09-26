# M1 V5 TMR handoff

The M1 application is experimental and not qualified for daily use. Work is on
`feature/m1-v5-tmr`; `main` remains Huntsman-only. The default M1 build is
USB-only; `-DMT_M1_WIRELESS=ON` includes Bluetooth and 2.4 GHz support.

The connected M1 currently runs the wireless-enabled diagnostic build
`4a10d91`. It is in USB-accessible fail-stop after a Bluetooth slot-2 pairing
attempt. The retained fault is `radio=0x01040004`: the peer was in state 4
(pairing/searching), and a 500 ms gap in status replies was treated as fatal.
The current source separates the short output-freshness gate from a longer
pairing watchdog, and requires a neutral report before resumed input. That fix
has passed linked-ARM tests but has **not yet been flashed or tested physically**.

Bluetooth slot 1 paired with this computer and delivered three A key-down/up
pairs to the Bluetooth HID device. Short Fn+F5 returned to USB and delivered a
fresh A key-down/up pair. This does not establish other Bluetooth slots, 2.4 GHz,
sleep/wake, battery behavior, or long-term scan timing. The factory updater
erases custom profile and calibration slots on every flash; factory calibration
pages and bootloader code must remain untouched.

Next checkpoint: flash the guarded application-only build from the clean source
commit, repeat slot-2 pairing while watching the USB failure log, then verify
key delivery and Fn+F5 return. If pairing succeeds, exercise 2.4 GHz separately
with a receiver and verify power/cable transitions before broadening release
claims. See the [M1 design](docs/MONSGEEK_M1.md),
[validation limits](docs/VALIDATION.md), and
[flash procedure](docs/M1_TEST_CHECKLIST.md).
