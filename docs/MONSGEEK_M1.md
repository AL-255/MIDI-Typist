# MonsGeek M1 V5 TMR

The M1 backend contains scan, lighting, radio-transfer, battery-input, composite USB,
USB power-down and RTC sleep HALs, clock and wired/battery cold-start
components, an 82-key application library, a wireless report scheduler,
transport-menu and power-policy components, an experimental application image,
and matching GUI geometry. The GUI verifies internal model **ID2949** before
offering [factory conversion](DEVICE_FLASHING.md#monsgeek-m1-experimental-conversion).
Flashing, recovery and live 82-key high-speed USB telemetry are hardware-checked.
Bluetooth slot 1 pairing, a three-press A key delivery on its host input device,
and Fn+F5 return to USB with fresh key delivery are hardware-checked. Other
Bluetooth slots, 2.4 GHz report delivery and wireless power remain unqualified.
Released-key acquisition is hardware-checked with GUI telemetry active and an
8 kHz timer configuration. The user confirms working MIDI notes, Jankó and Fn+V;
worst-case pressed-key timing and full-layout operation remain unqualified.
This is not a daily-use build.
The Huntsman image must never be installed on this keyboard. Wireless
receiver operation and other MonsGeek models are not implemented. Complete
Bluetooth/2.4 GHz operation and power management are not yet qualified for use.
Automatic battery sleep/wake, cable-arrival restoration and explicit wireless
pairing requests are linked; physical wireless/power qualification remains unfinished.

## Wireless build switch

`MT_M1_WIRELESS` (CMake option, default `OFF`) decides whether the unverified
wireless feature exists in an artifact at all. The default build links
`m1_wireless_off.c` instead of the radio HAL and peer scheduler, so no SPI3
transfer, pairing request, peer report, peer battery metadata or radio sleep
transaction is present in the image; that build is the one intended for
redistribution. `-DMT_M1_WIRELESS=ON` produces the development/audit build that
contains the full wireless stack described below.

A USB-only artifact still starts, scans, lights, stores settings and sleeps:

- Boot selects USB for both power sources; a wireless boot transport is refused
  before any peripheral is touched. On battery the device has no host at all, so
  the idle policy sleeps it after the unselected limit instead of waiting for a
  link that cannot exist.
- `Fn+F1`–`Fn+F5`, GUI transport selection and pairing are refused, and the
  capability is reported once as telemetry `transport_flags` bit 8 (USB-only).
  The GUI labels such a device USB-only and replaces its Fn+F1–F5 guidance.
- Power handoff still parks live, blanks the LEDs, takes exclusive GPIO
  ownership, reduces the USB PHY and restores; the radio handoff has no peer to
  signal, so the sequence uses plain sleep and never claims a retained Bluetooth
  link. The no-radio stub answers "healthy, idle, no host", which is why the
  owner's health checks pass without a fault.
- The storage quiescence gate asks the radio module whether its bus is idle, so
  a build without that module never inspects the unused peripheral.

## Identify a keyboard

1. Connect the keyboard by USB in wired, normal application mode.
2. Open the GUI's **Device flashing** tab and select **MonsGeek M1 V5 TMR
   (experimental)**.
3. Refresh, then choose **Read firmware details…**. Linux may request permission
   to open the vendor HID interface.

Refresh only reads Linux device metadata. Explicit inspection sends the vendor
identity query and requires ID2949 before showing **FACTORY FIRMWARE · ID
VERIFIED**. The USB revision is shown separately from the queried firmware
version. An unavailable serial is not synthesized from the USB location.
Inspection alone leaves factory configuration untouched. Installation is a
separate, explicitly confirmed action that resets stock settings. For a custom
application, inspection verifies its embedded build target over a MIDI control
port bound to the selected physical USB device, then offers reflash/restoration.
A pre-existing shared bootloader PID remains unidentified and cannot be flashed
directly through the GUI.

## Board and GUI layout

`firmware/boards/monsgeek_m1_v5_tmr/include/m1_keys.def` is the shared C/Python
source for the **82 analog keys**. Compact sensor IDs follow physical rows;
they are neither ADC channel numbers nor factory calibration indices.
The mapping is inferred from the primary mapping at `0x08023BEC`, the
state-update handler at `0x08011F74`, and LED coordinates at `0x08025A3D`.
Only wiring/usage facts are retained, not the original packed tables or code.

| Bank | Compact sensors | Keys |
| --- | --- | --- |
| 0 | 0–13 | Esc, F1–F12, Delete |
| 1 | 14–28 | Grave, number row, Backspace, Home |
| 2 | 29–43 | Tab, Q row, backslash, Page Up |
| 3 | 44–57 | Caps, A row, Enter, Page Down |
| 4 | 58–71 | Left Shift, Z row, Right Shift, Up, End |
| 5 | 72–81 | Left Ctrl/GUI/Alt, Space, Right Alt, Fn, Right Ctrl, Left/Down/Right |

ADC ranks are the fifteen conversion positions in each bank. Factory records
use `rank * 6 + bank`; DMA rows are bank-major. Eight of the 90 ADC positions
are not keys. Fn is bank 5/rank 10. Standard HID modifier usages distinguish
Left GUI (`E3`) from Left Alt (`E2`); the stock alternate-OS remapping is not
mistaken for the physical key order. The 82-LED chain snakes across successive
rows. All key/HID/Fn and LED bindings have been checked against the reference.
Keycap widths use ANSI conventions; case gaps in the drawing are schematic.

The PB9/PB8/PB7 selector patterns for banks 0…5 are `6, 0, 4, 2, 1, 3`.
These are checked by executing the private reference selector at `0x080132B4`,
including its byte-indexed TBB and actual GPIO helpers. Reconstructed prose or
halfword-formatted hex dumps are not substitutes for that instruction check.

The rotary encoder is a separate digital input (PC10/PC12, button PC11), not
an 83rd analog sensor. Its HAL samples at the periodic scanner's cadence and
publishes bounded digital events. The foreground maps positive/negative cycles
to volume up/down and button presses to mute, sending consumer-control pulses
over the selected USB or wireless transport. The electrical direction has not
been confirmed as physically clockwise/counterclockwise. GUI knob remapping is
not implemented yet. The read-only `runtime encoder` diagnostic is documented in
[Telemetry](TELEMETRY.md#m1-digital-encoder).

The shared SDK-free `keyboard_encoder` decoder accepts consecutive stable
phase samples, rejects two-bit jumps, and emits one event per complete
quadrature cycle. Electrical sequence `0,1,3,2,0` is positive; physical clockwise
orientation is not established. The original sampler's PC10/PC12 order and both
legal cycle directions are checked by executing its instructions. Custom
debouncing requires consecutive samples, rather than the reference's accumulated
mismatch counter. Button debounce is time-scaled from the board scan rate.
Starting or resuming discards partial turns and requires release of a held button.
Knob events do not participate in analog calibration or velocity calculation.

The ISR-to-foreground queue never wraps over unread events: overflow discards
the queue and latches an auxiliary fault until explicit reinitialization.
That fault releases the current consumer action without stopping analog acquisition.
The portable `keyboard_aux` owner serializes press/release pulses under endpoint
backpressure, including simultaneous turn/button events. Pulse duration is
`AUX_PULSE_MS`; the button emits once per press, not repeatedly while held.
Fn menus, calibration, invalid input, offline hosts and transport changes discard
queued/partial movement and require a new released-button baseline. Releases
must be locally drained before switching hosts, saving or parking peripherals.
Pausing/stopping acquisition also discards queued knob events. Debounce, capacity
and default usages are defined in `defaults.h`; the board's
`config/auxmap.def` binds them separately from analog-key mappings.

Preview the board without opening hardware:

```sh
python tools/keyboard_gui.py --demo --board MG-M1V5TMR
```

The GUI reads the same key table, sizes its canvas for six rows, and binds
profiles to a board target and layout. It never applies a Huntsman profile to
M1. Shared MTG4 telemetry, SysEx services and the GUI support all 82 keys,
including per-key capture. Native C-to-Python and mocked Tk tests exercise this
path; the experimental application supplies live USB snapshots.

## Scanner HAL and application libraries

The HAL uses the pinned official Artery ADC, DMA, GPIO, CRM and timer drivers.
It owns only ADC/mux pins and TMR3/TMR6/DMA1 channel 6. Its caller must establish
the 216 MHz clock and sensor power first. Incompatible clocks or an ADC
calibration timeout fail initialization; startup is not hidden inside the HAL.

TMR6 schedules a requested **8 kHz complete-frame cadence**. Each acquisition
chains six TMR3-triggered rows. DMA completes before selecting the next bank;
each rearm disables ADC DMA requests and drains the old result/status before
enabling the new destination and reconnecting the producer. Pretrigger DMA
counts remain available through cold-start diagnostics to detect stale transfers.
Complete acquisitions enter a bounded `M1_SCAN_QUEUE_FRAMES` FIFO (32 frames,
4 ms at the declared rate), preserving order through short foreground delays.
On overflow, the oldest complete frame is discarded and the newest retained;
the resulting sequence gap invalidates keyboard input until neutral, and
lossless capture reports failure rather than silently skipping a sample.
Invalid banks, ADC values and DMA completions still fault the scanner. Pause/stop
discard queued frames; startup resumes acquisition only after the application
has loaded its profile. Battery telemetry uses the newest complete acquisition.
The shared released-key fast path still copies and validates every sample;
it skips edge/velocity work only after all keys are released and no fit is pending.
During held chords, individual unchanged keys also skip velocity work once their
fit is closed and any release arming has been recorded; active fits still receive
every sample. MIDI pending-slot bitmaps avoid rescanning empty strike slots.
Read-only [`runtime stats`](TELEMETRY.md#m1-foreground-timing) measures foreground
wall time, including interrupts, without changing the timer configuration.
Only a complete, correctly ordered frame is published. Raw ADC `0…4095` maps
to canonical `1…4096`, decreasing with travel. ADC rails are not per-key travel
calibration. A short IRQ critical section protects the latest complete-frame
copy. DMA errors, invalid samples and cadence overruns fail the scanner rather
than silently changing velocity's timebase. This cadence is not measured yet.

The shared application is compiled with 82-key storage and the M1 board
callbacks. Native tests exercise all keys, simultaneous NKRO, the Fn/MIDI menu,
LED permutation, normalization, incomplete/out-of-order rows and frame drops.
The ARM build produces static libraries, emulator-only audit ELFs and an
development ELF and an application-only `.bin` for experimental conversion.
See [build commands](BUILDING.md#monsgeek-m1-development-build).

`m1_hal_capture_start(now_us)` uses the same ADC/DMA path for one complete
six-bank wake-check frame, without starting the periodic timer. Completion
stops ADC triggering and disables ADC, while retaining configuration for the
next capture. The caller consumes the complete frame before starting another
capture or periodic scanning. Busy/unconsumed frames are never overwritten.
The mandatory foreground service enforces `M1_WAKE_SCAN_TIMEOUT_US` (2000 µs).
Timeout, DMA error or invalid sample invalidates acquisition and requires init.
Clocks, sensor power and settling remain caller responsibilities. A one-shot
frame is not an 8 kHz sample stream and must not feed normal velocity fitting.

`m1_hal_pause()` / `m1_hal_resume()` provide a foreground-only pause for
periodic scanning. Pause stops ADC, both timers and the owned DMA channel;
partial/unread frames and battery readings are discarded. Resume retains ADC
configuration/calibration, checks the original clocks and starts a fresh
bank-zero frame after one full timer period. Neither operation changes power
rails. A latched acquisition fault still requires reinitialization. One-shot
captures cannot be paused, and ordinary start/capture cannot bypass a pause.
The owner must invalidate input/velocity state and report a capture gap; scan
sequence numbers count only completed acquisitions. This primitive does not
alone provide flash power/drain/timekeeping. The
[save gate](DEVICE_CONFIG_STORAGE.md#m1-application-tail-backend) composes these
checks without changing the radio/USB links or power rails.

## Lighting HAL

`m1_lighting_encode` converts the shared application's LED-chain RGB frame
into 1968 bytes: green, red, blue; MSB first; `0xF0` for a set bit and
`0xC0` for a clear bit. These encoding/wiring facts were checked at reference
entries `0x08007010`, `0x08017C28` and `0x080150BC`. The custom application
supplies final RGB brightness; it does not copy the stock renderer's channel
scaling, effect code or color tables.

The official SPI driver configures SPI2 half-duplex transmit on PA10/AF5:
8 bits, master, MSB first, low polarity, second edge, APB1 divided by 16.
At the required 108 MHz APB1 clock this requests 6.75 MHz SPI and approximately
2.33 ms per frame. DMA1 channel 1 uses TX request 13, byte widths, memory
increment, non-looping mode and high priority. Scanner channel 6 is independent.
These are configuration calculations, not measured output timings.

`m1_lighting_init(now_us)` checks clocks and holds PA10 low, without starting
DMA. The caller owns LED supply sequencing on PB13. Only the serialized
foreground owner calls init, offer, service and stop; no lighting ISR is needed.
An accepted offer encodes into private DMA storage. Busy offers do not modify
that storage. The foreground service waits for DMA completion, TX-data-empty
and SPI-not-busy before detaching SPI and driving PA10 low. It then waits
`M1_LED_LATCH_US` (1000 µs by default) before accepting another frame.

DMA/master-mode errors or `M1_LED_TRANSFER_TIMEOUT_US` (10000 µs) stop output,
hold the data line low and require explicit reinitialization. Stop cancels the
transfer; it does not promise to turn off already latched LEDs. To blank LEDs,
send a zero-RGB frame and complete its latch interval before removing power.
No driver operation changes supply, radio, bootloader or flash state. Electrical
output, color balance and the board's LED current budget still need verification
before integrating power control or enabling full-brightness hardware operation.

Native tests cover every byte value in every color component on all 82 LEDs.
The synthetic-address `m1_hal_audit.elf` executes the actual Cortex-M4 HAL and
official SDK under Unicorn. Tests cover DMA buffer ownership, GPIO/SPI setup,
drain/latch ordering, timeouts, errors, wraparound, ADC ranks, six-bank frames
and scanner/lighting coexistence. DMA flags, calibration completion and time
are scripted; these tests do not emulate analog conversion or LED waveforms.
The register model follows the [Artery reference manual](https://www.arterychip.com/download/RM/RM_AT32F402_405_EN_V2.01.pdf),
including DMA flag-clear semantics (§9.5.2).

## Identity protocol

See the [power and transport section](#power-and-transport-components) for the
custom application's control policy; this identity protocol does not execute it.

`tools/flash_monsgeek.py` owns the protocol and adapter; it is not a standalone
application. The shared [GUI worker](DEVICE_FLASHING.md#adapter-design-and-tests)
handles privilege separation.

| Field | Contract |
| --- | --- |
| Normal USB candidates | `3151:5030`; reference alternate `38ee:0033` |
| Bootloader candidate | `3151:502a`, shared between products; model unverified |
| Vendor interface | HID interface 2; the keyboard interfaces are not opened |
| Feature report | 64 bytes, unnumbered; Linux hidraw adds a leading zero byte |
| Request | Payload byte 0 = `0x8F`; byte 7 makes the first eight bytes sum to `0xFF` modulo 256; other bytes zero |
| Reply | Payload byte 0 = `0x8F`; bytes 1–2 = little-endian model ID; bytes 7–8 = little-endian BCD firmware version |

The reply's version occupies the request checksum field, so the request checksum
rule does **not** apply to the reply. Inspection uses one SET_FEATURE and one
GET_FEATURE with no automatic retry. Device tokens bind the physical port and
USB enumeration; missing, changed or ambiguous candidates fail closed. USB
VID/PID alone never verifies the model. Bootloader candidates receive no query.

## Board boundaries from the reference

The independently authored adapter uses behavioral facts from the ID2949/v4.10
reference. The reference remains read-only and is not distributed here. Its
recovered implementation and binary are not part of the custom build.

| Area | Reference contract |
| --- | --- |
| MCU | AT32F405, Cortex-M4; exact physical density/package still needs verification |
| Application | Header `0x08005000`, vectors `0x08005200` |
| Boot erase range | `[0x08005000, 0x08028000)`, 70 pages of 2048 bytes |
| Custom profiles | `0x08027000/0x08027800`, reserved inside that application range; erased by bootloader reflashing |
| Boot request flag | `0x08004800`; not custom profile storage |
| Factory calibration | 2048-byte records at `0x08032000` and `0x08032800` |
| Factory key types | Record at `0x08033000`; preserve it |
| Clock | 12 MHz external oscillator; reference core 216 MHz, APB1 108 MHz, APB2 216 MHz, USB 48 MHz |
| Sensor acquisition | ADC1, DMA1 channel 6; six banks, 15 conversions per bank, 21-cell logical row stride |

Entering the factory bootloader is **destructive before an image is sent**:
the application entry command erases settings, and the bootloader erases its
application range before USB enumeration. Do not use bootloader entry as an
identity or connectivity test. Huntsman storage addresses are not portable to
it. M1's distinct `M1P2` journal uses its two reserved application-tail pages,
not the stock settings/calibration area. Native tests cover all 82 keys and
every byte-cut point; an ARM audit executes the official SDK and SRAM writer
against a controller model. Foreground restore/autosave uses the journal, with
writing gated by explicit outer-owner safety callbacks. See [device storage](DEVICE_CONFIG_STORAGE.md#m1-application-tail-backend)
for the reservation, power/quiescence gate, RAM execution and update-loss contract.

An M1 firmware port still requires verified startup/power behavior, physical
confirmation of the inferred key/sensor/LED mapping, USB clock/PHY and runtime binding,
physical verification of factory calibration, live storage scheduling and an independently checked
application update path. In particular, the six-bank acquisition is not the
Huntsman optical-ASIC path. Shared telemetry is count-aware; each board retains
its own buffer budget. See the [porting contract](PORTING.md).

### Read-only factory calibration

`m1_factory_load` imports the two calibration records without unlocking or
writing flash. The schema comes from the loader at `0x0800F814` and the save
paths at `0x0800D3A8`/`0x0800D4FC`: each page begins with 126 little-endian
halfwords indexed by `rank * 6 + bank`, followed by a saved flag at byte 2045
and marker bytes `55 aa` at 2046–2047. The upper page contains startup resting
baselines; the lower page contains floors. Only the 82 mapped key cells become
application bounds; unused, battery and extra logical cells are not keys.

Both records must have valid markers and saved flag 1. Every mapped resting
baseline must be within the reference's native 1000–4000 range; both endpoints
must be ADC-representable and span at least the M1 calibration minimum (128).
Bounds receive the same native-to-canonical conversion as scans (`ADC + 1`).
The decoder stages the entire result before publishing it. Bad markers, absent
calibration, wrapped/reversed/narrow pairs, busy flash or invalid execution
context leave the previous output unchanged. The reader preserves the interrupt
mask and touches only 252 data bytes plus three trailer bytes per page.
The tested keyboard has valid record markers but values outside this importer's
ADC domain. They are rejected, not rescaled or overwritten. The
[cold-start diagnostics](TELEMETRY.md#cold-start-failure-reporting) expose those
fixed fields and the first actual scan for investigating the representation.

This is a conservative import, not the stock calibration algorithm: it does not
repair records, import nonlinear vendor curves, initialize the key-type page or erase anything.
The factory schema has no verified checksum; plausible in-range corruption
cannot be detected by marker/range checks alone. These bounds drive custom
linear lighting/aftertouch, not a claim of physical millimetres.

Foreground initialization gives validated custom calibration precedence. If
neither saved source is usable, an explicit real released startup frame permits
**provisional RAM bounds**: current electrical ADC+1 as upper, upper minus 700
as lower. Keep every key released at startup. Raw readings outside native
1000–4000 reject the fallback. The private-reference audit executes the original
validity/fallback instructions at `0x08005D68..0x08005E86`; this supports the RAM
policy, not a claim of measured travel or compatibility of out-of-range records.
Context/busy read failures still reject startup.

The GUI's saved-calibration flag stays clear for provisional bounds. Settings
autosave does not promote them to calibration. A full Fn+C/GUI calibration is
needed for measured endpoints; it accepts a stable electrical drop of at least
128 counts for one second, independently for each key. User-held partial travel
can still yield partial bounds: fully depress each key. Storage callbacks are
required for calibration entry, and only verified saves replace active bounds.

The shared application's layout policy maps each key's electrical bounds to
control values **4096 released / 1 pressed**, clipping at the endpoints. Thresholds,
velocity, wheels, menus, lighting, aftertouch, GUI readouts and captures use this
control domain; calibration and saved endpoints retain electrical ADC+1 values.
Huntsman's input policy is unchanged. These linear coordinates are not millimetres.
The shared travel converter validates and normalizes each frame in one pass,
skipping division for clipped endpoints while retaining exact integer rounding.
Any invalid sample or endpoint invalidates the whole frame.

## Power and transport components

The experimental image binds the Fn transport owner to the radio HAL/scheduler.
Switching and routing have offline integration tests; **physical delivery is
verified only for Bluetooth slot 1**. The [runtime battery controller](#runtime-battery-sleepwake)
coordinates automatic sleep/wake and cable changes, including restoration when
USB power arrives during battery sleep.
`m1_controls_bind` attaches board-specific input/lighting hooks to the shared
application. Its table defines these Fn controls; bare F1–F5 remain normal keys:

| Chord | Software policy |
| --- | --- |
| Fn+F1 / F2 / F3 | Bluetooth slot 1 / 2 / 3 |
| Fn+F4 | 2.4 GHz |
| Hold Fn+F1 / F2 / F3 / F4 for 3 seconds, then release | Request pairing for that Bluetooth slot / 2.4 GHz |
| Fn+F5 | USB |
| Fn+Enter | Keyboard/MIDI toggle on USB only; unavailable on wireless |
| Fn+Space | Battery bar on number keys while held; amber Space means unavailable |

Transport names preview while held and selection starts on release. The old
transport must accept a neutral keyboard report, finish MIDI cleanup when
leaving USB, and finish local output transfers. Radio switching additionally
requires neutral committed/staged reports and no outstanding SPI transaction.
An unsent neutral baseline on an unpaired slot may be cancelled. The protocol
does not provide an old-host receipt acknowledgement; local completion must not
be described as one. The owner then sends mode opcode `93` and waits for a fresh
matching status reply. All selections, including USB mode 6, use that reference
packet contract (`0x080180AE`); the mode branch is instruction-checked against
the private image. Short presses never send a pairing command. Vendor forwarding
and peer-update commands are not implemented.
The USB control link stays connected while keyboard output uses wireless.

A matching mode selects the slot even if it is unpaired. State 3 separately
permits reports; Fn menus remain usable while waiting. A newly eligible host
requires neutral input, so offline-held keys cannot be replayed. Fn+F5 is not
offered without ready USB. An attempted switch that times out or loses scan
integrity is terminal, retaining wired diagnostics when available; it does not
silently resume on an ambiguous host. Cable transitions belong to the separate
power-source owner. Transport selection is not yet persisted.

For explicit pairing, keep Fn+F1–F4 held until the preview changes to `PAIR BT1`,
`PAIR BT2`, `PAIR BT3` or `PAIR RF`, then release the combination and all keys.
This can replace the selected slot's existing bond; use a short press to select
an already paired host. The three-second hold is a custom default, not a measured
factory timing. Fn+F5 and Fn+Space never request pairing.

After the neutral handoff and target-mode confirmation, the owner sends one
control `94` packet. The reference branches at `0x080180FE` / `0x0801819A`
establish the radio payload `[0, 1]` and Bluetooth's 33-byte payload: control 2,
12-byte name length, name, then zero fill. The custom names are `MIDI-Typist1`
through `MIDI-Typist3`; no factory name bytes are included. Both DMA directions
and SPI drain must finish before requesting fresh mode status. No automatic
pairing retry or intervening mode resend occurs. The complete transition has a
six-second deadline; the pairing phase has a three-second deadline.

The GUI shows **pairing requested / searching** during the radio request or
peer state 4. Neither a completed command nor matching mode proves a paired
host. State 3 separately permits reports, starting with a fresh neutral report
and released physical keys. Actual advertising names, bond replacement, receiver
other Bluetooth slots and 2.4 GHz host delivery still require hardware verification.

Wireless forces keyboard mode, including a restored MIDI setting. The shared
menu hides the MIDI entry hint. USB selection does not automatically enable MIDI.
Native tests use mock transport callbacks to verify all five choices, normal
F-key reports, release ordering, MIDI gating, battery display and failures.

### Battery acquisition and policy

The reference battery consumer at `0x080172F8` reads native ADC counts at
`0x20009134`. The DMA base is `0x2000905A`, with a 42-byte row stride,
placing this sample at **bank 5, rank 4**
(ADC channel 0), an unused key position. The scanner publishes this sample with
the same completed-frame sequence as the keys, without their `+1` normalization.
Partial frames never publish a new battery sample.

`m1_battery_hal` uses official SDK GPIO calls to configure **PB10 and PC13 as
pull-up inputs only**. It rate-limits fresh completed samples, rejects duplicates,
and invalidates status on scanner faults or stale acquisition. It does not drive
a charging-enable pin. PC13 low selects the externally powered branch in the
reference. PB10 is debounced and exposed as **pin high / pin low**: its electrical
charging-versus-complete meaning still needs verification. No GUI charging label
or completed-charge claim is fabricated from that signal.

The board's piecewise ADC-to-percentage curve is tested against the recovered
arithmetic for all 4096 inputs: 1145 or below maps to 1%, 1280 to 20%, 1704 to
99%, and 1705 or above to 100%. These are ADC characterization points, not
millivolts. The custom filter averages eight fresh samples, then applies a
half-weight running average. Display changes require ten consecutive filtered
estimates above the displayed level on external power, or below it on battery;
the tenth commits the latest estimate. Estimates need not be identical. Returning
to the displayed level or crossing it in the opposite direction resets the
counter. Cable changes restart the filter. Unlike the
stock special case, an empty reading on external power is not presented as 100%.
Filter timing and display intensity are explicit defaults, not claims of exact
stock scheduler timing.

On battery, at **20% or below**, the overlay suppresses ordinary backlighting
and flashes physical LED78 (logical left Alt). Fn+Space shows one number-row
segment per ten percent, rounded upward; unknown remains distinct from empty.
At **5% or below**, the power policy latches a critical condition until external
power returns. These thresholds and all timing/filter tunables live in
`defaults.h`.

The GUI reads the same cached battery/source state through the portable
[`power status` reply](TELEMETRY.md#power-status). Its percentage is an estimate;
PB10 is labelled raw low/high rather than charging/full until polarity is verified.
Reading status neither changes power policy nor writes flash.

`m1_power` implements the idle/critical request policy from `0x080178D8`, with
separate connected-host limits for Bluetooth and 2.4 GHz. Limits count qualified
periodic service steps, **not scan frames or milliseconds**. Zero disables
ordinary connected-host idle sleep without bypassing critical protection.
Activity cancels ordinary idle; USB/external power cancels pending sleep.
The policy stages radio control payload 5 for ordinary connected Bluetooth
sleep, or 3 for the other paths. It grants sleep eligibility only after that
radio transaction is committed, host reports are drained, scanning is stopped,
LEDs are off, SPI is idle and USB power is quiescent. It does not itself execute
RTC sleep or power-rail writes.
Critical battery escalation replaces a pending retention command 5 with command
3 and invalidates any earlier retention completion. A completed command 5 must
not authorize deeper sleep while command 3 is still pending.

### Radio transfer HAL

`m1_radio` provides a bounded SPI3 transfer HAL and a portable packet codec,
not a complete radio controller. Wiring and framing were checked against
reference entries `0x080186C0`, `0x0801887C`, `0x08018914`, `0x08017FDC`
and `0x08017E6C`. No peer firmware, recovered code or packet buffers are bundled.

The official SDK configures PB3/PB4/PB5 as AF6, PA15 as software select,
and PD2 as an active-low data-ready input. SPI3 is full-duplex, master,
8-bit MSB-first, low polarity/second edge, APB1 divided by 16. At the required
108 MHz APB1 clock this requests 6.75 MHz. Init holds PA15 low for
`M1_RADIO_START_PULSE_US` (10000 µs), then raises it without blocking service.
It does not establish peer power or change sensor/LED supplies.

An accepted exchange copies into private storage and arms RX DMA1 channel 3
(request 14) before asserting select and starting TX channel 2 (request 15).
Both channels, TX-data-empty and SPI-not-busy must complete before select rises.
The caller must consume the received bytes before another exchange can start;
busy offers cannot overwrite an outstanding reply. All calls belong to one
serialized foreground owner. DMA/SPI errors or
`M1_RADIO_TRANSFER_TIMEOUT_US` (10000 µs) stop the transfer and require explicit
reinitialization. Stop is an abort, not a delivery guarantee.

The codec emits only reviewed runtime opcodes: reports (`81`), battery (`90`),
status request (`92`), mode (`93`) and control (`94`). Payloads are 1–65 bytes;
the checksum sums payload bytes only, and DMA length rounds header, payload
and checksum upward to four bytes with zero padding. Poll (`09`) is a separate
68-byte zero-filled transaction. Decoding rejects truncated, oversized or
bad-checksum replies rather than reproducing unsafe reference buffer accesses.
Status kind `10` has three separate fields: flags, state and mode. Keyboard LED
flags must not be mistaken for the transport mode. Vendor command forwarding
and peer firmware-update dispatch are not implemented.

Transfer completion proves only local SPI completion, **not radio acceptance,
host delivery or sleep**. `m1_radio_quiesce` requires explicit caller permission
and no outstanding/unconsumed transfer before applying the reference's inactive
pin pattern. It does not infer permission from an opcode or an unknown reply.
The foreground scheduler below owns this HAL. The runtime power controller
coordinates sleep/retention; the explicit pairing owner is described above.
Physical host-delivery proof remains outstanding.

Native tests cover every supported payload length, opcode rejection, checksum,
padding, malformed lengths and status offsets. Linked Cortex-M4 tests execute
the actual HAL/SDK for pin configuration, DMA ordering, buffer ownership,
separate RX/TX completion, drain, backpressure, faults and timer wraparound.
They also check active scan/LED DMA survives radio operations. DMA movement and
SPI status are scripted; no physical radio transmission is demonstrated.

### Wireless report scheduler

`m1_wireless` owns SPI3 from one foreground context. Initialization requires an
idle initialized HAL, one of modes 0/1/2/5, and explicit permission from the outer
coordinator that the previous host has been released. It sends mode (`93`) and
status-request (`92`) packets, then polls (`09`) on the active-low data-ready pin.
Only poll replies update status, matching the original parser's receive gate.
The status request uses a deterministic zero payload rather than the reference's
retained vendor-command byte. Peer compatibility still needs physical validation.

A matching mode byte confirms only mode selection. Reports additionally require
the reference's eligible state 3 and fresh status; LED flags are kept separate.
An initial neutral pair precedes keyboard input. Same-mode transitions from
state 3 to states 0, 1, 2 or 4 discard queued/committed keyboard and consumer
input, including a partially sent list/bitmap pair, while retaining the healthy
radio session. This follows the reference's bounded state tables at
`0x08017A82` / `0x08017AFE`; it does not issue another mode or pairing command.
The GUI shows waiting/searching and Fn transport selection remains available.
When fresh state 3 returns, a new neutral keyboard pair precedes reports. The
application cancels held keys, modifiers and knob movement on both readiness
edges, releases consumer controls and requires physical release before rearming.
It cannot acknowledge a release to an already disconnected host.

Status older than 500 ms immediately gates output and discards committed keys;
a fresh report-eligible reply restarts with a neutral baseline. The paired-host
watchdog faults after five seconds without status, while state 4
(pairing/searching) permits a 60-second pause before faulting. This distinction
comes from a physical pairing attempt: the peer advertised and the host bonded,
but paused status replies during pairing. An unsolicited mode change,
unsupported state outside 0–4, watchdog expiry or HAL fault still stops output
and requires an explicit stop and restart. Poll/query/report intervals and
deadlines are custom tunables in `defaults.h`, not inferred stock timer units.

`m1_radio_keyboard` translates the common **already remapped** NKRO report into
the peer's two `81` subtypes, reviewed against usage helpers `0x08007820`/
`0x08007764` and scheduler `0x08017FDC`:

| Subtype | Contents |
| --- | --- |
| 1 | Modifier byte and six usage slots; no USB reserved byte |
| 2 | 15-byte bitmap indexed by usage itself, not by usage minus four |

Still-held keys retain their list/bitmap ownership across reports. New extended
usages receive free slots first; overflow usages below 120 use the bitmap.
If an extended usage cannot fit, subtype 1 emits HID ErrorRollOver rather than
writing beyond the peer bitmap as the original could. USB retains its full
custom NKRO range. Actual peer handling of rollover and report aggregation is
not established by the offline tests.

The scheduler copies one complete keyboard state and rejects further offers
until both subtype transactions complete. A release cannot overwrite half of an
accepted press. `reports_sent` counts locally completed pairs; `local_idle`
means only local work is drained. **Neither proves delivery to a wireless host**
and neither may satisfy the Fn transport menu's host-drained callback.

Native tests cover every common keycode, bitmap boundaries, ownership across
releases, rollover recovery and evolving polyphony. Linked ARM tests execute
the scheduler and official SPI/DMA drivers with scripted replies/completion,
covering all three Bluetooth selections and 2.4 GHz, baseline ordering,
backpressure, stale/invalid status, faults and timer wrap. `m1_live` binds this
component to the shared keyboard application and runtime transport/power owners.
Only Bluetooth slot 1 pairing and a three-press A key delivery have physical
confirmation. Other slots, 2.4 GHz delivery and the electrical sleep handoff
remain unverified.

### Radio battery and sleep-control handoff

The owner passes filtered battery state to `m1_wireless_battery` after a battery
HAL update or invalidation. Valid, source-qualified percentages 1–100 become
opcode `90` with one payload byte. Queued metadata is latest-only; an in-flight
packet is immutable. Unknown/invalid readings cancel unsent metadata and never
become a fabricated full battery. Unchanged percentages are not retransmitted.
Keyboard report pairs take precedence, including releases. The last locally
completed percentage is separate from the latest requested value. Raw charger
pin state is not encoded as an unverified charging/full flag.

`m1_wireless_request_sleep` admits only the reviewed opcode `94` payloads 3
(sleep) and 5 (Bluetooth retention). It requires explicit host-release permission,
no transfer in flight and neutral completed keyboard state. The unsent neutral
startup baseline may be discarded for command 3, allowing critical protection
before a host becomes report-eligible; retention still requires eligible Bluetooth.
Accepted requests reject new keyboard/battery offers and stop regular polling.

Cancellation succeeds only before the control packet enters the HAL. The owner
must process activity/cable cancellation before servicing that queued request;
after submission, cancellation fails and a real wake/restore path is required.
Cancellation discards the staged battery metadata, so republish current qualified
battery state when normal operation resumes. After both DMA channels and the
SPI shifter finish, `m1_wireless_sleep_sent` exposes the exact transmitted command
for `m1_power_radio_committed`. The scheduler then stays quiet, including after
normal status expiry. Pending work, a DMA flag alone, timeout or error cannot
produce a completion. All handoff deadlines are in `defaults.h`.

Completed Bluetooth retention can escalate to command 3 without reinitializing
SPI or admitting keyboard traffic. Cancelling an unsent escalation preserves
the quiet retention state, not a fictional awake state. The power policy still
requires command 3 completion when critical protection has superseded retention.

This is a **local transaction handoff, not proof of peer sleep or host release**.
It does not change power rails, apply sleep GPIO patterns or invoke RTC sleep.
The outer coordinator still owns those operations and the retention timeout.
ARM tests compose the actual battery filter and idle/critical policy with the
scheduler and SDK DMA path. ADC values, time and completion are scripted; no
battery voltage, charging behavior, current draw or radio peer is simulated.

### Composite USB class

`m1_usb_class` uses the pinned Artery device core, standard-request handler,
interrupt routines and USB peripheral driver, without modifying SDK sources.
The board-owned descriptors expose a report-only 30-byte NKRO keyboard and
two-cable USB-MIDI 1.0, and an independent Consumer Control HID. Cable 0 is
performance, cable 1 GUI SysEx; bulk endpoints are 02/82, with 64-byte full-speed
and 512-byte high-speed packets. Keyboard HID IN is 81. Interface 3 uses IN 83
for a two-byte little-endian consumer usage (zero releases, maximum `03ff`).
The four-interface configuration is 191 bytes. Both HID interfaces have their
own SET_IDLE state and immutable in-flight buffers.
The short product string preserves the complete control-jack name in the GUI's
ALSA discovery path. There is no CDC, factory vendor command, class-level
boot-entry handler, fabricated serial or remote wake advertisement. Armed IAP
entry is handled separately by the application's SysEx service.

Bind the stopped core with `m1_usb_bind` before SDK initialization. Foreground
send calls copy accepted buffers and preserve interrupt masking. A pending IN
buffer cannot be overwritten. Unread MIDI OUT data retains its buffer and NAKs
further traffic until foreground `m1_usb_midi_take` consumes and rearms it;
undersized destinations never truncate packets. Malformed packet lengths latch
a fault. Keyboard HID supports one-byte LED output; both HID interfaces support host-requested idle repeats;
unchanged application heartbeats do not bypass SET_IDLE. Its four-millisecond
units and rate-change timing follow [HID 1.11 §7.2.4](https://www.usb.org/sites/default/files/documents/hid1_11.pdf).

Linker wrappers validate endpoint numbers before the SDK indexes its arrays,
reject unsupported device requests, and serve dual-speed descriptors even when
the negotiated speed is full speed. Suspend retains in-flight ownership; reset
and deconfiguration discard it. Halt/clear-halt aborts pending IN only after
bounded SDK disable/flush completes; timeout latches a fault. The generation
counter changes on these events and wake. The owner must invalidate application
output and reset the GUI session before further submissions. Drained means no
local pending IN, not proof that a host consumed an aborted report.

`m1_usb_audit.elf` executes these paths through the actual SDK and shared
application/SysEx service into the Python GUI decoder, at both packet sizes.
Tests include all endpoint-index bytes, control lengths, descriptor separation,
buffer ownership, IRQ masks, idle timing, halt recovery, disabled-endpoint FIFO
cleanup, independent TX/RX flush timeouts and 82-key ACK/readback.
Endpoint completions and register effects are modeled. This does **not** prove
physical enumeration, acquisition cadence or a running M1 application.

### Foreground application and transport routing

`m1_live` connects periodic scan frames to the shared keyboard/MIDI application,
LED renderer, USB/radio output and GUI SysEx services. One foreground owner
calls it with independently maintained wrapping millisecond/microsecond clocks.
Initialization restores the custom journal before accepting the first scan,
using stored calibration or the validated factory bounds above. Unknown bounds
are not replaced with ADC rails. Settings save through an explicit
`m1_live_storage_ops_t` owner gate; pending/saved/fault metadata reaches the GUI.
Without those callbacks, edits remain pending in RAM and calibration entry is
disabled. With a healthy storage backend, Fn+C and the GUI run the shared
parallel calibration routine for all 82 keys, and Fn+R (`cfg clean` from the
GUI) clears the custom profile. RESET erases both custom pages through the same
power, output-drain and pause/resume gate as autosave, and the board writer
blank-verifies each erase before replying. It reaches only
`0x08027000`/`0x08027800`: the bootloader, factory settings and factory
calibration pages are untouched, so defaults return with the factory electrical
bounds instead of an invented scale. The cleared RAM state is applied on the
confirmed neutral frame, and the following autosave stores a record that
carries no custom calibration. A denied gate refuses RESET without erasing; a
failed erase latches the store fault rather than claiming success.
Calibration completion saves the entire profile through the same power,
output-drain and pause/resume gate as autosave. Completed physical keys may
remain held: calibration suppresses their host output. Busy gates retain the
complete candidate without a user-inactivity timeout; cancellation,
invalid scans and loss still discard it. Active bounds change only after
verified storage and successful scan resume. Gap publication follows that
decision and requires fresh neutral input before rearming. A failed resume is
terminal even if the new record reached flash; check storage status rather than
assuming the previous record will be selected at reboot.
The caller selects USB, BT1/2/3 or 2.4 GHz at initialization. A wireless choice
requires a healthy scheduler configured for that mode, then waits for actual
peer eligibility before accepting keyboard input. HID state goes only to the
selected transport. The radio uses the same physical-to-keycode mapping and
Schmitt logic as USB; MIDI performance is disabled in all wireless modes.
Filtered battery metadata is forwarded to the radio and Fn+Space renders the
battery hint. USB SysEx remains available for GUI configuration while the
keyboard reports to a wireless host; it does not carry wireless performance MIDI.

Only complete periodic frames enter velocity and capture processing, never
one-shot wake scans. Sequence gaps or duplicates cancel held outputs and partial
velocity windows, require neutral before rearming, and terminate a per-key
capture with an explicit loss marker. GUI snapshots remain latest-only.
USB generation changes always reset the GUI session, but invalidate key
ownership only when USB is the active keyboard transport. Reconnecting the GUI
must not release wireless keys. Stop neutralizes application state without
changing rails; continue servicing releases before explicit reinitialization.
Wireless restart additionally requires the outer owner's documented neutral-output
handoff; a neutral SPI packet still does not prove receipt by the remote host.

Power integration uses `m1_live_power_suspend`, continued foreground service,
then `m1_live_power_park` to drain local neutral reports and relinquish foreground
hardware ownership. Unlike stop/reinitialization, this preserves RAM settings,
calibration bounds and pending unsaved changes. It cancels transient menus,
calibration, captures and the GUI lease; no flash writes occur during handoff.
While draining, complete acquisitions are consumed and discarded, so report
backpressure does not fill the scan FIFO. A never-linked wireless transport may
hand off a cancellable neutral baseline and unsent battery metadata; this is
not a claimed host delivery. Previously committed held reports must still drain.
While parked, live service touches no peripherals. The outer owner still owes
host-release/peer-sleep policy, LED blanking, scan/USB shutdown and rail/clock
sequencing. Local completion is not permission to remove power.
After explicit physical restoration and servicing the same transport,
`m1_live_power_resume` discards unread pre-wake scans/control traffic and requires
fresh neutral acquisitions and a new GUI handshake. Stopping an already parked
owner is terminal and does not reclaim hardware. Wireless restoration requires
a fresh matching mode reply but not an already connected host; host arrival
still invalidates offline input and requires release before rearming. The development
main loop delegates this sequence to `m1_runtime_power`.

`m1_transport_ops_t` callbacks connect Fn+F1–F5 to `m1_transport`, bound by the
development main. Other callers can omit the callbacks to disable these actions.
The optional availability callback hides/rejects unavailable USB selection.
The foreground owner waits for neutral reports, USB MIDI cleanup where relevant,
local drain and the outer owner's neutral-output handoff before calling select.
It latches that boundary while selection is in progress, since the old
driver may then be stopped. Confirmation also requires the selected USB endpoint
or a fresh matching radio mode to be confirmed; a callback cannot bypass those checks.
An interrupted or timed-out attempted selection latches a terminal transport
fault instead of resuming typing on an ambiguous host. A USB session change
during an authorized selection resets control traffic without revoking the
already established old-transport handoff.

`m1_live_audit.elf` exercises this coordinator through the real USB class and
shared services at both packet sizes, decoding snapshots with the GUI codec;
it also runs all four wireless modes through the real scheduler/SPI/DMA code.
Acquisition, battery and LED boundaries are scripted, including discontinuities
and backpressure. Factory loading executes against synthetic read-only flash.
Profile I/O and the storage pause/resume callbacks are scripted; tests cover
deferred neutral saves, no-change wear, GUI status, restart restoration, write
failure latching and terminal gate/resume failure. Calibration tests exercise
GUI/Fn+C entry, parallel 82-key collection, held-key save, deferred gates,
whole-profile restoration and cancellation/timeout/scan/USB/storage failures.
The save-gate audit executes
actual scanner/time/battery/LED HALs with scripted power and transport readiness;
the flash driver has its own controller-model audit. Transport tests exercise
both scripted callbacks and the runtime selection owner. Radio status and DMA
completion remain scripted, not proof of host delivery, physical scans or
measured 8 kHz operation.
Verified radio delivery, physical cable/sleep qualification and
power-cycle persistence remain physically unqualified in the experimental application.
Normal peer-reported disconnect/reconnect is supported with release-before-rearm;
hardware/protocol faults are not automatically restarted. Physical wireless
reconnection remains unverified.

### USB hardware lifecycle

`m1_usb_hw_start(platform_quiescent)` binds the composite class to the official
SDK initializer. It requires privileged foreground execution, the verified
216 MHz clock/PLL configuration, PC13 configured as an input and low, stopped
SysTick/scan/output activity, and disabled USB interrupts. Invalid preconditions
make no peripheral writes. The caller must account for time spent quiescent.

The board wrapper resets OTGHS, enables its clock, waits a bounded number of
polls for PLLU, selects the PLLU/HEXT USB clocks, and runs `usbd_init`. This
follows the recovered USB wrapper at `0x080145B8`, with explicit fault handling.
The SDK delay hooks use the CMSIS cycle counter in wrap-safe, bounded chunks;
they restore DWT/trace configuration and never reset the counter or borrow
SysTick. A linker wrapper supplies the PHY's minimum power-up delay because
the SDK's ordinary empty delay loop may be optimized away.

Another wrapper defers attachment until reset, AHB idle, FIFO flush, PHY and
cable postconditions pass; the SDK's unconditional success return is not
sufficient. Only then is IRQ77 enabled at priority 0. Route its vector to
`m1_usb_hw_irq`. `m1_usb_hw_running()` means initialized/attached, not configured
by a host; output still requires `m1_usb_ready()`.

`m1_usb_hw_stop()` masks USB interrupts, disconnects and holds the core in
reset before invalidating class buffers. It gates the owned OTG clock and
disables PLLU only if startup enabled it. Other peripherals and power rails are
untouched. This abort is not proof of report delivery or PHY low-power entry.
The outer transport/power coordinator must handle cable loss, drain reports
where possible and invalidate application/SysEx state when the class epoch
changes. Initialization is not automatically retried.

Linked tests execute actual SDK startup and reset-IRQ dispatch, check FIFO
allocation and delayed attachment, preserve unrelated GPIO/DMA, and exercise
PLLU/counter/reset/flush faults, cable loss, counter wrap, shutdown/restart and
the transition through PHY power-down. Counter progression and hardware flags
are scripted: electrical timing, host enumeration and interrupt delivery remain
unverified. The development ELF binds the vector table and foreground loop;
complete runtime recovery and installation support are still required.

### Clock, rails and remaining integration

`m1_clock_init` requires masked interrupts and quiescent peripherals. Using the
official SDK, it raises flash read latency, moves to HICK before changing LDO
voltage, stops the
PLL and external oscillator before reconfiguring them, and establishes the
216/108/216 MHz core/APB1/APB2 clocks. Readiness waits are bounded; failures do
not retry indefinitely. USB PLL output remains disabled until USB initialization.
No flash programming or erase operation is linked into this component.

`m1_startup` implements both PC13 branches of the cold-start rail sequence
observed at `0x08016D04`. Begin requires explicit caller permission, privileged
foreground context and quiescent peripherals/transports. Shared GPIO restoration
establishes normal pin roles and the encoder baseline before selecting a branch:

- **PC13 low (wired):** PB6 high, 10 ms, PB12 low, 10 ms, PC14 high, 10 ms.
- **PC13 high (battery):** initialize RTC, begin its rate measurement against
  the running TMR2 timebase, reduce the idle USB PHY's power and prepare wake
  GPIO. Once the rate is qualified, wait 10 ms with rails low, sleep for 25 RTC ticks, then
  raise PC14/PB6, initialize the scanner, raise PC6 and settle for 10 µs.
  Complete and discard one bounded six-bank scan; lower PB6/PC14/PC6, raise
  PB6, wait 10 ms, restore GPIO/encoder state, wait 10 ms, raise PC14 and
  wait another 10 ms.

Both paths then initialize the scanner, raise PB13, initialize LEDs, raise PC6,
settle the sensors and start periodic acquisition. Delays are defaults, not
measured hardware timings; RTC ticks are not milliseconds. The battery warmup
uses the custom scanner's complete-frame ownership and timeout instead of the
reference's unbounded busy wait. No active USB session is allowed, so there is
no host connection to disconnect during this sequence.

Start `m1_time` before battery startup. Service takes independent wrapping
millisecond/microsecond clocks and polls the RTC rate measurement without
blocking for its window. Its sleep handoff suspends TMR2 and resumes with
observed RTC elapsed time, including early wakes. Fresh polls
start settling intervals after potentially blocking initialization or wake;
the caller must refresh time after service returns. A source change while
servicing this owner, scan fault or ordinary sleep failure stops owned HALs and
rails and attempts GPIO restoration. Restart is explicit; failed GPIO restoration
retains ownership and blocks begin. Fatal clock restoration leaves IRQs masked
and SysTick stopped: subsequent stop/service calls perform no peripheral work.
The encoder baseline is available only when ready. Startup does not select a
radio transport, report keys, start USB, restore profiles or form a boot image.

### Cold application handoff

`m1_boot` connects the cold-start HAL to `m1_live`. Its caller must first
establish normal clocks and the running TMR2 timebase, install interrupt routes,
and explicitly prove cold peripheral/host ownership. It is not a reset handler
or a way to restart an existing host session. The caller selects USB, BT1/2/3
or 2.4 GHz; transport selection is not yet restored from the custom journal.

The foreground sequence is:

1. Begin the source-specific GPIO/rail sequence above.
2. With external power, attach USB before sensor/LED initialization, while no
   acquisition DMA is active. Refresh time after the blocking SDK call.
3. Complete rail/warmup initialization, then pause scanning and discard unread
   frames before binding profiles/calibration. USB control does not depend on
   valid sensors; it also remains available for a wireless-selected keyboard
   while external power is present.
4. For wireless selection, initialize SPI3, wait its full startup pulse and
   initialize the scheduler for that exact mode. No peer link is fabricated.
5. Initialize battery inputs and the application with restored profile/factory
   bounds and the actual `m1_save_ops()` gate, then resume scanning.

`READY` transfers ownership to the runtime caller, which must then service the
application using fresh `m1_time_now` readings. It means initialization completed,
not that USB enumerated or the radio linked. This coordinator stops doing work
after handoff. USB selected on battery remains disconnected; it does not silently
select another transport. Optional transport callbacks are passed through to the
application and must outlive it; absent callbacks keep Fn transport changes disabled.

Before handoff, a source change or component failure latches a terminal error
and stops radio, scan/lighting and rails. An established wired USB link stays
available for cold-start diagnostics unless its source or timebase is lost.
`m1_diagnostics_service` owns the same SysEx channel until successful application
handoff, with build/git queries, a `boot status` query and the guarded `bootloader`
request. It rejects setting changes, emits no fabricated scan snapshots, and
reports a failure through the reserved `Boot failed: ` log prefix. The GUI
displays that failure and disables configuration instead of claiming connection.
A fatal clock-restoration failure
does no further peripheral cleanup and keeps interrupts masked. There is no
automatic retry, profile erase or factory-data write. Offline tests execute
the composed HAL/application chain; profile I/O and hardware effects are modeled.
Failures before USB startup remain debugger-only diagnostics. Pairing and physical
cable/sleep/wake
validation remain required.

### Development ELF and reset entry

`m1_development.elf` links at the actual application addresses; its generated
`.bin` is accepted by the GUI's experimental factory-conversion action. Builds
do not open hardware. See the [update/recovery contract](DEVICE_FLASHING.md#monsgeek-m1-experimental-conversion).

| Region | Contract |
| --- | --- |
| `0x08005000` | Fourteen-byte boot identity `AT32F405 8KMKB`, without a NUL |
| `0x08005200` | 512-byte vector table; reset plus RTC wake, scan DMA/TMR6 and USB IRQ routes |
| Remaining application loads | Code, SRAM-writer initializer and data initializer end at/before `0x08027000` |
| `0x20000000..0x20017fff` | Main SRAM: relocated flash code, data/BSS and the reserved main stack |
| Profile/factory/boot regions | No load payload; custom slots and stock data are not image sections |

Explicit ELF program headers exclude loader metadata from flash loads: the
default linker must not prepend a segment covering the bootloader. The stack
is an aligned, separate NOLOAD reservation of `M1_MAIN_STACK_BYTES` (8192 by
default), after BSS; its top is the initial MSP. Link assertions reject memory
overflow and profile overlap. This is a reservation, not a measured worst-case
stack high-water mark.

Reset masks interrupts, selects privileged MSP, stops inherited SysTick,
disables/clears the implemented external interrupt banks, clears pending
SysTick/PendSV, installs VTOR and priority grouping, and copies data plus the
complete SDK/custom flash-writer section into SRAM before clearing BSS and
calling main. Unused vectors trap with debugger-visible exception information.
It does not call the SDK's unbounded `SystemInit`.

Main checks the flash-density register and requires an erased IAP flag page,
without writing it. Thus normal reset retains the application and profile slots.
Only an explicit update request shuts down peripherals and USB, checks external
power, and programs/verifies the exact IAP word using the SRAM-resident SDK
writer before reset. It never erases boot metadata. The factory loader's flag
and identity branch at `0x080012aa..0x080012cc`, app handoff at `0x08001356`,
and flag erase at `0x08001322..0x08001336` establish this contract. The persistent
flag protects interrupted header-first updates; corrupting the header alone
would not. Early startup failures no longer imply power-cycle recovery and may
require hardware debugging. This policy still needs hardware qualification.
Main establishes clocks/time and invokes
`m1_boot`. PC13 external power selects USB; battery selects
`M1_DEFAULT_WIRELESS_TRANSPORT` (BT1 by default). That wireless preference is
not persisted yet. After handoff it polls `m1_runtime_power_service` using independent
millisecond/microsecond readings. Profile restore, gated autosave, parallel
calibration saves and custom-profile RESET are linked. Fn+F1–F5 uses the
runtime transport owner.

The development loop implements battery idle/critical sleep, wake restoration,
cable transitions and explicit long-hold pairing requests as described here.
An unsupported source change or device fault
stops acquisition/radio/lighting, retains rails, and latches a
terminal diagnostic. If USB and the timebase remain usable, it sends neutral
keyboard/consumer HID and MIDI sustain-off/all-sound-off/all-notes-off, retains the SysEx recovery
service and reports `Runtime failed: detail=0x…`. It does not restart the failed
peripherals or prove release at a wireless host.
Clock/time faults trap without guessing a safe peripheral recovery sequence.
`m1_main_state` and `m1_main_detail` expose the failure class and its clock/boot
code, density or device-fault bits to a debugger; there is no automatic reset.
Power faults set device bit 32, with the runtime stage-plus-one in detail bits
16–23 and, for awake source transitions, the source stage-plus-one in bits 24–31.

### Awake USB power-source transitions

`m1_source` owns awake cable changes and explicitly parked, cancelled sleep entry.
Plugging USB power into a wireless session preserves its selected BT slot or
2.4 GHz mode and starts the USB configuration link. Fn+F5 explicitly selects
USB typing after enumeration. Unplugging USB typing selects the most recently
selected wireless mode in RAM, or `M1_DEFAULT_WIRELESS_TRANSPORT` if none has
been selected. Unplugging while already wireless preserves that mode.

The owner immediately stops USB on removal. Aborted endpoint buffers are not
claimed to be delivered releases; wireless reports still require their normal
neutral drain. It parks the application, pauses acquisition and stops the LED
driver without cutting sensor/LED rails. Radio transactions remain serviced
during `M1_SOURCE_DEBOUNCE_MS` (20 ms). Only after stable source selection and
idle radio DMA does it start/reduce the USB PHY; first battery entry initializes
and qualifies the RTC time bridge. Fresh timestamps precede LED/radio restoration.
USB may resume before enumeration, so a power-only cable does not block wireless
typing. RAM mappings, thresholds and calibration survive; stale input/control
buffers are discarded and released keys are required before rearming.

The entire handoff is bounded by `M1_SOURCE_TRANSITION_MS` (5 seconds). A cable
edge cancels an Fn selection still waiting for old-host drain, retaining the
current transport's release duties. Ordinary sleep cannot cancel that selection.
Once a platform select/pair callback has run, cable overlap still fails closed:
physical ownership may already have changed. Source bounce before PHY mutation
is debounced; another edge during mutation or a failed peripheral also fails closed.
There are no flash writes, cold application reinitialization or automatic retry
inside this owner. Cable arrival during battery sleep first follows the runtime
restoration path below. Linked tests check the source owner, live handoff and RAM-state
retention with scripted HAL/USB completion; physical unplug/replug is unverified.

Image tests verify every load segment/vector/RAM-code address and reject
corrupted identity, vector, bootloader-prefix and profile-overlap fixtures.
They execute reset from poisoned RAM, including an inherited PSP selection,
and check exact data/code copies, BSS and untouched gaps. Main-loop tests stub
component calls to check ordering and terminal branches; the separate cold
handoff audit executes the actual HAL/application chain. Neither is physical
startup, interrupt scheduling, full-runtime power or update-path validation.

### Runtime battery sleep/wake

`m1_runtime_power` is the sole foreground dispatcher after cold startup. It calls
the live application while awake and draining; once parked, it owns the scanner,
lighting, radio and sleep GPIO sequence. It never reloads application settings or
writes flash. The caller refreshes both clocks after each service call, including WFI.

1. Observe eligible live activity and battery state every
   `M1_RUNTIME_POWER_PERIOD_MS` (10 ms). Sticky activity retains short presses and
   encoder input between observations. USB/external power inhibits sleep. Ordinary
   Bluetooth/2.4 GHz idle limits are 300 qualified policy steps, nominally about
   five minutes; critical-battery policy can bypass activity. Delayed service
   extends these intervals rather than synthesizing missed observations.
2. Drain neutral reports, park the live owner and immediately pause acquisition.
   Complete a black LED frame, send the policy's exact radio sleep command, then
   stop lighting/scanning and reduce USB PHY power before changing sleep GPIOs.
   Command 5 retains Bluetooth; command 3 also quiesces the radio pins.
3. Wait 100 ms, then perform measured RTC sleeps of 30 **RTC ticks**, not 30 ms.
   Between sleeps, raise sensor rails, settle for 100 µs and acquire one whole
   frame. The wake filter retains acquisition sequence across checks. A key
   movement or encoder-switch change requests restoration. Bluetooth retention
   expires after 30 seconds and requires completed command 3 before deeper sleep.
4. Restore GPIOs and rails in bounded stages, reinitialize scanning and lighting,
   then resume the retained radio session or perform a full radio restart.
   Require fresh status confirming the selected mode, not an already paired host.
   Resume periodic scanning and the same RAM application/settings. Wake scans
   never enter velocity processing; keys require release before rearming, so the
   waking press is not replayed as a synthetic host event.

All timings and limits are `M1_RUNTIME_*` defaults. They are custom policy values,
not claims of recovered stock scheduler durations. Reference sequencing comes
from the power-transition, periodic wake-scan and transport-state routines.
Each handoff stage has a 3-second deadline; capture uses its HAL deadline.
Failures latch without retrying rails, peripherals or flash. Clock/time failures
trap immediately; ordinary failures use the retained diagnostic path where USB
is still usable.

USB power arrival wakes the custom runtime without changing its wireless mode:

- Before parking, transfer the existing neutral drain to the source owner.
- While blanking, finish the LED frame and cancel sleep without cycling sensor
  rails. A queued radio command is cancellable only before submission; a command
  already in flight must complete before choosing retained or full restoration.
- During RTC settling, periodic wake checks or an active one-shot scan, stop the
  scan before changing rails and follow the reference GPIO/rail restoration.
  A cancelled escalation from retention keeps the completed command-5 state;
  an already-started command 3 finishes before a full radio restart.
- Finish restoration with USB stopped, then use the ordinary parked source
  handoff to attach USB. A transient arrival that disappears during restoration
  still wakes the application, but does not force USB selection or enumeration.

Arrival inside the guarded PHY/GPIO checks is handled as a source change, not a
blind peripheral retry. No extra WFI is issued after arrival is observed. Arrival
during WFI is noticed after the RTC/other wake returns; PC13 is polled, not an
independent instant-wake interrupt. This custom policy restores the GUI link on
power arrival; stock `0x0801754c` instead reinitializes/disconnects USB between
sleep scans until its separate wake condition. The staged rail/GPIO and retained
radio restoration follow the executable reference; GUI availability is intentional.

The installed-image controller audit executes the real state machine, power
policy, wake filter and GPIO writes with scripted HAL completion and elapsed
time. It covers all wireless selections, critical unpaired sleep, retained/deep
radio wake, cable arrival at every sleep/restoration stage, queued-versus-in-flight
cancellation, source-check races and terminal failures. This is not physical battery, charging,
current-draw, radio delivery or sleep/wake validation.

### Sleep/wake pin ownership

`m1_power_gpio` uses the official GPIO driver for the reference transitions at
`0x08017BCC` and `0x0801A814`. These pins change roles; they are not ordinary
backlight or sensor-power controls:

| Pin | Prepared for sleep | Restored for normal operation |
| --- | --- | --- |
| PB12 | Input, no pull | Push-pull output, then driven low |
| PB10 | Push-pull output, then driven low | Pull-up input for charge-status sampling |
| PA11 | Push-pull output, **existing latch preserved** | Pull-up input |
| PC10/PC12/PC11 | Unchanged by prepare | Pull-up inputs for encoder phases/button |

Prepare requires explicit platform permission, restored main clocks, idle
ADC/timers/output DMA/SPI, no live USB owner/IRQs/endpoints, PC13 configured as
an input, and successful USB PHY power reduction. Restore applies the same
quiescence checks but permits cold initialization or a cable arriving while
asleep; USB must remain idle until pin restoration finishes. Both preserve the
caller’s interrupt mask and unrelated pins, including debug and power rails.
They neither enter sleep nor shut down the radio or sensor supplies.

While prepared, battery service invalidates readings without consuming scans;
battery initialization, a new startup and USB startup cannot take over the pins.
If battery initialization was attempted during that interval, initialize it
again after restoration. A failed restore retains sleep ownership.

Restore samples PC10 and PC12 separately and returns their two-bit encoder
baseline. The read-only switch helper samples PC10, PC12 and PC11 into bits
0–2, requiring input configuration. These are raw sequential reads, not an
atomic physical snapshot, debounced movement or a keyboard event. Normal encoder
reporting uses the separate decoder and auxiliary-report owner, not these raw reads.

Linked ARM audits check the ordered SDK writes, drive/pull modes, preserved
PA11 latch, switch changes between reads, busy/context rejection, battery and
USB exclusion, and cable-arrival restoration. Register effects are scripted;
electrical pin roles beyond the observed sequence and physical battery startup/
sleep/wake operation are not established by these tests.

### Foreground timebase

`m1_time_start` configures SDK TMR2 in its **32-bit plus mode** at 1 MHz
(216 MHz timer clock divided by 216). This custom allocation uses no pins,
DMA, SysTick, DWT or timer interrupts. `m1_time_now` returns independently
wrapping microsecond/millisecond timestamps, preserving sub-millisecond carry.
It must be sampled at least once per counter wrap (about 71 minutes). Keep it
running during flash operations: hardware counts masked-IRQ time without
depending on pending SysTick events. This does not measure flash timing itself.

The API has one privileged foreground owner and preserves PRIMASK. Start
rejects an active/interrupt-owned timer; lost clock or register ownership
latches a fault instead of publishing an unreliable timestamp. Stop/start
explicitly discards the previous epoch. Before changing clocks or sleeping,
use `m1_time_suspend`, then restore the original clocks and pass **measured**
elapsed microseconds to `m1_time_resume`. It retains fractional milliseconds
and checks that the stopped counter was preserved. Clock initialization and
RTC sleep reject a running TMR2. The measured RTC bridge below supplies elapsed
time for battery startup; a requested wake interval is not such a measurement.

ARM tests execute the official timer driver with scripted counter progression,
including masked intervals, hardware wrap, independent millisecond wrap,
suspend/early-wake gap arithmetic, ownership faults and IRQ preservation.
They do not measure oscillator accuracy or physical sleep/flash duration.

### RTC sleep HAL

`m1_sleep_init` configures the reference LICK clock, 7/7 RTC dividers, CK_B
16-bit wake counter, EXINT22 rising-edge interrupt and IRQ3. It does not reset
the backup domain or overwrite retained registers/calendar values. A different
existing RTC clock is rejected. Initialization checks SDK error returns, keeps
the wake timer stopped until requested and leaves failed initialization unready.

`m1_sleep_wait(ticks, platform_quiescent)` accepts 1–65536 RTC ticks, not
milliseconds. The platform must first drain host reports, coordinate radio and
USB PHY shutdown, stop periodic interrupts and remove sensor/LED power. The HAL
rechecks DMA channels 1/2/3/6, ADC, scan timers, TMR2, both SPI busy flags and owned
power latches. Missing permission, active hardware, incompatible clocks,
standby selection or deep-sleep debugging rejects entry without writes.

The HAL masks interrupts through sleep and clock restoration, while the enabled
RTC IRQ can still wake the core. This follows [Arm's restore-before-interrupt
pattern](https://documentation-service.arm.com/static/5f2ac4ab60a93e65927bbdbf)
(DUI0553A §2.5.2). It requires privileged thread mode with BASEPRI/FAULTMASK clear
and preserves the caller's PRIMASK. SysTick is disabled during the transition;
its writable control bits are restored only after a successful clock restore.

At HICK, the regulator switches to the original firmware's **1.0 V** setting
and extra-low-power mode before the official SDK WFI call. On wake it returns
to normal regulation and reestablishes 216 MHz before acknowledging the RTC and
unmasking interrupts. The 1.0 V selector is confirmed by the [Artery reference
manual](https://www.arterychip.com/download/RM/RM_AT32F402_405_EN_V2.01.pdf)
(§3.6, §3.7.3), although the pinned SDK enum omits that value; its field-setting
macro is used unchanged. The wake timer is stopped after each wait.

Other interrupts may wake early. A timer result means a wake flag was observed,
not that a measured duration has passed; the outer power manager must handle
elapsed time and key wake capture. Clock-restore failure leaves interrupts
masked and SysTick stopped and returns `M1_SLEEP_CLOCK_FATAL`. Ordinary
application operation must not resume on that result. There is no automatic
reset, standby entry, factory-data write or physical sleep test.

### Measured RTC time bridge

`m1_sleep_time_begin/service` measures RTC subsecond progression against TMR2
over at least `M1_SLEEP_CLOCK_WINDOW_US` (20 ms), with a 1-second qualification
timeout. It admits the reference manual's 30–60 kHz LICK range with a small
sampling allowance, rather than assuming a fixed 40 kHz oscillator. Calendar
ticks run at LICK/64 with the board's 7/7 dividers; subseconds run at LICK/8.
The fast calendar's day wrap is therefore **not 24 wall-clock hours**.

`m1_sleep_timed_wait` composes the existing sleep HAL with TMR2 suspend/resume.
It reads SBS before the SDK calendar getter, keeping TIME/DATE locked until
the final DATE read. After wake, the SDK refreshes shadow registers inside an
unlock/sync/relock sequence, as required by the
[reference manual §18.3.2](https://www.arterychip.com/download/RM/RM_AT32F402_405_EN_V2.01.pdf).
The measured rate converts the observed counter delta into microseconds,
carrying fractional conversion remainders across sleeps. Early interrupts use
their actual delta, not the requested wake count. Backward/implausible deltas,
stale or incompatible RTC configuration, failed synchronization and timer
ownership faults are rejected. The requested wake bound has a 100-ms default
clock-restoration allowance, not permission to sleep indefinitely.

Qualification/timing policy lives in `defaults.h`. The bridge never resets the
backup domain or writes calendar/date/backup data. A failed time handoff returns
`M1_SLEEP_TIME_ERROR` and latches a fault; failures after suspension leave TMR2
stopped. Clock restoration failure retains the existing masked fatal behavior.
Resolution is one RTC subsecond tick, with measurement quantization; drift
during sleep is not corrected. This is not a precision wall clock or a physical
oscillator calibration claim. ARM tests script counter progression, including
independent wraps, fractional carry, early wakes and protected-sync failures.

### USB power-down helper

`m1_usb_power_down(platform_quiescent)` wraps the pinned SDK's unmodified
`reduce_power_consumption`, matched to reference entry `0x08017238`. It is a
prerequisite for battery sleep/cold start, **not USB bus suspend** and not a USB
device driver. The caller must first drain reports, shut down the USB stack,
disable its interrupts/endpoints and stop time-sensitive scan/output activity.

The helper requires privileged foreground context, a stable external oscillator,
the normal 216 MHz clock and PC13 high. It checks scan timers/ADC, the four owned
DMA channels, SPI busy flags and USB interrupts. A clocked USB core must be in
device mode with global interrupts and all eight IN/OUT endpoints disabled.
Failed preconditions return without peripheral writes. Unclocked USB registers
are not read. Interrupt masking is preserved across the operation.
If the USB owner left the stopped core in reset, the helper releases reset
only after these checks, before the SDK configures PHY sleep.

Requiring HEXT selects the SDK's bounded path and avoids its unbounded PLL-only
wait. The SDK enables the OTGHS clock, requests device mode, polls suspend for
up to 216000 reads and gates the PHY. The wrapper then checks suspend and cable
state; a missing suspend flag or cable insertion leaves readiness false.
This read limit belongs to the pinned SDK, not a calibrated delay. Readiness
also rechecks the PHY gate bits. Invalidate it before reinitializing the USB
core after wake or cable insertion. This helper does not restore enumeration
or silently retry failures.

Linked tests execute the actual SDK routine, including its full timeout loop,
and cover cold/warm core guards, every endpoint, active DMA, both interrupt-mask
states and cable insertion during the wait. Register behavior is scripted;
neither suspend signaling nor reduced physical current is established.

### Scan-based wake detector

`m1_wake` accepts complete, canonical one-shot frames. Its independently written
policy follows the comparisons at reference `0x08016524`: acquire the baseline
over ten frames, reset each drift counter on equality, refresh its baseline
after fifty unequal readings, and wake only for a drop **strictly greater than
300 counts**. Refresh happens before the drop comparison, so that refresh
frame cannot trigger a key. The last acquisition frame cannot trigger either.
The counts are configurable defaults, not elapsed-time units.

The enable mask uses the 82 compact key IDs; unused ADC positions, including
the battery cell, are not wake keys. Duplicate sequence numbers do not advance
filter state. Missing/out-of-order or invalid frames latch a fault that requires
leaving the sleep loop, rather than inventing a key press. Sequence wrap is
handled. The first wake freezes a complete frame and a simultaneous movement
mask until the next sleep episode; later reads cannot overwrite this evidence.
Movement is separate from the user's keyboard actuation threshold and is not
a HID event or velocity result. The outer restoration path must preserve the
captured input until the application/transport has handled it.

Native tests cover all 82 wake positions, simultaneous and disabled keys,
strict threshold/refresh edges, frozen capture, duplicates, sequence wrap and
invalid frames. Linked HAL tests cover repeated one-shot DMA chains,
one-shot/periodic transitions, stale timer IRQs, unread-frame protection,
timeouts and faults. Runtime restoration requires release before rearming;
it does not transmit the waking press.

Complete power management still requires physical pairing verification, handling cable changes
after an Fn switch begins physical selection or during PHY mutation, and physical qualification. The
reference paths at `0x08016F68`, `0x0801754C` and `0x080168B0` distinguish light
idle, longer sleep, periodic sensor wake checks and radio retention. They must
not be replaced by an unconditional WFI or indiscriminate GPIO power-off.
Charger polarity, battery readings, current draw and wake reliability still
require physical validation before describing power management as complete.

## Verification limits

The adapter's identity transaction is physically checked on an ID2949 keyboard
running **v4.08**, enumerating as `3151:5030` at USB high speed. The reference
version is v4.10; matching model IDs do not prove identical peripheral or update
behavior between revisions. Guarded factory entry and application IAP transfer
have a physical checksum/readback success verdict. Custom USB enumerates at
480 Mb/s and answers build and startup diagnostic queries. The on-demand IAP
flag transition and normal cold-boot persistence have compiled offline checks,
but are not yet hardware-qualified. Keyboard startup
uses provisional bounds when factory records are outside the accepted electrical
domain, without changing those records. Released-key GUI snapshots and a
continuous per-key capture are physically checked. A/S presses and simultaneous
detection have matching report bits and independent velocities; full-layout
typing and worst-case musical load are not yet qualified. MIDI notes, Jankó and
Fn+V have user confirmation. See [validation](VALIDATION.md) for measurement limits.
The alternate application PID remains covered only offline.

Offline tests exercise report framing, invalid replies, model rejection,
changed/ambiguous targets, vendor-interface selection, descriptor identity,
short transfers, I/O failures, image bounds and rejection of unverified targets.
The private v4.10 bootloader instructions independently accept the host packet
sequence and reject simulated readback corruption. Tk checks cover model
selection and identity-gated factory/custom conversion actions. Native C-to-Python tests run the
actual shared command mailbox, telemetry encoder and SysEx stream with 82 keys.
Mocked Tk tests verify identity-selected geometry, sensor 81 edits/capture,
all-key thresholds and calibration status/cancel. These checks do not validate an M1
custom application, physical scanner timing, lighting waveform, persistence or updater.
Custom-profile RESET is covered only as linked foreground behaviour with
scripted erase completion: no physical reset has been performed, and a real
erase's supply margin and timing on the connected keyboard remain unverified.
Battery/filter/menu/power-policy tests are native software tests. Clock,
wired/battery cold-start rail ordering, battery GPIO configuration, fresh-frame acquisition,
SPI3 radio transfers, USB power-down and RTC initialization/sleep/resume also
execute the linked Cortex-M4 code and official SDK under scripted register
models. RTC readiness and the WFI wake boundary are scripted, not elapsed-time
or architectural exception simulation. Neither category demonstrates radio delivery, charging behavior or
actual sleep/wake operation on the connected keyboard.
The runtime-controller audit additionally checks composed sleep/wake ordering
with scripted HAL boundaries, not an electrical model of the complete keyboard.
