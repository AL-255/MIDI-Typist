# M1 V5 TMR handoff

The M1 application is experimental and not qualified for daily use. Work is on
`feature/m1-v5-tmr`; `main` remains Huntsman-only. The default M1 build is
USB-only; `-DMT_M1_WIRELESS=ON` includes Bluetooth and 2.4 GHz support.

The latest wireless build gates host output after 500 ms without radio status,
keeps polling an offline peer, and permits battery search/idle sleep. On a
valid-clock battery fault it stops scan/output, clears the known sensor and LED
rails, and requests a normal reset after two stable USB-power samples. Native,
linked-ARM and runtime-power tests pass; this build has not been installed on a
physical M1. Battery current, sleep/wake, cable recovery, reconnection and
long-term scan timing therefore remain unverified. The factory updater erases
custom profile and calibration slots on every flash; factory calibration pages
and bootloader code must remain untouched.

Next checkpoint: exercise physical power/cable transitions before broadening
release claims.
See the [M1 design](docs/MONSGEEK_M1.md),
[validation limits](docs/VALIDATION.md), and
[flash procedure](docs/M1_TEST_CHECKLIST.md).
