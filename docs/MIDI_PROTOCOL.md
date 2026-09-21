# MIDI and GUI protocol

MIDI event encoding and `cfg` command validation belong to the shared
application. SysEx sessions and count-aware GUI telemetry are shared services;
JSON profiles bind to board identity and layout. USB interface numbers and
endpoints below describe the complete Huntsman port. Other platforms provide
their own USB integration and geometry; see [porting](PORTING.md).

## USB-MIDI 1.0

The composite exposes two virtual MIDI cables: Audio Control interface 1,
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
accepted; the GUI displays sharps to match the default mapping. Inbound performance-cable packets are ignored. Cable 1 carries the GUI's
bidirectional SysEx commands and replies. There is no built-in synthesizer.

## MIDI SysEx commands

The GUI sends each command as a versioned SysEx COMMAND payload, without a
newline. See [the envelope, sessions and acknowledgments](TELEMETRY.md#sysex-envelope).
Serialize commands: snapshots retain only the latest nonzero decimal uint32 request ID.

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

The READY handshake supplies the build identity before streaming. Optional
`version` and `menu status` replies use separate LOG messages.

`cfg clean` explicitly resets custom settings and calibration, exactly like
Fn+R. It erases both authorized pages and answers result 1 only after CMD5
blank verification. Compatible firmware updates do not send it by default. It requires a valid scan less than
100 ms old, releases active outputs and cancels unfinished strikes. Defaults
apply only after a fresh all-keys-released frame; the erase ACK alone does not
mean that step has completed. Use Fn+R for an explicit reset; normal GUI flashing retains compatible custom saves.

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
Schmitt validation and neutral-arming rules. Threshold/mapping/enable edits
schedule a save after neutral and 250 ms without further changes; they do not
enter the bootloader or reset the MCU. `cfg calibrate` starts
the keyboard-mode calibration routine; only completion of all keys saves
endpoints to the two authorized tail pages. Its ACK means accepted, not saved.
`cfg calcancel` discards the staged attempt. While calibrating, other config
edits are rejected; `cfg get` remains available. See [calibration](CALIBRATION.md).

The GUI allows one outstanding command, with a 3 s ACK timeout and no automatic
retry. Rejection, mismatched readback, malformed telemetry or stale/disconnected
MIDI SysEx stops the worker and cancels unsent queued changes. Bulk profile import is
not atomic across all commands: already acknowledged changes remain if a later
command fails, and output may remain disabled. Reconnect and inspect the device
before deciding whether to apply again.

## GUI telemetry

The shared application emits count-aware MTG3 telemetry, including MIDI fields,
calibration status and per-key parallel-hold bits. SysEx version 3 identifies
the protocol; READY binds each snapshot to a board target and build identity. The complete field
table, the other streams and the text replies are documented in
[device telemetry](TELEMETRY.md); the calibration fields additionally appear in
[calibration](CALIBRATION.md).

`cfg` commands are acknowledged inside this stream - request ID and
accepted/rejected in the snapshot - so a host needs `stream gui` active to
observe a result and must serialize commands. Rejection, mismatched readback,
malformed telemetry or stale/disconnected MIDI SysEx stops the GUI's worker and
cancels unsent queued changes.

## Host JSON

Version 1 remains threshold-only. Version 2 adds integer `midi` to every one of
the 61 ANSI key objects:

```json
{"sensor": 32, "label": "A", "press": 3500, "release": 3600, "midi": 60, "keyboard": 4}
```

The surrounding object has `version: 4`, a board `target` (for example
`"RZ03-0499"`), numeric `layout: 1`, and `keys` containing every unique, correctly
labelled sensor for that board (61 on Huntsman ANSI, 82 on M1). Notes are 0…127 or 255; reserved
control keys must use 255. `keyboard` is a keyboard/keypad usage (0 or 4…231);
Fn must use 0. Invalid pairs, boolean numeric fields, duplicates,
wrong labels, missing entries and invalid MIDI values are rejected before
commands are queued. The GUI accepts only version-4 JSON bound to the exact board
target and layout. Mode, octave and calibration
persist in the complete device snapshot but are not included in host JSON.
