# Device telemetry

The GUI uses bidirectional USB-MIDI 1.0 SysEx on **cable 1**, separate from
musical events on cable 0. No CDC/serial interface exists. USB endpoints remain
OUT `0x02` and IN `0x82`; the driver exposes two paired MIDI ports.

## SysEx envelope

`F0 7D 4D 54 01 KIND PACKED_BODY F7`

`7D` is the experimental/non-commercial SysEx namespace; `4D 54` is the
project tag "MT", not a registered manufacturer ID. Commercial/product
distribution requires an appropriate registered ID. Do not use another vendor's ID.

Before packing, the body is little-endian `session:u32, sequence:u32,
payload_length:u16, payload, crc32:u32`. CRC-32 uses reflected polynomial
`0xEDB88320`, initial/final XOR `0xFFFFFFFF`, over bytes
`7D 4D 54 01 KIND` followed by the unencoded header and payload.
Each group of up to seven body bytes becomes an MSB bitmask (bit i is byte i's
high bit), followed by those bytes with their high bits cleared. Unused mask
bits must be zero. Payloads are at most 1152 bytes; the largest SysEx is 1340 bytes.

| Kind | Value | Payload and meaning |
| --- | --- | --- |
| HELLO | 1 | Empty; new nonzero random session, sequence 0 |
| READY | 2 | ASCII `build=vVERSION-TARGET git=HASH state=STATE`; confirms session |
| COMMAND | 3 | One printable ASCII command, at most 96 bytes; no newline/NUL |
| ACK | 4 | Command dispatched, echoes command sequence; empty except `git`, which returns provenance |
| SNAPSHOT | 5 | One HKG snapshot, sequence 0 in envelope |
| SAMPLES | 6 | 1…32 consecutive HKL1 records, sequence 0 in envelope |
| LOG | 7 | Best-effort debug text, sequence 0 |
| ERROR | 8 | ASCII rejection reason, command sequence |
| DUMP | 9 | One HBD1 read response, sequence 0 |
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
For `cfg`, the GUI additionally requires matching HKG request ID, accepted
result and applicable readback checks. Timeout is 3000 ms, with no automatic
retry. Settings may already have applied when a response is lost.

USB event CIN 4 carries continuing three-byte groups; CIN 5/6/7 terminates with
one/two/three bytes ending F7. Cable-0 notes are serviced before control traffic.
A complete encoded outbound message remains immutable; each up-to-16-event
chunk is copied to a separate DMA buffer. Command execution and CRC processing
run in main, not in the USB ISR. Buffer/timing defaults live in `defaults.h`.

## Channels

| Payload | Size | Selection | Delivery |
| --- | --- | --- | --- |
| HKG snapshot | 1152 | `stream gui` | latest-only, at most once per 33 ms |
| HKL1 sample | 20 | `stream key THRESHOLD SESSION SENSOR` | every acquisition of the pinned sensor |
| HBD1 read | 128 | `dump read ID ADDRESS` | diagnostic, one response per request |

`stream off` stops binary telemetry. GUI and capture are mutually exclusive,
but LOG messages have independent framing and can accompany either.

## GUI snapshot (`stream gui`)

Latest-only: a newer snapshot replaces an unsent one, so gaps are expected and
only the newest state matters. 1152 bytes, little-endian, no faster than one per
33 ms. This table is the authoritative layout.

| Offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `HKG` and a NUL byte: constant frame magic |
| 4 | u16 | 1152 |
| 6 | u8 | Fn+V transmitted-velocity start, 1…10 (1 = 0%, 10 = 100%) |
| 7, 8 | u8 each | profile 0…3, sensor count 0/61/62/65 |
| 9 | u8 flags | enabled=1, armed=2, valid=4, scan fault=8, LED fault=16, Fn held=32, Jankó layout=64 |
| 10 | u8 | last command result: initial=0, accepted=1, rejected=2 |
| 11 | u8 | keyboard Fn trigger editor mode 0…2, **not** performance mode |
| 12 | u32 | snapshot sequence |
| 16 | u32 | RAM configuration revision |
| 20 | u32 | ID of the last command this snapshot acknowledges |
| 24, 28 | u32 each | optical and LED error counts |
| 32 | 65 × u16 | raw samples, ~3900 released … ~1000 fully pressed |
| 162 | 65 × u16 | press thresholds |
| 292 | 65 × u16 | release thresholds |
| 422 | 9 bytes | sensor-down bitmap |
| 431 | 16 bytes | last accepted NKRO HID report |
| 447 | 65 × float32 | normalized device velocity, 0…1, computed on the keyboard |
| 707 | 65 × u32 | completed velocity-fit counts |
| 967 | 65 × u8 | velocity ready=1, result valid=2, fit pending=4; calibration hold active=8 |
| 1032 | u8 | performance mode: keyboard=0, MIDI=1 |
| 1033 | i8 | octave offset, −10…+10 |
| 1034 | u8 | MIDI channel, currently always 1 |
| 1035 | u8 | MIDI cleanup pending, 0 or 1 |
| 1036 | 65 × u8 | base note per sensor; 255 = unmapped |
| 1101 | 3 bytes | zero padding |
| 1104 | u32 | MIDI event-queue or pending-strike overflow count |
| 1108 | u32 | performance-mode change count |
| 1112 | 32 bytes | [calibration state, completion bitmap and generation/error](CALIBRATION.md#gui-protocol) |
| 1144 | u8 flags | whole-profile storage: valid snapshot=1, save pending=2, fault=4 |
| 1145 | u8 | active storage slot 0/1; 255 means none |
| 1146 | u16 | low 16 bits of complete-profile generation (wraps) |
| 1148 | u32 | checksum: sum of the preceding 574 little-endian u16 words |

Unused sensor slots are zero, including MIDI mapping padding; **active** unmapped
slots are 255. Frames carry no version number: the constant magic and size
identify the layout, and the build identity below records which application
produced them. The decoder validates magic, size, checksum, reserved bytes,
value ranges and padding. The enclosing SysEx message supplies framing and protocol version;
CRC or payload validation errors fail the connection.

`cfg` commands are acknowledged **in this stream**, not as text: the snapshot
carries the request ID in field 20 and accepted/rejected in field 10, and only
the latest acknowledgment is retained. A host therefore needs `stream gui`
active to observe a command result, and must serialize commands.
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
The velocity calculation assumes 8000 Hz; actual acquisition cadence must be
measured and is not specified by the packet format.

| Offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `HKL1` |
| 4 | u32 | session ID from the command |
| 8 | u32 | record sequence, starting at 0 |
| 12 | u16 | raw sample of the selected sensor |
| 14 | u8 | selected sensor index |
| 15 | u8 flags | bit 0: first record; bit 1: stream overflow/transport fault; bit 2: invalid sample or profile change |
| 16 | u16 | trigger threshold from the command |
| 18 | u16 | checksum: sum of the preceding 9 little-endian u16 words |

Records are never dropped silently. A full ring emits a terminal fault record
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
