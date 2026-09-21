# Validation and limitations

This is the common evidence boundary for the complete `huntsman` build. Do not treat modeled hardware as a physical test.
Commands and dependencies are in [Building](BUILDING.md).
The separate [M1 backend](MONSGEEK_M1.md#verification-limits) has a physically
checked identity query and IAP transfer, official-SDK ARM library/application builds, and native
82-key application/scanner plus GUI tests. Native tests also cover Fn transport
controls, USB-only MIDI, the battery curve/filter, sleep-request policy and radio packet codec.
Linked Cortex-M4 tests execute clock, wired/battery cold-start, battery-input, USB power-down, RTC sleep,
scan, LED and SPI3 radio HALs with official SDK drivers; register effects and completion
events are modeled. These do not validate an installable
M1 firmware or peripheral timing.
Scanner pause/resume tests cut acquisition at every bank boundary, reject
one-shot/invalid contexts and latched faults, discard unread/battery samples,
preserve IRQ masks and ADC configuration, and require a fresh complete frame
after restart. Register guards prohibit power-rail and unrelated-DMA changes;
ADC settling and resumed sample accuracy still require physical validation.
Timebase tests execute SDK TMR2 setup with scripted counter progression through
masked intervals, counter/millisecond wrap and suspend/resume gaps. Clock and
sleep transitions reject a running timebase. Tests verify fractional carry,
context/ownership rejection and latched faults, not real elapsed-time accuracy.
The composite USB audit also drives real SDK endpoint/control routines through
shared SysEx into the 82-key GUI decoder, including configuration ACK/readback,
bounded control requests, ownership, suspend and session reset at both USB speeds.
It executes the SDK hardware initializer and reset IRQ, with deferred attachment,
bounded clock/counter/reset/flush failures, shutdown/restart and PHY power-down
handoff. Oscillator readiness and cycle progression are modeled, not measured.
The foreground coordinator audit connects scripted scan/battery/LED boundaries
to the actual application, USB class and GUI decoder. It covers remapping,
MIDI, capture loss, unsupported storage actions, session reset, stale input,
independent timer/sequence wrap and release draining before explicit restart.
It also connects the application to the real radio scheduler and SPI/DMA driver
for all three Bluetooth slots and 2.4 GHz: mapped key press/release, duplicate
destinations, backpressure, battery metadata, USB-only performance MIDI and GUI
resets independent of wireless key ownership. Fn transport tests script the
external host-release/selection callbacks but still require actual local drain
and matching peer readiness; ambiguous selection fails closed. Those callbacks
are an explicit evidence gap, not an implementation of physical switching.
It does not measure real acquisition or foreground execution time.
Wireless tests execute the actual foreground report scheduler and SPI/DMA HAL
with scripted peer status and completion. They cover mode gates, paired report
ownership, neutral startup, stale/invalid replies and failures; native tests
cover list/bitmap mapping and rollover. These do not simulate a radio host or
prove delivery, pairing, report aggregation or complete transport switching.
The same ARM audit composes battery filtering and idle/critical policy with
radio metadata and sleep-control transfers: latest-only percentages, neutral
gates, cancellation boundaries, retention escalation, both-DMA/SPI completion,
timeout rejection and quiet handoff. It does not establish peer sleep or execute
a complete battery-powered startup/sleep/wake cycle.
Sleep-GPIO tests execute ordered SDK pin changes and switch reads, preserving
unrelated pins and the reference's untouched latch. They check quiescence,
battery/USB ownership exclusion and restoration after cable arrival. This is
register-level evidence, not electrical charging or wake validation.
Factory-calibration tests decode synthetic rank-major pages for all 82 keys,
checking trailer flags, range/span boundaries, unused-cell exclusion and
all-or-nothing publication. The ARM reader accesses only the two records'
data/trailers in read-only mapped flash, rejects busy/invalid context and preserves
IRQ masking. Foreground tests load those fixtures before exercising USB/radio
and GUI telemetry. No physical factory data, checksum or travel accuracy has
been verified; invalid records are not repaired or erased.
Cold-start tests compose the actual RTC/PHY/GPIO/scanner code, checking rail
order, measured RTC/TMR2 handoff, fresh settling timestamps, complete warmup-frame discard, timer wrap,
scan/PHY/clock failures, cancellation and cable changes. No startup helper is
stubbed; hardware readiness, ADC data and the WFI wake boundary are scripted.
The cold application-handoff audit composes these HALs with actual USB startup,
radio initialization and shared application binding for both power sources and
all five transports. It checks wired USB attachment before scanner initialization, fresh radio pulse
timestamps, counter wrap, IRQ-mask preservation, factory-bounds rejection,
source/USB/time/scan faults and terminal cleanup. Profile reads are modeled as
erased and writes rejected; it does not execute a reset vector, real enumeration,
peer delivery, post-handoff runtime power management or physical persistence.
The cold-start diagnostic test executes the compiled SysEx service with abstract
USB callbacks: build handshake, failure text, rejected mutations, boot-flag-gated
reset requests, USB-generation invalidation and handoff to the live owner. A GUI
test confirms that a failure log cannot be mistaken for valid scan telemetry.
Sleep-time tests script rates across the supported LICK range and verify
coherent SDK reads, protected shadow synchronization, independent wraps,
fractional carry, early-wake deltas and terminal clock/time faults. They do not
measure physical oscillator rate, drift or sleep duration.
The development-ELF audit checks exact boot identity, IRQ routes, all physical
load ranges, SRAM SDK/custom writer placement and stack/BSS bounds. It rejects
mutated protected-region/vector fixtures and executes the actual reset from
poisoned RAM to verify copies, BSS clearing, interrupt masks and MSP selection.
Its main-loop tests stub component calls; they are not full-image peripheral
execution, a stack high-water measurement or installation approval.

## Automated checks

| Layer | Coverage |
| --- | --- |
| Native C/Python | Shared defaults/alternate-initializer builds, lifecycle and architecture, NKRO, Schmitt/velocity, menus, MIDI, parallel calibration, complete storage snapshots, GUI/SysEx transport, device-flashing adapters, current-only rules and capture framing |
| Linked Cortex-M4 execution | M1 clock waits/failures, wired/battery cold-start rail ordering, ADC rank/bank/DMA configuration, battery sampling, SPI LED/radio handling and coexistence, USB power-down guards/timeout, RTC setup/backup preservation and scripted sleep/resume; no analog timing, physical wake or radio-peer simulation |
| Linked Cortex-M33 execution | SDK startup/USB/DMA/I2C paths, descriptor/control transfers, MIDI packets, LED writes, faults, MIDI SysEx, updater entry and storage integration |
| Original-reference comparison | Selected scan/editor/lighting behavior and flash register transactions; the original fixture is separate and read-only |
| Tk against simulated MIDI | Real widgets, configuration ACK/readback, capture isolation, flashing-tab actions/confirmation, bounds and timeout handling, resolved typography (antialiased or native-pixel bitmap faces) and a settings panel that scrolls in a small window; no keyboard opened |
| Sphinx | All public pages build, internal references resolve, source links exist; warnings fail CI |

`python3 tools/run_tests.py` runs 27 audit groups, including native Huntsman
and M1 tests, with a 300-second total deadline. It requires the optional original
reference and Python/Tk dependencies. Missing dependencies are failures, not
silent skips. The M1 group also compiles the HAL/services against the pinned
Artery SDK. Ordinary builds and native tests do not need that reference.

Shared application tests cover deferred calibration saves: retries preserve
active bounds and suppress keyboard output; only verified completion publishes
the candidate. Failure, invalid readings, stale scans, cancellation, layout
changes and inactivity timeout discard it without further save attempts.

Storage tests cover all supported layouts, settings/calibration preservation,
keyboard mappings including disabled/international/modifier destinations,
MIDI+Jankó boot restoration, missing/corrupt saves, fallback generations,
unsupported-schema rejection, controller faults and all 512 byte-cut points
of a Huntsman inactive-page write. The controller model rejects writes outside
the two tail slots. M1 runs the same shared-journal native suite for all 82 keys
and 2048 byte-cut points using memory callbacks, including board identity and
error-code isolation. Its separate linked ARM journal/SDK writer audit models
controller effects and rejects out-of-slot writes, invalid geometry/context,
unsafe activity and failed blank/program verification. It checks RAM-only
transactions, temporary exception vectors, reboot fallback, fault latching and
stuck-busy fail-stop. Foreground tests separately script profile I/O and the
owner gate, checking pending status, stable neutral autosave, no-change wear,
restart restoration, failure latching and terminal gate/resume failure at both USB
packet sizes. Parallel calibration tests cover all 82 endpoints, GUI/Fn+C entry,
deferred completion while keys remain held, whole-profile restore, intentional
scan gaps, cancellation, timeout, USB/acquisition loss and failed write/gate/resume.
The GUI widget audit checks capability flags and disables new calibration after
a storage fault. The save-gate audit executes real scanner/time/battery/LED HALs,
checking low/stale/changing power, local drain, retained rails/links, masked
pause ownership, elapsed-time limits and fresh resume; transport readiness and
electrical values are scripted. Physical pause/drain scheduling, flash timing, power qualification
and hardware persistence remain unverified.

## Physical checks

The current Huntsman image builds and passes linked ARM execution audits, but
the remapping, 30-byte HID and MTG3/SysEx version 3 integration has not been flashed or tested
on physical hardware. Offline USB tests are not evidence of physical
enumeration, acquisition cadence or host/DAW interoperability.

Remapping tests exercise every accepted usage, duplicate-source ownership,
held-key edits, immutable Fn shortcuts, GUI dropdown/ACK/readback, and compiled
ARM save/reboot. These are offline tests, not a physical unplug/replug check.

M1 hardware checks confirm ID2949/v4.08 factory identity, guarded bootloader
entry, and an application transfer accepted by the bootloader's checksum and
per-byte readback verdict. The custom application does not yet enumerate on
USB; kernel messages show the bootloader disconnect without an application
return. The early recovery-flag writer passes the SRAM/SDK model, but its
execution on the device and reset-to-IAP recovery remain unconfirmed.
The early USB/diagnostic path has not yet been tested on the physical keyboard.
No custom keyboard, MIDI, lighting, calibration or wireless behavior is claimed
physically working. See [experimental flashing](DEVICE_FLASHING.md#monsgeek-m1-experimental-conversion).

Velocity uses each board's declared scan rate, not USB delivery timestamps.
The GUI's measured capture throughput does not establish the acquisition rate.
Huntsman flash tests restrict writes to the two custom tail pages, but modeled
flash behavior does not establish electrical power-loss recovery. No reset of
a user's calibration is part of validation.

## Not established

- Electrical power-cut recovery, flash endurance and physical unplug/replug
  restoration in MIDI+Jankó mode; automated boot/interruption tests cover logic.
- Calibrated distance, force or velocity in physical units.
- Physical LED color/animation timing, worst-case interrupt/stack margins,
  sustained 8 kHz acquisition, USB certification or comprehensive DAW support.
- Full stock-firmware feature equivalence, factory restoration from an
  incomplete dump, or compatibility with unported keyboards/MCUs.

Do not reset a user's calibration just to test recovery. Keep dumps and their
error maps outside version control; unreadable flash bytes are not backups.
