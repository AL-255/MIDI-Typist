# Device telemetry

Everything the `huntsman` application reports to a host: the binary CDC streams,
the text replies on the same interface, and the build identity. HID reports and
USB-MIDI events are **output**, not telemetry; the last accepted HID report is
only echoed back inside the GUI snapshot. See
[USB/GUI protocol](MIDI_PROTOCOL.md) for the command surface and
[porting](PORTING.md) for which parts are board contracts.

## Channels

Only one binary stream is served at a time on the CDC IN endpoint, chosen by the
command in the last column. Text replies are available only while no binary
stream is active, so a host selects `stream off` before reading them.

| Stream | Magic | Record | Rate | Selected by | Consumed by |
| --- | --- | --- | --- | --- | --- |
| GUI snapshot | `HKG` + NUL | 1152 bytes, latest-only | ≤ 1 per 33 ms | `stream gui` | `tools/keyboard_gui.py` |
| Per-key samples | `HKL1` | 20 bytes, lossless | 8 ksps while a key is active | `stream key N [SESSION [SENSOR]]` | `tools/last_key_stream.py`, GUI hold mode |
| Whole-scan frames | `HKS1` | 160 bytes | one per scan frame (~8 kHz) | `stream on` | `tools/decode_scan_stream.py` |
| Flash dump | `HBD1` | 128 bytes | one per `dump read` request | `dump read ID ADDRESS` | `tools/dump_flash.py` |
| Text replies | — | newline-terminated ASCII | on request | `version`, `menu status`, `status`, `light status`, `help` | operators, ARM audits |

Switching streams resets the previous one. `stream off` stops binary output;
`stream gui` then `stream key ...` re-selects without re-enumerating USB.

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
| 11 | u8 | legacy Fn editor mode 0…2, **not** performance mode |
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
| 1104 | u32 | MIDI queue-overflow count |
| 1108 | u32 | performance-mode change count |
| 1112 | 36 bytes | [calibration state, completion bitmap, generation/error and reserved bytes](CALIBRATION.md#gui-protocol) |
| 1148 | u32 | checksum: sum of the preceding 574 little-endian u16 words |

Unused sensor slots are zero, including MIDI mapping padding; **active** unmapped
slots are 255. Frames carry no version number: the constant magic and size
identify the layout, and the build identity below records which application
produced them. The decoder validates magic, size, checksum, reserved bytes,
value ranges and padding, and resynchronizes by dropping bytes until the next
magic.

`cfg` commands are acknowledged **in this stream**, not as text: the snapshot
carries the request ID in field 20 and accepted/rejected in field 10, and only
the latest acknowledgment is retained. A host therefore needs `stream gui`
active to observe a command result, and must serialize commands.

## Per-key stream (`stream key`)

Lossless 20-byte records at the scan rate (8 ksps) for one selected sensor, used
for keystroke capture and to reproduce the firmware's velocity fit on the host.

| Offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `HKL1` |
| 4 | u32 | session ID from the command |
| 8 | u32 | record sequence, starting at 0 |
| 12 | u16 | raw sample of the selected sensor |
| 14 | u8 | selected sensor index |
| 15 | u8 flags | bit 0: first record of the session; bit 2: invalid sample or profile change |
| 16 | u16 | trigger threshold from the command |
| 18 | u16 | checksum: sum of the preceding 9 little-endian u16 words |

Records are never dropped silently: a full ring, an unready CDC endpoint or a
USB reset stops the session and sets the fault flag rather than losing samples.
See [the per-key stream](LAST_KEY_STREAM.md).

## Whole-scan stream (`stream on`)

One record per scan frame: every sensor's raw value, so a host can draw the
whole keyboard. Records are dropped whole and counted when the host falls
behind.

| Offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | `HKS1` |
| 4 | u16 | 160 |
| 6, 7 | u8 each | sensor count, profile |
| 8 | u32 | frame sequence |
| 12 | u32 | optical tick the frame belongs to |
| 16 | u32 | dropped-frame count so far |
| 20 | u8 | set when any sample in this frame was 0 or above 4096 |
| 24 | 65 × u16 | raw samples |
| 156 | u32 | checksum: sum of the preceding 78 little-endian u16 words |

## Flash dump (`dump read`)

Read-only window on the application's flash, one 64-byte chunk per request. It
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
| 124 | u32 | checksum of bytes 0…123 |

## Text replies

Newline-terminated ASCII, available only while no binary stream is active.

| Command | Reply |
| --- | --- |
| `version` | `build=v0.1.0-RZ03-0499` — project version plus board build target |
| `menu status` | Fn/menu state: `fn`, legacy editor `mode`, `level`/`saved` actuation, `brightness`/`pwm`, `reset_confirm`, `ready`, `lower_muted`, `root`, `scale`, `music_page`, `janko`, `velocity_start`, `build`, `key`, `scale_name` |
| `status`, `scan status` | `SCAN phase`, `profile`, `count`, `transfers`, `frames`, `markers`, `errors`, `settled`, `valid`, `calibrated`, `stream_dropped`, optional `fault` |
| `light status` | `LIGHT phase`, `on`, `profile`, `transfers`, `frames`, `errors`, `calibrated`, `count`, optional `fault` |
| keyboard/config changes | `KEYS host`, `fn`, `mode`, `act`, `rapid`, `enabled`, `saved`, `revision`, then `RAW enabled`, `armed`, `valid`, `revision` |
| `help` | Command summary, including `stream ...`, `cfg ...`, `dump read`, `menu status` |

`menu status` and `version` are the record of what the keyboard currently holds,
so hosts and audits read them instead of guessing from the binary streams.

## What the streams do not carry

- Timestamps. The GUI snapshot has a sequence, the per-key stream a sequence and
  a session, the scan stream a sequence and an optical tick; none is wall time.
- Historical samples. The GUI stream is latest-only and the scan stream is
  rate-limited by the host's consumption.
- Serial numbers, primary settings, calibration-flash dumps or any factory data.
  `dump read` is bounded to the application's window and never returns the
  protected tail of flash.
- Host-side interpretation: velocity is computed on the keyboard, and the host
  reproduces the same window from the per-key stream instead of rescaling it.

## Validation

```sh
cmake --preset host-tests && cmake --build --preset host-tests && ctest --preset host-tests
python3 -B tools/test_keyboard_gui.py         # GUI framing, fields, transport readback
python3 -B tools/test_last_key_stream.py      # HKL1 records and velocity reproduction
python3 -B tools/test_scan_display.py         # HKS1 frames and display model
python3 -B tools/test_dump_flash.py           # HBD1 framing, bounds, error priority
```

The offline ARM suites execute the compiled firmware and assert the same
framing end to end: `tools/test_keyboard_mode_arm.py`,
`tools/test_scan_stream_arm.py`, `tools/test_flash_dump_arm.py` and
`tools/test_calibration_arm.py`. They never touch hardware.
