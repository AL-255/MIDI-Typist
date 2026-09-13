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

The application emits one 1152-byte telemetry layout, including MIDI fields,
calibration status and per-key parallel-hold bits. Frames carry no version
number: a constant magic and the fixed size identify them, while the console
build identity records which application produced them. The complete field
table, the other streams and the text replies are documented in
[device telemetry](TELEMETRY.md); the calibration fields additionally appear in
[calibration](CALIBRATION.md).

`cfg` commands are acknowledged inside this stream - request ID and
accepted/rejected in the snapshot - so a host needs `stream gui` active to
observe a result and must serialize commands. Rejection, mismatched readback,
malformed telemetry or stale/disconnected CDC stops the GUI's worker and
cancels unsent queued changes.

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
