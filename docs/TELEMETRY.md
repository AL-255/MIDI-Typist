# Device telemetry

The GUI uses bidirectional USB-MIDI 1.0 SysEx on **cable 1**, separate from
musical events on cable 0. No CDC/serial interface exists. USB endpoints remain
OUT `0x02` and IN `0x82`; the driver exposes two paired MIDI ports.

This describes the complete Huntsman custom application. The GUI's separate
[M1 factory identity query](MONSGEEK_M1.md#identity-protocol) uses vendor HID,
not these custom telemetry frames; it exposes no configuration stream.
The experimental M1 application accepts the ASCII command `bootloader` on its
active SysEx control session only if the factory IAP flag is armed. It requests
a reset after stopping local peripherals; disconnect, not an ACK alone, is the
transition. The factory updater then erases the application and custom saves.
The M1 control port enumerates at USB high speed and provides live 82-key
snapshots. Released-key validity is hardware-checked with GUI telemetry active;
the declared scan rate is 8 kHz. See [validation limits](VALIDATION.md) for what
is measured; pressed-key performance remains unverified.

## Cold-start failure reporting

`LOG` payloads beginning with `Boot failed: ` or `Runtime failed: ` indicate
that normal application configuration is unavailable. The GUI surfaces the text,
ends its configuration session and does not accept it as a scan snapshot. This
contract is board-independent; the text after the prefix is board-specific.

M1 runtime failures include `detail=0x… store=0x… scan=0x…`: the main fault
bitmask, retained storage error, and first scanner fault respectively. Scanner
codes are 0 none, 1 ADC calibration, 2 pause-time overrun, 3 DMA error, 4 one-shot
timeout, 5 periodic overrun, 6 invalid bank/data, and 7 acquisition queue full.
Stopping acquisition preserves the first reason; reinitialization clears it.

The M1 cold-start owner reports, for example,
`Boot failed: application factory=0x00000003 dma=0x1ef7bdef`. The named error follows
`m1_boot_error_t`; the eight hexadecimal digits hold `m1_factory_result_t`.
The `dma` word packs six 5-bit DMA counts (bank 0 in the lowest bits), sampled
after DMA enable but before the row trigger starts. Each should still be 15;
zero means that bank has not been armed since initialization.
`boot status` requests the current failure log. Build/git and the guarded
`bootloader` request remain available on an established control link, while
configuration writes are rejected. The GUI's `stream gui`/`cfg get` read probes
are acknowledged only to allow the failure log through; no snapshot or successful
configuration readback is fabricated. This service hands ownership to the normal
application on successful startup. It cannot report failures before USB or after
loss of the clock/timebase; those still require external recovery/debugging.

M1 `Runtime failed: detail=0x…` reports a terminal runtime failure: the main
loop retains its debugger-visible failure class, with device-fault bits scan=1,
lighting=2, transport=4, storage=8 and radio=16. For a cable-source change the
detail is the previous external-power state. A working USB/timebase remains
available for diagnostics and guarded IAP; no failed peripheral is restarted.
Wired cleanup submits neutral HID and MIDI CC64/120/123=0 on all 16 channels.

During normal M1 operation or a diagnostic failure, `factory read` returns one read-only `DUMP`
payload with magic `M1FC`, version byte 1, reserved byte 0 and a little-endian
u16 cell count (126). Two records follow, upper then lower: 126 raw little-endian
u16 values followed by their three stored trailer bytes (flag, `55`, `AA`).
Total size is 518 bytes. Values retain the factory rank-major order, not compact
key order. The command exposes only these fixed calibration fields, never an
arbitrary address or a flash write. ACK means the response was queued; ERROR
means it was not. Private readbacks must stay outside Git.

`boot scan` returns the first complete cold-start acquisition, not a live stream:
`DUMP` magic `M1BS`, version byte 1, reserved byte 0, little-endian u16 key count
(82), u32 scan sequence, then 82 u16 canonical samples (native ADC + 1) in
compact sensor order. Total size is 176 bytes. It is retained after startup
failure without restarting peripherals. A missing acquisition returns ERROR;
cold startup times out after `SCAN_STALE_MS` rather than inventing samples.

## M1 foreground timing

During normal M1 operation, `runtime stats` queues one read-only `DUMP` with
magic `M1PF`, version 1. Stop GUI/capture streaming and drain its in-flight
response before requesting this dump: a busy bulk-response slot returns ERROR
(`unsupported command`); ACK means the dump was queued, not already delivered.
After a runtime failure, the diagnostic control owner also exposes these
retained counters: time, scan sequence and timing stages stop advancing with
live service, while the HAL error field includes the terminal fault. This does
not restart acquisition. Boot failure before live initialization has no snapshot.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `M1PF` |
| 4 | u8 | Version 1 |
| 5 | u8 | Eight timing stages |
| 6 | u16 LE | Total size, 120 bytes |
| 8 | u32 LE | Current application milliseconds |
| 12 | u32 LE | Last consumed acquisition sequence |
| 16 | u32 LE | Loss/gap events, including intentional flash-save pauses |
| 20 | u32 LE | Scanner HAL error count |
| 24 | Eight 12-byte records | Calls, total microseconds, maximum microseconds; all u32 LE |

Stage order is HAL services, frame processing, profile storage, report output,
transport controls, lighting, USB control/telemetry, and the complete live loop.
Timing reads the existing 1 MHz TMR2 counter through the SDK and includes interrupt
preemption. Totals/calls wrap modulo 2³²; use differences for interval averages.
Maximums and counters reset at live initialization. Early returns can give stages
different call counts. This is elapsed software time, not ADC conversion timing;
no sample or timer is synthesized for the measurement.

## M1 digital encoder

`runtime encoder` queues a read-only 32-byte `DUMP` (`M1EN`, version 1) during
normal M1 operation. The same single-response-slot rule as `runtime stats`
applies. It neither consumes queued input events nor resets counters/faults.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `M1EN` |
| 4 | u8 | Version 1 |
| 5 | u8 | Flags: active bit 0, queue fault bit 1, debounced button pressed bit 2 |
| 6 | u16 LE | Total size, 32 bytes |
| 8 | u8 | Debounced phase: PC10 in bit 0, PC12 in bit 1 |
| 9 | u8 | Number of queued event-producing samples |
| 10 | 2 bytes | Reserved zero |
| 12 | u32 LE | Sampling calls |
| 16 | u32 LE | Positive complete cycles (`0,1,3,2,0`) |
| 20 | u32 LE | Negative complete cycles (`0,2,3,1,0`) |
| 24 | u32 LE | Rejected two-bit phase transitions |
| 28 | u32 LE | Queue overflows |

Counters persist across scanner pauses and wrap modulo 2³²; they reset at MCU
startup. Inactive/faulted sampling does not advance them. Directions describe
electrical cycles, not physically verified clockwise/counterclockwise motion.
The diagnostic does not prove consumer/HID delivery. Knob events are mapped by
the foreground consumer owner; see [M1 input behavior](MONSGEEK_M1.md).

## SysEx envelope

`F0 7D 4D 54 03 KIND PACKED_BODY F7`

`7D` is the experimental/non-commercial SysEx namespace; `4D 54` is the
project tag "MT", not a registered manufacturer ID. Commercial/product
distribution requires an appropriate registered ID. Do not use another vendor's ID.

Before packing, the body is little-endian `session:u32, sequence:u32,
payload_length:u16, payload, crc32:u32`. CRC-32 uses reflected polynomial
`0xEDB88320`, initial/final XOR `0xFFFFFFFF`, over bytes
`7D 4D 54 03 KIND` followed by the unencoded header and payload.
Each group of up to seven body bytes becomes an MSB bitmask (bit i is byte i's
high bit), followed by those bytes with their high bits cleared. Unused mask
bits must be zero. Payloads are at most 2292 bytes; the largest SysEx is 2643 bytes.

| Kind | Value | Payload and meaning |
| --- | --- | --- |
| HELLO | 1 | Empty; new nonzero random session, sequence 0 |
| READY | 2 | ASCII `build=vVERSION-TARGET git=HASH state=STATE`; confirms session |
| COMMAND | 3 | One printable ASCII command, at most 96 bytes; no newline/NUL |
| ACK | 4 | Command dispatched, echoes command sequence; empty except `git`, which returns provenance |
| SNAPSHOT | 5 | One MTG4 snapshot, sequence 0 in envelope |
| SAMPLES | 6 | 1…32 consecutive HKL1 records, sequence 0 in envelope |
| LOG | 7 | Best-effort debug text, sequence 0 |
| ERROR | 8 | ASCII rejection reason, command sequence |
| DUMP | 9 | One HBD1 read response or M1FC/M1BS/M1PF/M1EN diagnostic, sequence 0 |
| KEEPALIVE | 10 | Empty, sequence 0 |
| CLOSE | 11 | Empty, sequence 0; stops GUI streaming |

HELLO replaces the current GUI session and stops its stream. The GUI waits for
READY, selects `stream gui`, then issues `cfg get ID`. Commands are strictly
serialized with sequences 1,2,…; duplicates do not execute twice. Foreign
sessions and malformed framing/CRC are ignored. An incomplete inbound SysEx
expires after 1000 ms. GUI heartbeats arrive every 500 ms; after 2500 ms without
a valid session command/heartbeat, streaming stops. Neither close nor expiry
disables normal keyboard/MIDI performance. Only one GUI owner is supported.

A COMMAND ACK means dispatch, not configuration acceptance or flash completion.
For `cfg`, the GUI additionally requires matching MTG4 request ID, accepted
result and applicable readback checks. Timeout is 3000 ms, with no automatic
retry. Settings may already have applied when a response is lost.

The GUI's private MIDI-process completion means only that the native send call
returned; it is not a COMMAND ACK or device readback. Native and IPC receive
queues are bounded. Either overflowing fails the capture through a separate
error signal, even if data is already queued. Disconnect reaps that MIDI owner
before flashing may acquire the control port. This changes no USB wire fields.

USB event CIN 4 carries continuing three-byte groups; CIN 5/6/7 terminates with
one/two/three bytes ending F7. Cable-0 notes are serviced before control traffic.
A complete encoded outbound message remains immutable; each up-to-16-event
chunk is copied to a separate DMA buffer. Command execution and CRC processing
run in main, not in the USB ISR. Buffer/timing defaults live in `defaults.h`.
Bulk capture preparation first checks transmit-slot readiness; a busy slot
does not repeatedly copy the same pending batch. Software CRC-32 uses a small
nibble table, with identical wire bytes and no MCU-specific CRC peripheral.
Healthy capture waits for `MIDI_CONTROL_SAMPLE_BATCH` records before encoding,
amortizing envelope and endpoint overhead. Faults flush any partial batch and
then their loss marker. No samples are decimated and the acquisition rate is unchanged.

## Channels

| Payload | Size | Selection | Delivery |
| --- | --- | --- | --- |
| MTG4 snapshot | count-dependent | `stream gui` | latest-only, at most once per 33 ms |
| HKL1 sample | 20 | `stream key THRESHOLD SESSION SENSOR` | every acquisition of the pinned sensor |
| HBD1 read | 128 | `dump read ID ADDRESS` | diagnostic, one response per request |

`stream off` stops binary telemetry. GUI and capture are mutually exclusive,
but LOG messages have independent framing and can accompany either.

## GUI snapshot (`stream gui`)

Latest-only: a newer snapshot replaces an unsent one, so gaps are expected.
All fields are little-endian. The shared application encoder emits an 80-byte
header, one 17-byte record per active sensor, the submitted HID report, zero
padding to a four-byte boundary, and a u32 checksum.

Total bytes = `align4(80 + 17 * count + hid_bytes) + 4`: Huntsman ANSI uses
1152 bytes (61 sensors, 30-byte HID), M1 uses 1508 (82 sensors, 30-byte HID).
The protocol permits up to 128 sensors and 32 HID bytes (2292 bytes total);
each board allocates buffers for its own capacity. Huntsman publishes no faster
than once per 33 ms. M1 also uses this interval; its 82-key snapshots have been
received and decoded on the connected high-speed USB device.

| Header offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `MTG4` |
| 4 | u16 | total frame size |
| 6, 7 | u8 each | board-local layout/profile ID, active sensor count |
| 8 | u8 flags | enabled=1, armed=2, valid=4, scan fault=8, LED fault=16, Fn held=32, Jankó=64 |
| 9 | u8 | last command result: initial=0, accepted=1, rejected=2 |
| 10 | u8 | keyboard Fn trigger editor mode 0…2, not performance mode |
| 11 | u8 | transmitted-velocity start 1…10 |
| 12, 16, 20 | u32 each | snapshot sequence, RAM configuration revision, acknowledged request ID |
| 24, 28 | u32 each | scan and LED error counts |
| 32 | u32 | board's configured scan rate in Hz (not a measured rate) |
| 36 | u8 | submitted HID report length |
| 37, 38 | u8, i8 | performance mode (keyboard=0, MIDI=1), octave offset −10…+10 |
| 39, 40 | u8 each | MIDI channel (1), cleanup pending (0/1) |
| 41, 42, 43 | u8 each | calibration state, completed count, selected sensor (255 none) |
| 44 | u8 flags | calibration active=1, saved=2, supported=4 |
| 45 | u8 | calibration reason: none=0, timeout=1, invalid scan/USB=2, cancelled=3, storage=4 |
| 46, 47 | u8 each | storage flags (valid=1, pending=2, fault=4), slot (0/1/255 none) |
| 48, 52 | u32 each | MIDI overflow/error count, performance-mode change count |
| 56, 58 | u16 each | selected calibration hold elapsed ms, inactivity remaining ms |
| 60, 62 | u16 each | selected electrical calibration candidate upper/lower endpoints, zero if absent |
| 64, 68, 72 | u32 each | calibration generation, storage error, full profile generation |
| 76 | u16 | header size, 80 |
| 78 | u8 | selected keyboard transport: unreported=0, USB=1, Bluetooth slots 1/2/3=2/3/4, 2.4 GHz=5 |
| 79 | u8 flags | transport report-eligible=1, switching=2; zero when transport is unreported |

Transport selection is distinct from report eligibility. A selected but unpaired
Bluetooth slot reports waiting, not ready; neither flag proves reception by a
remote host. The wired SysEx connection may coexist with wireless keyboard output.
Unknown transport values/bits and unsupported snapshot magics are rejected.

M1 storage errors at offset 68 use `0x31001`–`0x3100c` for backend failures
(argument, context, unsafe, geometry, linker, busy, controller, record, unlock,
erase, program, verification, respectively). `0x3100d` means resume failed;
`0x3100e` means the save gate failed before writing. Both gate/resume failures
latch the storage fault flag and disable normal input; a deferred gate is not
an error. These codes do not change the frame layout.

Saved calibration does not imply a writable backend: a set saved bit and clear
supported bit (`flags=2` when idle) indicate imported read-only bounds. M1 uses
this combination without storage callbacks. With callbacks, saved bounds give
idle flags 6 and active flags 7. M1 provisional startup bounds give idle flags 4
and active flags 5: supported, but explicitly unsaved. The GUI and Fn+C can
initiate parallel calibration.
Factory bounds have calibration generation 0; a verified custom calibration
increments it. The saved bit alone must not imply a durable whole profile.
The supported bit describes the backend capability, not current permission to
write: a storage fault disables new calibration entry even with that bit set.
M1's `M1P2` journal independently supplies storage flags, slot,
generation and errors; absent/invalid records start at slot 255, generation 0.
Changes remain pending until the outer safety gate permits a verified save.
See [device storage](DEVICE_CONFIG_STORAGE.md) for ownership and failure rules.

Each sensor record begins at `80 + 17 * sensor`:

Readout and threshold fields use the same control domain. On M1 this is linear
per-key travel, 4096 released to 1 pressed; electrical ADC+1 readings and stored
calibration endpoints are separate. Huntsman retains its electrical-domain
readouts. M1 single-key captures use the same travel values as these GUI records.

| Record offset | Encoding | Meaning |
| --- | --- | --- |
| 0, 2, 4 | u16 each | raw value, press threshold, release threshold |
| 6 | IEEE-754 float32 | device-computed normalized velocity, 0…1 |
| 10 | u32 | completed velocity-fit count |
| 14 | u8 flags | velocity ready=1, result valid=2, fit pending=4, calibration hold=8, key down=16, calibration done=32 |
| 15 | u8 | base MIDI note; 255 means unmapped |
| 16 | u8 | base keyboard/keypad usage: 0 disabled, 04…DF key, E0…E7 modifier; physical Fn stays 0 |

The HID report begins at `80 + 17 * count`. Padding follows it. The final
u32 is the sum of all preceding little-endian u16 words. There are no unused
sensor records or fixed-size bitmaps. Layout/count zero represents no valid
scan layout and requires rate zero; otherwise the layout/rate/count/HID length
must match the target announced by READY. The GUI rejects an unknown target,
cross-board layout, invalid value, reserved bit, padding, size or checksum.
SysEx version 3 and `MTG4` are the only supported wire contract.

`cfg` commands are acknowledged **in this stream**, not as text: the snapshot
carries the request ID in field 20 and accepted/rejected in field 9, and only
the latest acknowledgment is retained. A host therefore needs `stream gui`
active to observe a command result, and must serialize commands.
`cfg key ID SENSOR USAGE` changes one base-layer keyboard mapping; arguments
are decimal. Fn and all physical Fn combinations remain fixed. Invalid sensor,
reserved usage (1…3), out-of-range usage, calibration or an active trigger
editor rejects the request. Accepted edits release output and require neutral;
the GUI verifies record byte 16 before considering the edit applied.
`cfg clean` requires a valid scan younger than 100 ms; acceptance confirms only
the bounded, CMD5-verified erase. Settings ACKs otherwise mean applied in RAM;
wait for storage valid with neither pending nor fault before unplugging.
Automatic saving needs neutral input and 250 ms without settings changes.
Outputs are released immediately on reset and defaults wait for a
new all-keys-released frame. The host finishes configuration ACK/readback before
switching to a capture stream.

## Per-key stream (`stream key`)

Loss-detecting 20-byte records at the hardware scan rate for one selected sensor, used
for keystroke capture and to reproduce the firmware's velocity fit on the host.
The host uses the board's configured scan rate from its preceding MTG4 snapshot
for velocity calculations; actual acquisition cadence still needs measurement.

| Offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `HKL1` |
| 4 | u32 | session ID from the command |
| 8 | u32 | record sequence, starting at 0 |
| 12 | u16 | raw sample of the selected sensor |
| 14 | u8 | selected sensor index |
| 15 | u8 flags | bit 0: first record; bit 1: stream overflow or acquisition/transport loss; bit 2: invalid sample or profile change |
| 16 | u16 | trigger threshold from the command |
| 18 | u16 | checksum: sum of the preceding 9 little-endian u16 words |

Records are never dropped silently. A full ring or reported acquisition loss emits a terminal fault record
once queued data drains. USB reset or session expiry stops the stream; the
host detects disconnect or stale telemetry and must open a new session.
See [the per-key stream](LAST_KEY_STREAM.md).
The GUI's 16384-sample host buffer likewise fails on overflow, rather than
silently deleting samples used for a velocity waveform.
When changing the selected sensor, the GUI stops the previous capture and
waits for its ACK before starting a fresh nonce-bearing capture. Typed packets
from the completed capture are not input to the new decoder. Within a capture,
bad framing, foreign nonces and sequence gaps fail; no byte-prefix recovery
or old-firmware fallback is supported.

## Flash dump (`dump read`)

Read-only main-flash window (including bootloader, application and stock user
storage), one 64-byte chunk per request. Addresses are bounded below both
`0x7f400` and the detected flash size minus 10 KiB. It
issues only the controller's read command and can never erase or program.

| Offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `HBD1` |
| 4, 8, 12 | u32 each | request ID, address, chunk size (64) |
| 16, 20 | u32 each | flash size, page size (512) |
| 24 | u32 | request status: 0 accepted, otherwise an SDK error |
| 32 | 4 × u32 | per-word read status; non-zero marks that 16-byte block invalid |
| 48 | 64 bytes | data, zero-filled for a failed block |
| 112, 116 | u32 each | part ID, die ID |
| 124 | u32 | reflected CRC-32 of bytes 0…123 (polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF) |

## Text replies

Best-effort ASCII LOG payloads, independently framed and allowed alongside
snapshots/capture. The bounded debug ring may drop logs; logs are never command
acknowledgments or sample data.

| Command | Reply |
| --- | --- |
| `version` | `build=vVERSION-TARGET git=HASH state=STATE` — project version, target and build-time Git provenance (LOG) |
| `git` | `git=HASH state=STATE` in the matching ACK, not the best-effort LOG stream |
| `menu status` | Fn/menu state: `fn`, keyboard trigger editor `mode`, `level`/`saved` actuation, `brightness`/`pwm`, `reset_confirm`, `ready`, `lower_muted`, `root`, `scale`, `music_page`, `janko`, `velocity_start`, `build`, `key`, `scale_name` |
| `status`, `scan status` | `SCAN phase`, `profile`, `count`, `transfers`, `frames`, `markers`, `errors`, `settled`, `valid`, `calibrated`, `stream_dropped`, optional `fault` |
| `light status` | `LIGHT phase`, `on`, `profile`, `transfers`, `frames`, `errors`, `calibrated`, `count`, optional `fault` |
| keyboard/config changes | `KEYS host`, `fn`, `mode`, `act`, `rapid`, `enabled`, `saved`, `revision`, then `RAW enabled`, `armed`, `valid`, `revision`; raw status notes automatic save after neutral |
| `help` | Command summary, including `stream ...`, `cfg ...`, `dump read`, `menu status`; RAlt/RCtrl are octave −/+ |

`HASH` is the full lowercase Git commit object ID (40 hex digits for SHA-1,
64 for SHA-256). `STATE` is `clean` or `dirty`; a build without usable Git
metadata reports `git=unknown state=unknown`. Dirty includes staged, unstaged,
untracked and submodule changes, excluding Git-ignored build artifacts.
The hash is embedded at build time, never read from the PC checkout at query
time. A dirty build names its base commit, not an exact source snapshot.

Send `git` as a COMMAND on control cable 1 after HELLO/READY, using the next
command sequence. Its ACK contains the complete ASCII result without a newline.
The query is read-only and works during snapshots, captures and calibration;
it neither stops telemetry nor changes settings. READY carries the same fields
so the GUI can display them without an additional query. `version` and
`menu status` also include them in debug output. Telemetry field layouts do not
change, and the factory HID compatibility version is not a source identifier.

## What the streams do not carry

- Timestamps. The GUI snapshot has a sequence, the per-key stream a sequence and
  and a session; neither is wall time.
- Historical samples. The GUI stream is latest-only; no whole-scan history is transmitted.
- Private flash contents in ordinary scan/GUI telemetry. Explicit `dump read`
  requests can return bootloader and user-storage contents, including calibration
  slots and serial data; treat dumps as private. Security/PFR, ROM, MMIO and
  secondary-ASIC storage are outside its read boundary.
- Host-side interpretation: velocity is computed on the keyboard, and the host
  reproduces the same window from the per-key stream instead of rescaling it.


The framing follows [USB-MIDI 1.0](https://www.usb.org/sites/default/files/midi10.pdf).
The experimental identifier is subject to the
[MIDI Association's SysEx ID policy](https://midi.org/new-midi-association-sysex-id-policies-as-of-oct-15-2025).
