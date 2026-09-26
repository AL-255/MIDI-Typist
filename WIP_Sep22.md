# M1 V5 TMR handoff

The M1 application is experimental and not qualified for daily use. Work is on
`feature/m1-v5-tmr`; `main` remains Huntsman-only. The default M1 build is
USB-only; `-DMT_M1_WIRELESS=ON` includes Bluetooth and 2.4 GHz support.

The connected M1 runs the clean wireless-enabled build `b3410b1`. The current
source further changes status-only silence to an offline condition instead of
a fatal watchdog: output is still gated after 500 ms without status, while
queries continue and the battery search/idle policy can proceed. This change
has not yet been flashed or physically tested. The current source also stops
the known sensor/LED rails on a valid-clock battery fault and waits for two
stable USB-power samples before a normal reset; this is linked-ARM tested but
its physical current draw and recovery are not yet measured.
Bluetooth slots 2 and 3 paired and delivered A key-down/up pairs on their own
host HID devices; slot 2 survived the status gap. Linked-ARM wireless and
runtime-power tests pass.

Bluetooth slot 1 paired with this computer and delivered three A key-down/up
pairs to the Bluetooth HID device. The 2.4 GHz receiver delivered three A
key-down/up pairs, and short Fn+F5 returned the device to USB from each tested
wireless mode. This does not establish sleep/wake, battery behavior, or
long-term scan timing. The factory updater
erases custom profile and calibration slots on every flash; factory calibration
pages and bootloader code must remain untouched.

Next checkpoint: exercise physical power/cable transitions before broadening
release claims.
See the [M1 design](docs/MONSGEEK_M1.md),
[validation limits](docs/VALIDATION.md), and
[flash procedure](docs/M1_TEST_CHECKLIST.md).
