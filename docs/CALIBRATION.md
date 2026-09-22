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
The shared save callback can defer while its board's storage gate is busy.
This retains the complete candidate in the save state, but does not extend the
five-second inactivity deadline or change active bounds. Cancellation and scan
validation still apply before each retry. Board storage capabilities and safety
gates are specified in the [porting guide](PORTING.md).

## Inspect a completed run

The GUI's selected-key panel shows **Sensor input**, **Released bound**,
**Bottom-out bound**, **Active span** and **Control reading** on every platform.
These are shared-application readbacks, not factory-page dumps. After calibration
reports completion and a saved generation, release all keys to check resting
inputs; hold individual keys fully down to compare them with their saved lower
bounds. Normalized control readings alone cannot reveal the electrical range.

**Export all sensor readings and bounds…** writes a read-only diagnostic JSON
report for every key. It is separate from threshold/mapping profiles and cannot
restore calibration. The default `.device-dump.json` extension is Git-ignored;
keep real device reports private. Polling pauses during full-rate waveform capture.
See the [shared readback protocol](TELEMETRY.md#sensor-and-calibration-bounds-calibration-read).

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
RAM or share velocity buffers. The separate board journal stores complete profiles.
It does not change active bounds until a complete save passes readback.
New bounds drive linear travel lighting and MIDI aftertouch. Schmitt thresholds
remain raw ADC values; calibration does not silently change press/release
settings or velocity scaling. Host threshold/mapping edits and committed
Fn-menu choices persist alongside calibration in whole-profile snapshots
([device storage](DEVICE_CONFIG_STORAGE.md)).

## Persistence

Only physical pages **0x78000 and 0x78200** are writable, and they carry the
whole-profile record. Both were independently verified as FF inside an original
free allocator block. The primary settings, serial number, bootloader and
application image are untouched by calibration.
See [record format, original-driver evidence and power-failure behavior](DEVICE_CONFIG_STORAGE.md).
Stock firmware may reclaim this previously unused space; keep private backups.

## GUI protocol

`cfg calibrate ID` and `cfg calcancel ID` use the existing decimal nonzero
request ID and ACK result (1 accepted, 2 rejected). Start ACK means the routine
started, **not** that flash was saved. Observe terminal state and generation.
Cancellation is idempotent. One outstanding request is supported.

The shared [MTG4 layout](TELEMETRY.md#gui-snapshot-stream-gui) is authoritative
for wire offsets. Header fields report state, completed count, selected sensor,
active/saved/supported flags, reason, selected hold time and candidate bounds.
Generation and storage errors are full 32-bit fields.

Each sensor record's state byte carries calibration hold (bit 3) and completion
(bit 5), allowing all keys on any supported board to be represented.
This bit is clear for completed keys and outside collection. The GUI colors
every active hold amber and displays their count. The selected-sensor/elapsed
fields show one representative hold (lowest sensor index), not a shared timer.

States: 0 idle, 1 release, 2 settle, 3 collect, 4 reserved (rejected), 5 save,
6 complete, 7 discarded, 8 storage failure. Flags: bit0 active, bit1 a valid
saved record loaded/written, bit2 firmware supports calibration. Pending values
are not active calibration. GUI telemetry remains latest-frame, about 30 Hz;
acquisition consumes hardware scans, not GUI frames.

## Validation status

See [Validation and limitations](VALIDATION.md) for native, compiled ARM and
physical evidence. Calibration tests cover independent holds, parallel
completion, cancellation, save failure and boot restoration.
