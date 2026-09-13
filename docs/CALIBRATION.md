# Per-key optical calibration

The parallel calibration state machine is shared by all ports. Physical
controls, 65-slot RAM sizes, telemetry fields and flash pages below describe the
`huntsman` build. A new board supplies sample normalization, endpoint storage
and LED placement through the [porting contracts](PORTING.md).

In keyboard mode, hold **Fn+C** to preview `CALIBRATION`, then release either
key to start, or connect the GUI and choose **Calibrate keys
→ device flash**. Keyboard output pauses for the routine. Physical entry
requires keyboard output enabled and armed; GUI entry also works when disabled.

1. Release all keys, including Fn+C. Purple indicates the release/settle phase.
   Keep the keyboard untouched for 500 ms. The next valid whole scan supplies
   each key's upper/rest endpoint.
2. Blue means not yet calibrated. Fully bottom out one or more blue keys and
   hold them for one second. Each held key turns amber independently, then
   green when registered. You may hold several together, start them at different
   times, and leave green keys held while pressing others. Include Fn, modifiers
   and space. Only the initial rest capture requires all keys released together.
3. After every key is green, the device saves and resumes operation once keys
   are released. Green remains briefly as confirmation. The GUI reports the
   saved generation. Red indicates cancellation, timeout or failure.

Five seconds without progress aborts and discards the staged result. **Cancel
calibration** in the GUI also discards it. Scan/USB failure aborts. Existing
active calibration remains unchanged on failure; no partial result is saved.
Configuration edits are rejected while the routine is active.

## Measurement choices

Readback decreases with force. Rest values must be at least 2048. Each candidate
must be at most half its own rest value. This rejects shallow touches, but cannot
prove the key has reached its mechanical stop: the user must fully press it.
Motion over 64 counts from a key's hold anchor restarts only that key's timer.
Releasing a pending key also resets only its own hold. Each key has separate
start time, anchor, sum and sample count; the mean during its stable one-second
hold supplies its lower endpoint. Already completed keys cannot register twice.
Noise alone does not indefinitely reset inactivity.

The routine stages separate endpoint arrays, a nine-byte completion bitmap,
and 65 independent hold registers. This 1324-byte state lives in a dedicated
writable section inside the existing application RAM image (0x20000000 region),
initialized explicitly at startup. It does not consume peripheral SRAMX or USB
RAM, share velocity buffers, or change the flash calibration record format.
It does not change active bounds until a complete save passes readback.
New bounds drive linear travel lighting and MIDI aftertouch. Schmitt thresholds
remain raw ADC values; calibration does not silently change press/release
settings or velocity scaling. Host threshold and MIDI-mapping edits are still
RAM-only with host JSON import/export. Calibration endpoints and the Fn-menu
settings persist on-device in the same two pages ([device storage](DEVICE_CONFIG_STORAGE.md)).

## Persistence

Only physical pages **0x7d400 and 0x7d600** are writable, and they carry both
the calibration part and the stored Fn-menu settings. Both were independently
verified as FF inside an original free allocator block. The primary settings,
serial number, bootloader and application image are untouched by calibration
or by a settings mirror.
See [record format, original-driver evidence and power-failure behavior](DEVICE_CONFIG_STORAGE.md).
Stock firmware may reclaim this previously unused space; keep private backups.

## GUI protocol

`cfg calibrate ID` and `cfg calcancel ID` use the existing decimal nonzero
request ID and ACK result (1 accepted, 2 rejected). Start ACK means the routine
started, **not** that flash was saved. Observe terminal state and generation.
Cancellation is idempotent. One outstanding request is supported.

Telemetry packets are 1152 bytes, with the following calibration status fields:

| Offset | Little-endian field |
| --- | --- |
| 1112–1115 | uint8 state, completed count, selected sensor (255 none), flags |
| 1116, 1118 | uint16 hold elapsed ms (0–1000), inactivity remaining ms (0–5000) |
| 1120 | 9-byte completed bitmap, sensor order |
| 1129 | reason: 0 none, 1 timeout, 2 invalid scan/USB, 3 cancelled, 4 storage |
| 1130, 1132 | selected candidate upper/lower uint16 endpoints; zero if absent |
| 1134 | reserved uint16 zero |
| 1136, 1140 | uint32 saved generation, storage error |
| 1144 | reserved uint32 zero |
| 1148 | unchanged uint32 checksum of preceding uint16 words |

Telemetry additionally uses bit 3 (value 8) of each per-key state byte at 967+i
to indicate an active calibration hold. Velocity bits 0–2 retain their meaning.
This bit is clear for completed keys and outside collection. The GUI colors
every active hold amber and displays their count. The selected-sensor/elapsed
fields show one representative hold (lowest sensor index), not a shared timer.

States: 0 idle, 1 release, 2 settle, 3 collect, 4 legacy wait key release (not
emitted by parallel calibration), 5 save,
6 complete, 7 discarded, 8 storage failure. Flags: bit0 active, bit1 a valid
saved record loaded/written, bit2 firmware supports calibration. Pending values
are not active calibration. GUI telemetry remains latest-frame, about 30 Hz;
acquisition consumes hardware scans, not GUI frames.

## Validation status

The latest complete application is `huntsman` (alias `keyboard-fn-menu`). Native tests cover
independent parallel holds, timing, aborts, record validation and simulated
power-cut boundaries. Compiled ARM tests cover Fn+C/GUI entry, output isolation,
parallel completion, two-page save/reload and original-controller register
differentials. The Fn-menu audit also checks that trigger/brightness edits
and calibration cancellation issue no writes and preserve a loaded record.

Build artifact: `build-keyboard-fn-menu/huntsman_firmware.bin`, 131072 bytes,
SHA256 `7aad237327434f5484800affd232b2cc175b694bd45f8ff7ea1f384136a082bc`.
Application load is 78056 bytes, SRAMX 24328/24576 and USB RAM 15488/16384;
the separate 8 KiB stack is retained. Twelve native suites, original lighting-channel
comparisons, and compiled USB/MIDI/lighting checks pass, including inverse
brightness, live MIDI mapping masks, right-side octave controls and fixed-range
modulation/pitch wheels. Enter and the MIDI controls use full channel intensity;
native tests cover all 20 global brightness levels and compiled I2C checks
compare their scaled output with ordinary note keys. These are software/register-model results.
Startup/RESET pairs are press 3500 / release 3600. Native and compiled tests
check exact crossing/equality behavior, independent of explicit test or GUI pairs.
The keyboard override tests cover four arrow usages without right modifier bits,
all twenty Fn shortcuts, both release orders, Fn-held repeated taps and green
hint channels. MIDI behavior and original trigger-editor differential tests pass.
Fn+Left Shift tests exercise physical Caps/Shift-row MIDI filtering across all layouts,
custom mappings, release-only toggles, pending-note cleanup, dark note LEDs,
mode persistence and unchanged keyboard/wheel behavior. Compiled checks
exercise the MIDI-only white Left Shift hint, actual USB note packets, mapping retention,
top-row output and LED channel masks with synthetic input.
Root/scale tests cover all 120 combinations and 128-note interval predicates,
modal selection/cancellation, physical selector tables, custom mappings,
filtered USB packets and matching LED output. The GUI/Tk tests confirm the
3500/3600 defaults and Space's reserved sustain control. Sustain tests cover
all layouts, exact Schmitt boundaries, blue lighting, CC64/note ordering,
queued pedal edges under backpressure, Fn/cleanup rearming and overflow pedal-off.
These checks do not represent physical root/scale or sustain keypresses.

The supplied updater has installed this application through computer-initiated
bootloader entry. Full 131072-byte readback at physical `0x8000` matches the
build SHA256 above with no flash-controller/ECC read errors. Both calibration
tail pages are byte-for-byte unchanged. After reboot, live GUI telemetry
advances with 61 valid sensors, enabled/armed output, calibration generation 0
(no saved calibration) and zero scan, lighting, MIDI or calibration errors.
Live readback confirms press 3500 / release 3600 on every sensor. The device
returns at USB high speed with keyboard, MIDI and CDC interfaces.
Builds and offline tests themselves do not access the device.
The SDK-free simulator additionally validates the shared lifecycle with 104
sensors, a different scan rate and ADC polarity, a wider HID report, layout
changes and independent parallel calibration.

Fn+R RESET requires a fresh Y confirmation after all keys are released; N or
simultaneous Y/N cancels. Tests cover pre-held Y rejection, full-brightness
confirmation colors and repeated brightness taps with Fn held. RESET storage
is validated in native and compiled flash models, including both
occupied slots, ownership/error guards and interrupted-erasure cases. It is
not executed against the user's saved calibration during hardware validation.

Physical key-combination presses, wheel travel/response in a synthesizer,
text-animation appearance and release latency, acquisition cadence,
saved-record cold boot, endurance and power-cut recovery remain unverified on
this build. A modeled acquisition is not a physical key-holding test. Private
endpoint exports and flash backups remain excluded from Git.
