# MonsGeek M1 V5 TMR

The M1 backend contains scan, lighting, radio-transfer, battery-input, composite USB,
USB power-down and RTC sleep HALs, clock and wired/battery cold-start
components, an 82-key application library, a wireless report scheduler,
transport-menu and power-policy components, and matching GUI geometry. Connected-device access is **read-only factory
identity inspection**, for internal model **ID2949**. There is no flashable M1
application or flashing support. The Huntsman image must never be installed on this keyboard. Wireless
receiver operation and other MonsGeek models are not implemented. Complete
Bluetooth/2.4 GHz operation and power management are not yet available.

## Identify a keyboard

1. Connect the keyboard by USB in wired, normal application mode.
2. Open the GUI's **Device flashing** tab and select **MonsGeek M1 V5 TMR
   (identity only)**.
3. Refresh, then choose **Read firmware details…**. Linux may request permission
   to open the vendor HID interface.

Refresh only reads Linux device metadata. Explicit inspection sends the vendor
identity query and requires ID2949 before showing **FACTORY FIRMWARE · ID
VERIFIED**. The USB revision is shown separately from the queried firmware
version. An unavailable serial is not synthesized from the USB location.
There are no install, restore or reflash actions, including in the privileged
worker. Factory configuration and the keyboard interfaces remain untouched.

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

The rotary encoder is a separate digital input (PC10/PC12, button PC11), not
an 83rd analog sensor. Its firmware/UI operation is not implemented yet.

Preview the board without opening hardware:

```sh
python tools/keyboard_gui.py --demo --board MG-M1V5TMR
```

The GUI reads the same key table, sizes its canvas for six rows, and binds
profiles to a board target and layout. It never applies a Huntsman profile to
M1. Shared MTG3 telemetry, SysEx services and the GUI support all 82 keys,
including per-key capture. Native C-to-Python and mocked Tk tests exercise this
path. Live configuration still requires a complete M1 USB application.

## Scanner HAL and application libraries

The HAL uses the pinned official Artery ADC, DMA, GPIO, CRM and timer drivers.
It owns only ADC/mux pins and TMR3/TMR6/DMA1 channel 6. Its caller must establish
the 216 MHz clock and sensor power first. Incompatible clocks or an ADC
calibration timeout fail initialization; startup is not hidden inside the HAL.

TMR6 schedules a requested **8 kHz complete-frame cadence**. Each acquisition
chains six TMR3-triggered rows. DMA completes before selecting the next bank;
only a complete, correctly ordered frame is published. Raw ADC `0…4095` maps
to canonical `1…4096`, decreasing with travel. ADC rails are not per-key travel
calibration. A short IRQ critical section protects the latest complete-frame
copy. DMA errors, invalid samples and cadence overruns fail the scanner rather
than silently changing velocity's timebase. This cadence is not measured yet.

The shared application is compiled with 82-key storage and the M1 board
callbacks. Native tests exercise all keys, simultaneous NKRO, the Fn/MIDI menu,
LED permutation, normalization, incomplete/out-of-order rows and frame drops.
The ARM build produces static libraries and emulator-only audit ELFs,
**not** an installable firmware.
See [build commands](BUILDING.md#monsgeek-m1-libraries).

`m1_hal_capture_start(now_us)` uses the same ADC/DMA path for one complete
six-bank wake-check frame, without starting the periodic timer. Completion
stops ADC triggering and disables ADC, while retaining configuration for the
next capture. The caller consumes the complete frame before starting another
capture or periodic scanning. Busy/unconsumed frames are never overwritten.
The mandatory foreground service enforces `M1_WAKE_SCAN_TIMEOUT_US` (2000 µs).
Timeout, DMA error or invalid sample invalidates acquisition and requires init.
Clocks, sensor power and settling remain caller responsibilities. A one-shot
frame is not an 8 kHz sample stream and must not feed normal velocity fitting.

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
| Boot request flag | `0x08004800`; not custom profile storage |
| Factory calibration | 2048-byte records at `0x08032000` and `0x08032800` |
| Factory key types | Record at `0x08033000`; preserve it |
| Clock | 12 MHz external oscillator; reference core 216 MHz, APB1 108 MHz, APB2 216 MHz, USB 48 MHz |
| Sensor acquisition | ADC1, DMA1 channel 6; six banks, 15 conversions per bank, 21-cell logical row stride |

Entering the factory bootloader is **destructive before an image is sent**:
the application entry command erases settings, and the bootloader erases its
application range before USB enumeration. Do not use bootloader entry as an
identity or connectivity test. No custom storage region is allocated for M1;
Huntsman storage addresses are not portable to it.

An M1 firmware port still requires verified startup/power behavior, physical
confirmation of the inferred key/sensor/LED mapping, USB clock/PHY and runtime binding,
physical verification of factory calibration, safe storage ownership and an independently checked
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
must be ADC-representable and span at least the shared calibration minimum.
Bounds receive the same native-to-canonical conversion as scans (`ADC + 1`).
The decoder stages the entire result before publishing it. Bad markers, absent
calibration, wrapped/reversed/narrow pairs, busy flash or invalid execution
context leave the previous output unchanged. The reader preserves the interrupt
mask and touches only 252 data bytes plus three trailer bytes per page.

This is a conservative import, not the stock calibration algorithm: it does not
repair records, use sample-minus-700 fallback floors, rebase resting samples,
import nonlinear vendor curves, initialize the key-type page or erase anything.
The factory schema has no verified checksum; plausible in-range corruption
cannot be detected by marker/range checks alone. These bounds drive custom
linear lighting/aftertouch, not a claim of physical millimetres.

Foreground initialization rejects invalid/missing factory calibration and
exposes the loader result to its outer owner. It does not proceed with invented
travel bounds. A calibration-recovery path and custom profile persistence are
still required before an installable firmware can handle every device state.
The GUI reports successfully imported calibration as stored/read-only, with
no custom generation or writable-profile claim. Tests use synthetic records,
read-only emulated flash and no connected-device calibration reads.

## Power and transport components

These are library APIs with offline tests, **not working wireless firmware**.
`m1_controls_bind` attaches board-specific input/lighting hooks to the shared
application. Its table defines these Fn controls; bare F1–F5 remain normal keys:

| Chord | Software policy |
| --- | --- |
| Fn+F1 / F2 / F3 | Bluetooth slot 1 / 2 / 3 |
| Fn+F4 | 2.4 GHz |
| Fn+F5 | USB |
| Fn+Enter | Keyboard/MIDI toggle on USB only; unavailable on wireless |
| Fn+Space | Battery bar on number keys while held; amber Space means unavailable |

Transport names preview while held and selection starts on release. The old
transport must accept a neutral keyboard report, finish MIDI cleanup when
leaving USB, and confirm its output queue has drained. Only then may the adapter
switch transport. A rejected or timed-out selection retains the previous
transport; a successful selection requires neutral keys before rearming.
The selector does not invent a radio acknowledgement. No physical radio adapter
is bound yet. It must also reject selecting unpowered USB and coordinate cable
changes, pairing and radio status with the power controller.

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
half-weight running average. Display changes are debounced and monotonic within
one charging/discharging interval. Cable changes restart the filter. Unlike the
stock special case, an empty reading on external power is not presented as 100%.
Filter timing and display intensity are explicit defaults, not claims of exact
stock scheduler timing.

On battery, at **20% or below**, the overlay suppresses ordinary backlighting
and flashes physical LED78 (logical left Alt). Fn+Space shows one number-row
segment per ten percent, rounded upward; unknown remains distinct from empty.
At **5% or below**, the power policy latches a critical condition until external
power returns. These thresholds and all timing/filter tunables live in
`defaults.h`.

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
The foreground scheduler below owns this HAL. Pairing, host-delivery proof and
sleep/retention coordination remain integration work.

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
An initial neutral pair precedes keyboard input. Lost status, changed mode/state
after eligibility, or a HAL fault stops output and requires an explicit stop and
restart, never replays queued keys automatically. Mode negotiation and status
freshness have bounded deadlines. Poll/query/report intervals and deadlines are
custom tunables in `defaults.h`, not inferred stock timer units.

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
component to the shared keyboard application. A complete transport/power owner, pairing,
physical host delivery and the electrical sleep handoff remain required.

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
two-cable USB-MIDI 1.0. Cable 0 is performance, cable 1 GUI SysEx; bulk endpoints
are 02/82, with 64-byte full-speed and 512-byte high-speed packets. HID IN is 81.
The short product string preserves the complete control-jack name in the GUI's
ALSA discovery path. There is no CDC, factory vendor command, boot-entry handler,
fabricated serial, remote wake advertisement or installable USB image.

Bind the stopped core with `m1_usb_bind` before SDK initialization. Foreground
send calls copy accepted buffers and preserve interrupt masking. A pending IN
buffer cannot be overwritten. Unread MIDI OUT data retains its buffer and NAKs
further traffic until foreground `m1_usb_midi_take` consumes and rearms it;
undersized destinations never truncate packets. Malformed packet lengths latch
a fault. HID supports one-byte LED output and host-requested idle repeats;
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
Initialization imports the validated factory bounds described above; unknown
calibration is not replaced with ADC rails. Settings are volatile: no profile
storage is advertised, and calibration/RESET commands and Fn hints are disabled.
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
Wireless restart additionally requires the outer owner's host-release proof;
neither local idle nor a neutral SPI packet supplies that proof.

Optional `m1_transport_ops_t` callbacks connect Fn+F1–F5 to the outer physical
transport owner. Without these callbacks, selection hints/actions are disabled.
The foreground owner waits for neutral reports, USB MIDI cleanup where relevant,
local drain **and** the external host-release confirmation before calling select.
It latches that release proof while selection is in progress, since the old
driver may then be stopped. Confirmation also requires the selected USB endpoint
or matching radio scheduler to be ready; a callback cannot bypass those checks.
An interrupted or timed-out attempted selection latches a terminal transport
fault instead of resuming typing on an ambiguous host. A USB session change
during an authorized selection resets control traffic without revoking the
already established old-host release proof.

`m1_live_audit.elf` exercises this coordinator through the real USB class and
shared services at both packet sizes, decoding snapshots with the GUI codec;
it also runs all four wireless modes through the real scheduler/SPI/DMA code.
Acquisition, battery and LED boundaries are scripted, including discontinuities
and backpressure. Factory loading executes against synthetic read-only flash.
Radio status, DMA completion and external transport callbacks
are scripted, not proof of host delivery, physical scans or measured 8 kHz operation.
The outer startup/power/transport coordinator, verified radio delivery, durable
storage and an installable application remain unfinished. Link faults are not
automatically restarted, and disconnected-host transport recovery is not implemented.

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
unverified. A complete vector table, runtime coordinator and flashable image
are still required.

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
- **PC13 high (battery):** initialize RTC, reduce the idle USB PHY's power,
  prepare wake GPIO, wait 10 ms with rails low, sleep for 25 RTC ticks, then
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

Service takes independent wrapping millisecond/microsecond clocks. Fresh polls
start settling intervals after potentially blocking initialization or wake;
the caller must refresh time after service returns. A source change while
servicing this owner, scan fault or ordinary sleep failure stops owned HALs and
rails and attempts GPIO restoration. Restart is explicit; failed GPIO restoration
retains ownership and blocks begin. Fatal clock restoration leaves IRQs masked
and SysTick stopped: subsequent stop/service calls perform no peripheral work.
The encoder baseline is available only when ready. Startup does not select a
radio transport, report keys, start USB, restore profiles or form a boot image.

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
atomic physical snapshot, debounced movement or a keyboard event. Encoder
reporting remains separate integration work.

Linked ARM audits check the ordered SDK writes, drive/pull modes, preserved
PA11 latch, switch changes between reads, busy/context rejection, battery and
USB exclusion, and cable-arrival restoration. Register effects are scripted;
electrical pin roles beyond the observed sequence and physical battery startup/
sleep/wake operation are not established by these tests.

### RTC sleep HAL

`m1_sleep_init` configures the reference LICK clock, 7/7 RTC dividers, CK_B
16-bit wake counter, EXINT22 rising-edge interrupt and IRQ3. It does not reset
the backup domain or overwrite retained registers/calendar values. A different
existing RTC clock is rejected. Initialization checks SDK error returns, keeps
the wake timer stopped until requested and leaves failed initialization unready.

`m1_sleep_wait(ticks, platform_quiescent)` accepts 1–65536 RTC ticks, not
milliseconds. The platform must first drain host reports, coordinate radio and
USB PHY shutdown, stop periodic interrupts and remove sensor/LED power. The HAL
rechecks DMA channels 1/2/3/6, ADC, scan timers, both SPI busy flags and owned
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
timeouts and faults. Wake-to-host key restoration is not integrated yet.

Complete power management still requires binding cold startup to the application,
radio scheduler and pairing/sleep handshakes, integrating the USB
lifecycle and power helpers, wake-check scheduling and full restoration of held keys. The
reference paths at `0x08016F68`, `0x0801754C` and `0x080168B0` distinguish light
idle, longer sleep, periodic sensor wake checks and radio retention. They must
not be replaced by an unconditional WFI or indiscriminate GPIO power-off.
Charger polarity, battery readings, current draw and wake reliability still
require physical validation before describing power management as complete.

## Verification limits

The adapter's identity transaction is physically checked on an ID2949 keyboard
running **v4.08**, enumerating as `3151:5030` at USB high speed. The reference
version is v4.10; matching model IDs do not prove identical peripheral or update
behavior between revisions. The alternate application PID and bootloader state
are covered only by offline tests/reference analysis.

Offline tests exercise report framing, invalid replies, model rejection,
changed/ambiguous targets, vendor-interface selection, descriptor identity,
short transfers, I/O failures and unconditional flashing rejection. Tk checks
cover model selection and disabled actions. Native C-to-Python tests run the
actual shared command mailbox, telemetry encoder and SysEx stream with 82 keys.
Mocked Tk tests verify identity-selected geometry, sensor 81 edits/capture,
all-key thresholds and calibration status/cancel. These checks do not validate an M1
custom application, physical scanner timing, lighting waveform, persistence or updater.
Battery/filter/menu/power-policy tests are native software tests. Clock,
wired/battery cold-start rail ordering, battery GPIO configuration, fresh-frame acquisition,
SPI3 radio transfers, USB power-down and RTC initialization/sleep/resume also
execute the linked Cortex-M4 code and official SDK under scripted register
models. RTC readiness and the WFI wake boundary are scripted, not elapsed-time
or architectural exception simulation. Neither category demonstrates radio delivery, charging behavior or
actual sleep/wake operation on the connected keyboard.
