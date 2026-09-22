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
Encoder tests cover the SDK-free quadrature/button decoder and the M1 SDK GPIO
adapter, periodic timer binding, bounded event queue, explicit overflow, pause
invalidation, held-button restart suppression and IRQ-mask preservation. The
private reference audit executes the original sampler to check PC10/PC12 order
and both legal electrical cycles. These tests do not establish physical detent
orientation, switch bounce characteristics or host consumer-report delivery.
The knob integration test runs actual GPIO reads, the decoder, pulse owner and
USB/radio output paths with scripted samples. It covers volume/mute press/release,
simultaneous event ordering, backpressure, Fn suppression, scan-loss cancellation
and transport neutrality. USB tests cover the independent consumer descriptor,
GET_REPORT, SET_IDLE, endpoint halt and reset. Radio subtype-3 payload order and
checksum are checked against executed private-reference instructions.
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
resets independent of wireless key ownership. Fn transport tests execute the
runtime selection owner, local neutral-report drain, SPI mode requests and fresh
matching peer status for every transport. They cover returning from an unpaired
slot, unavailable USB, and release-before-rearm after a host becomes ready.
Peer responses and transfer completion are scripted; local drain is not proof
that a remote host received the release. Ambiguous selection fails closed.
It does not measure real acquisition or foreground execution time.
The same compiled M1 binding returns portable power-status ACKs during GUI
streaming, with unknown and critical-battery fixtures. Python/Tk tests cover
field validation, raw charger labels, capable-board polling, stale status,
capture-time exclusion and malformed-reply disconnection. These tests do not
establish electrical charger polarity or battery-percentage accuracy.
Cancelling an in-flight power query on disconnect leaves the previous reading
unchanged and closes without a false malformed-reply error.
Power-handoff tests also check scan consumption during prolonged output drain,
never-linked neutral cancellation and a completed critical-sleep control packet
without an RF host. Restore tests require fresh matching peer state, admit a
searching peer, and reject replay of keys held through wake and host connection.
The installed-image runtime-power audit executes the real controller, policy,
wake filter and SDK GPIO writes with scripted live/HAL boundaries and elapsed
time. It checks drain-before-pause, black-frame completion, command 5 retention
and command 3 shutdown, GPIO/PHY ownership, periodic wake scans with continuous
sequence, key/encoder wake, retained/full radio restoration, external-power and
activity inhibition, and terminal stage/clock/time failures. This is sequencing
evidence, not physical radio sleep, battery life or charger validation.
Awake source-transition tests execute the installed controller with scripted
HAL completions: all wireless modes, USB-to-last-wireless fallback, debounce
and time wrap, paused PHY/RTC setup, bounded failures and runtime dispatch.
The linked live/USB test separately checks explicit endpoint abandonment,
wireless neutral drain, restoration before USB enumeration, preserved unsaved
settings and no held-key replay. Sleep-source tests inject arrival at every
drain/blank/peer/wake-scan/restoration stage, including retention escalation,
queued-versus-in-flight cancellation, a transient arrival and races inside
PHY/GPIO/sleep guards. They check restoration before USB attachment and no extra
WFI after an observed arrival. Physical cable transitions remain unverified;
edges during PHY mutation or an Fn transport switch still fail closed.
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
and GUI telemetry. Physical records have been read and preservation checked;
their out-of-domain representation, checksum and travel accuracy are not
established. Invalid records are not repaired or erased.
Cold-start tests compose the actual RTC/PHY/GPIO/scanner code, checking rail
order, measured RTC/TMR2 handoff, fresh settling timestamps, complete warmup-frame discard, timer wrap,
scan/PHY/clock failures, cancellation and cable changes. No startup helper is
stubbed; hardware readiness, ADC data and the WFI wake boundary are scripted.
The cold application-handoff audit composes these HALs with actual USB startup,
radio initialization and shared application binding for both power sources and
all five transports. It checks wired USB attachment before scanner initialization, fresh radio pulse
timestamps, counter wrap, IRQ-mask preservation, factory-bounds rejection and
explicit real-frame provisional bootstrap,
source/USB/time/scan faults and terminal cleanup. Profile reads are modeled as
erased and writes rejected; it does not execute a reset vector, real enumeration,
peer delivery, post-handoff runtime power management or physical persistence.
The cold-start diagnostic test executes the compiled SysEx service with abstract
USB callbacks: build handshake, failure text, rejected mutations, boot-flag-gated
reset requests, USB-generation invalidation and handoff to the live owner. Runtime
fault tests check neutral HID, MIDI cleanup and retained software-IAP access. A GUI
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
exact polling-cache comparisons of every global and per-key setting, edit
reversion, retained debounce time and cache invalidation after a verified save,
restart restoration, failure latching and terminal gate/resume failure at both USB
packet sizes. Parallel calibration tests cover all 82 endpoints, GUI/Fn+C entry,
deferred completion while keys remain held, whole-profile restore, intentional
scan gaps, cancellation, timeout, USB/acquisition loss and failed write/gate/resume.
The GUI widget audit checks capability flags and disables new calibration after
a storage fault. The save-gate audit executes real scanner/time/battery/LED HALs,
checking low/stale/changing power, local drain, retained rails/links, masked
pause ownership, elapsed-time limits and fresh resume. Boundary-injection tests
change USB readiness, supply/rail state, clocks and DMA after preflight; they
check unchanged deferral/retry or terminal rejection, and retain existing
overrun detection. Clock preflight is checked to enter without an outer IRQ
mask. Instruction traces are not physical latency measurements; transport
readiness and electrical values are scripted. Physical pause/drain scheduling, flash timing, power qualification
and hardware persistence remain unverified.

## Physical checks

Host MIDI lifecycle tests inject open/send failure, process exit, a hung send,
a hung close, and native/IPC queue overflow. They verify no command retry,
error-before-queued-data handling, child reaping, reconnect and the flashing
release guard. They do not model USB device behavior.

The current Huntsman image builds and passes linked ARM execution audits, but
the remapping, 30-byte HID and MTG4/SysEx version 3 integration has not been flashed or tested
on physical hardware. Offline USB tests are not evidence of physical
enumeration, acquisition cadence or host/DAW interoperability.

Remapping tests exercise every accepted usage, duplicate-source ownership,
held-key edits, immutable Fn shortcuts, GUI dropdown/ACK/readback, and compiled
ARM save/reboot. These are offline tests, not a physical unplug/replug check.

M1 hardware checks confirm ID2949/v4.08 factory identity, guarded bootloader
entry, and an application transfer accepted by the bootloader's checksum and
per-byte readback verdict. Custom USB enumerates at 480 Mb/s with HID and MIDI;
the control port returns the embedded Git identity and live 82-key GUI snapshots.
The process-isolated GUI transport has completed repeated live connect/stream/
disconnect cycles without retaining native workers. The GUI flasher's read-only
USB-bound identity handshake also completes and releases its MIDI owner.
The real Tk GUI has displayed the M1's 82-key layout alongside repeated
power-status replies on USB: external source, estimated percentage and explicitly
unverified raw charger state. Full-rate capture excludes power polling and
returns to fresh GUI/power readback without new scanner or lighting errors;
a five-second check delivered approximately 40,000 consecutive selected-key samples. These
checks validate the readback path, not battery capacity, charger polarity or
battery-powered sleep. Factory calibration-field readback remains unchanged.
Released-key M1 capture delivered approximately 80,000 consecutive samples over
ten seconds without sequence gaps or new scan errors, then returned to fresh GUI
snapshots. A further five-second GUI check received 152 snapshots, with all 82
keys released, control readings 3961–4096, and no light errors. This is a bounded
idle-input check, not qualification of pressed-key/polyphonic load, worst-case
latency or long-term stability. Queue overflow remains fail-stop; diagnostic USB
and guarded recovery remain available after a runtime scan fault.
Power-cycle and software-requested recovery return to the factory bootloader.
Read-only factory diagnostics return valid markers but resting values outside
the 12-bit import domain. The complete 518-byte calibration-field readback remains
unchanged after flashing and automatic settings-only initialization. The live
journal verifies settings-only saves without marking provisional calibration
as saved. A GUI threshold edit and restoration each advanced the journal
generation after ACK/readback; the original threshold was restored. This is not
a physical power-loss durability test. Scan/storage transition stability remains
under investigation; retained diagnostics distinguish storage errors and first
scanner fault causes. A combined fault is not evidence of an enumeration failure
or permission to retry flash writes automatically.
All 82 released-key readings in the first complete
scan are midrange, without near-zero positions, and all six DMA banks retain
15 slots before triggering. The selector is independently checked against
executed private reference instructions. The original startup RAM fallback is
also instruction-checked. Provisional travel normalization permits neutral
arming; no stock calibration scale conversion is inferred. The matching GUI
connection receives MTG4 telemetry and reports USB ready. With all keys released,
a live check received periodic fresh snapshots with released-key readings near
the top of the normalized range; all 82 keys stayed up. Input rearmed after each
intentional settings-save pause, with no light errors. Intentional save pauses
are included in the scan-gap counter; this GUI check
does not independently measure the declared 8 kHz acquisition rate. Queue order/wrap/overflow,
pause invalidation, and released-key fast-path velocity semantics have offline
tests. The per-key pending-strike bitmap is checked against every occupied slot
through the native MIDI suite, including retrigger and overflow cleanup. These
checks do not establish sustained pressed-key/polyphonic
performance or worst-case latency. A physical A/S check observed eight separate
A presses followed by a simultaneous A+S hold. GUI telemetry showed the matching
NKRO report bits, independent releases and separate per-key velocity captures,
with no unexpected down keys or new scanner/lighting errors. Both readings
returned to 4096 after release; fully pressed readings reached 1 in the provisional
control coordinates. This approximately 30 Hz snapshot check validates these two
physical-key bindings and firmware report contents, not every acquisition sample,
OS input delivery, electrical calibration or measured velocity accuracy. Mapping
of the remaining keys, full calibration, LED appearance and wireless behavior
still require hardware checks.
The live encoder diagnostic reports phase 3 (both phases high), button released,
and zero movement, invalid transitions or queue overflows with the knob untouched.
Its sampling counter advances alongside periodic acquisition. Rotation/button
presses have not been physically exercised. The installed USB configuration has
four interfaces; Linux binds interface 3 as a consumer input with mute and both
volume capabilities. This establishes descriptor recognition, not physical
knob-to-volume delivery or wireless host behavior.
See [experimental flashing](DEVICE_FLASHING.md#monsgeek-m1-experimental-conversion).

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
