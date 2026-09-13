# MIDI and GUI protocol

MIDI event encoding and `cfg` command validation belong to the shared
application. USB interface numbers, endpoints, GUI telemetry serialization and ANSI
JSON geometry below describe the Huntsman port. Other platforms must provide
their own transport/host adapter; see [porting](PORTING.md).

## USB-MIDI 1.0

The existing composite descriptors are unchanged: Audio Control interface 1,
MIDI Streaming interface 2, bulk OUT `0x02`, bulk IN `0x82`. The firmware emits
four-byte USB-MIDI event packets on cable 0, MIDI channel 1:

| Event | Byte 0 (cable/CIN) | Byte 1 | Byte 2 | Byte 3 |
| --- | --- | --- | --- | --- |
| Note On | `09` | `90` | note 0…127 | velocity 1…127 |
| Note Off | `08` | `80` | latched note | 0 |
| Poly Key Pressure | `0A` | `A0` | sounding note | pressure 0…127 |
| Modulation | `0B` | `B0` | 1 | amount 0…127 |
| Sustain | `0B` | `B0` | 64 | 127 pressed / 0 released |
| Pitch Bend | `0E` | `E0` | value & 127 | value >> 7 |
| Cleanup All Sound Off | `0B` | `B0` | 120 | 0 |
| Cleanup All Notes Off | `0B` | `B0` | 123 | 0 |

Pitch bend spans 0…16383 with center 8192 (`00 40`). Cleanup starts with
sustain off, then all 128 Note Offs, CC120, CC123, modulation zero and centered
pitch (133 packets). See [wheel scaling and scheduling](MIDI_DESIGN.md#modulation-and-pitch-wheels)
and [sustain ordering](MIDI_DESIGN.md#sustain-pedal).

This is MIDI 1.0, not MIDI 2.0 UMP, MPE, channel pressure or raw UART MIDI.
Note names are a GUI convention: C0=12, middle C/C4=60. Flat spellings are
accepted; the GUI displays sharps to match the default mapping. Inbound MIDI packets are received
and the existing OUT endpoint is rearmed, but they do not control this
application's synth, mapping or lights. There is no built-in synthesizer.

## CDC commands

Use raw serial I/O and serialize commands, because snapshots acknowledge only
the latest command ID. IDs must be nonzero decimal uint32 values. Commands
are newline-delimited ASCII and do not require a meaningful UART baud rate.

```
version
stream gui
cfg get ID
cfg set ID SENSOR PRESS RELEASE
cfg all ID PRESS RELEASE
cfg enable ID 0_OR_1
cfg midi ID SENSOR NOTE
cfg velocity ID LEVEL
cfg clean ID
cfg calibrate ID
cfg calcancel ID
```

`version` is a console query, not a `cfg` command: it answers
`build=v0.1.0-RZ03-0499`, the project version plus the build target of the
running application. It is answered before any other parsing and also works
while calibrating; `menu status` repeats the same string. Text replies are only
available while no binary stream is active, so hosts stop any stream left
running by a previous owner (`stream off`), query the identity, and only then
select `stream gui`.

`cfg clean` is the **cold boot**: it erases both authorized pages (the saved
calibration and the stored Fn-menu settings), exactly like Fn+R, and answers
result 1 only after reading them back blank. Defaults then apply on the first
neutral frame. `tools/flash_application.py` sends it after every flash so a new
build cannot inherit the previous build's state; `--keep-settings` skips it.

`cfg velocity` sets the transmitted-velocity start of
[the Fn+V editor](MIDI_DESIGN.md#transmitted-velocity-start): `LEVEL` is 1…10,
where 1 transmits the measured velocity unchanged and 10 transmits full
velocity for every note. It is rejected outside that range and accepted in
either performance mode, since it only shapes MIDI output and the host cannot
toggle MIDI mode itself. The new value appears in telemetry offset 6, so the
GUI checks both ACK and readback.

`cfg midi` accepts 0…127 or 255 (unmapped). It rejects unsupported sensor
indices and Fn, Left Ctrl/Windows/Alt, Right Alt/Ctrl and Space controls. The current identified profile
must exist. It releases active output, cancels pending strikes, increments the
shared RAM configuration revision and changes one mapping. On success, the
snapshot contains the exact new value; the GUI checks both ACK and readback.

`cfg enable` governs HID and MIDI performance output, not raw scanning or the
velocity monitor. Threshold edits and all-key application retain their existing
Schmitt validation and neutral-arming rules. Threshold/mapping/enable edits do
not write flash, enter the bootloader or reset the MCU. `cfg calibrate` starts
the keyboard-mode calibration routine; only completion of all keys saves
endpoints to the two authorized tail pages. Its ACK means accepted, not saved.
`cfg calcancel` discards the staged attempt. While calibrating, other config
edits are rejected; `cfg get` remains available. See [calibration](CALIBRATION.md).

The GUI allows one outstanding command, with a 3 s ACK timeout and no automatic
retry. Rejection, mismatched readback, malformed telemetry or stale/disconnected
CDC stops the worker and cancels unsent queued changes. Bulk profile import is
not atomic across all commands: already acknowledged changes remain if a later
command fails, and output may remain disabled. Reconnect and inspect the device
before deciding whether to apply again.

## GUI telemetry

Current firmware emits one 1152-byte telemetry layout, including MIDI fields,
calibration status and per-key parallel-hold bits. See
[calibration protocol](CALIBRATION.md).

Frames carry no version number: the constant magic and the fixed size identify
the layout, while the build identity above records which application produced
them. This is a coordinated firmware/host contract — a layout change updates
this table, the decoder and the recorded identity together. 1152-byte,
little-endian, latest-only snapshots, no faster than one per 33 ms:

| Offset | Encoding | Meaning |
| ---: | --- | --- |
| 0 | 4 bytes | `HKG` and a NUL byte: constant frame magic |
| 4 | u16 | 1152 |
| 6 | u8 | Fn+V transmitted-velocity start, 1…10 (1 = 0%, 10 = 100%) |
| 7, 8 | u8 each | profile 0…3, count 0/61/62/65 |
| 9 | u8 flags | enabled=1, armed=2, valid=4, scan fault=8, LED fault=16, Fn held=32, Jankó layout=64 |
| 10 | u8 | last result: initial=0, success=1, rejected=2 |
| 11 | u8 | legacy Fn editor mode 0…2, **not** performance mode |
| 12 | u32 | GUI sequence |
| 16 | u32 | RAM config revision |
| 20 | u32 | last command ID |
| 24, 28 | u32 each | optical/LED error counts |
| 32 | 65 × u16 | raw samples |
| 162 | 65 × u16 | press thresholds |
| 292 | 65 × u16 | release thresholds |
| 422 | 9 bytes | sensor-down bitset |
| 431 | 16 bytes | last accepted NKRO USB report |
| 447 | 65 × float32 | normalized device velocity, 0…1 |
| 707 | 65 × u32 | completed velocity fit counts |
| 967 | 65 × u8 | velocity ready=1, valid=2, pending=4; calibration hold active=8 |
| 1032 | u8 | performance mode: keyboard=0, MIDI=1 |
| 1033 | i8 | octave offset −10…+10 |
| 1034 | u8 | MIDI channel, currently always 1 |
| 1035 | u8 | MIDI cleanup pending, 0 or 1 |
| 1036 | 65 × u8 | base note per sensor; 255=unmapped |
| 1101 | 3 bytes | zero padding |
| 1104 | u32 | MIDI queue-overflow count |
| 1108 | u32 | performance-mode change count |
| 1112 | 36 bytes | [calibration state, completion bitmap, generation/error and reserved bytes](CALIBRATION.md#gui-protocol) |
| 1148 | u32 | sum of the preceding 574 little-endian u16 words |

Unused sensor slots are zero, including MIDI mapping padding; **active** unmapped
sensor slots are 255. This checksum detects framing errors, not authentication.
The decoder validates the magic, frame size, reserved bytes, value ranges and
padding, and resynchronizes by dropping bytes until the next magic. Velocity is
float32. Pressure is transmitted over MIDI, not duplicated as another GUI sensor
array. The armed flag reflects the raw engine; calibration can suppress HID
despite that flag. Use calibration state and the last submitted report to
interpret output.

## Host JSON

Version 1 remains threshold-only. Version 2 adds integer `midi` to every one of
the 61 ANSI key objects:

```json
{"sensor": 32, "label": "A", "press": 3500, "release": 3600, "midi": 60}
```

The surrounding object has `version: 2`, `layout: "ansi"`, and `keys` containing
all 61 unique, correctly labelled sensors. Notes are 0…127 or 255; reserved
control keys must use 255. Invalid pairs, boolean numeric fields, duplicates,
wrong labels, missing entries and invalid MIDI values are rejected before
commands are queued. Importing a version-1 file leaves MIDI mappings unchanged.
The current firmware supports version-2 JSON. Mode and octave are transient
performance state; calibration
records are separate. Neither is part of the host threshold/mapping profile.
