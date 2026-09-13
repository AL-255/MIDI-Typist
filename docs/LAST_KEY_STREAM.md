# Triggered 20-sample key capture

Device-side key selection and host capture are included in the complete
`huntsman` application. Wait for calibration to finish
before selecting diagnostic streams.
HKL1 sensor IDs and the host's fixed 8 kHz estimator describe this board,
not arbitrary platform ports. The shared MCU estimator uses its layout's rate;
see [velocity](KEY_VELOCITY.md) and [porting](PORTING.md).

## Usage

With the application installed, scanning running and no other CDC
reader, open the device directly:

```sh
python3 -u tools/decode_scan_stream.py --last-key --threshold 3600
python3 -u tools/decode_scan_stream.py --last-key --threshold 3600 --repeat
python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key
python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3700
python3 tools/decode_scan_stream.py /dev/ttyACM0 --last-key > values.txt
```

With `--last-key`, omitting the device defaults to `/dev/ttyACM0`, not the
interactive terminal. Specify a different device path if needed. To replay
HKL1 from a pipe or redirected stdin, explicitly supply `-`; interactive tty
stdin is rejected so a mode command cannot accidentally be sent to the console.
Other display modes default to stdin.

The user running this command needs read/write access to the device. The host
sets the tty raw and sends `stream key THRESHOLD SESSION`, where SESSION is a
random uint32 nonce. Matching START/sequence-zero metadata acknowledges the
new capture and prevents old queued data being mistaken for its samples.
The tool does not flush tty input, reset the keyboard, start/restart the ASIC,
enable host keystrokes, or change lighting. This firmware preset starts scanning
and lighting automatically after USB configuration.

The command optionally accepts a third argument, `stream key THRESHOLD SESSION
SENSOR` (0..64, or 255): a pinned session streams that sensor's raw on every
scan regardless of threshold crossings, so full-rate edges of one key can be
captured without first-press auto-selection. A sensor outside the active
layout is reported as an invalid session (HKL1 flags-4 fail-stop), never
silently. The configuration GUI uses the pinned form for its keystroke hold
mode; this CLI keeps the two-argument auto-selecting form.

Press means raw **strictly less than** the threshold (default 3800, range
1..4096). A new downward crossing selects that sensor. A sensor already below
the threshold on the session's first scan counts as pressed. If multiple
sensors cross in the same report, the lowest raw sensor index wins: sub-scan
ordering is unknown. Release is raw greater than or equal to the threshold.
There is no hysteresis or debounce in this selection rule.

The host prints and immediately flushes `Capture Armed` with the input path,
strict lower-than threshold, layout, sample count and report timeout. This
means it is waiting for the stream/trigger, not that the device has already
acknowledged the command. When the first sensor is selected, it prints e.g.
`Key: A (sensor 32)`, then **exactly 20 subsequent decimal readbacks**, one per
line, followed by a velocity estimate, and exits successfully. The trigger sample itself is excluded. Readings
include unchanged values and release; there is no rate limiting, interpolation,
latest-only replacement or skipped report within the requested interval.

Velocity uses the same bottom-out window as the MCU: the **triggering
readback** plus the following values, cut before the first sample below 1500,
ten readbacks maximum. The host script computes signed intervals
`d[i] = y[i] - y[i+1]`, and when more than five samples were collected
discards the interval furthest from their median (earliest wins ties); shorter
windows keep every interval, so the estimate is exactly `d(x)/count`.
The result is `8000 * mean(kept intervals)` **raw counts/second**, printed to
three decimal places. Positive means pressing/decreasing raw values, negative
means releasing, and flat readings give zero. Readings outside the window do
not influence the estimate. The result is printed after all twenty readings,
and only for a complete valid capture. The startup banner and result
explicitly identify the 8 kHz assumption. This is not calibrated
millimeters/second and does not use measured delivery timing: an actual 8 kHz
acquisition rate is not established.

This host estimator matches the Huntsman MCU estimator before normalization.

### Repeat captures

Add `--repeat` to keep the same stream/session open after each capture. Each
cycle prints the triggering key, its next 20 values and the bottom-out window velocity.
The host then consumes and validates every report without printing the held
key's additional values. When **that captured key** reads strictly greater
than the threshold, it prints `Capture Armed` with the release value and
trigger threshold, resets the capture and its velocity window, and waits for another
below-threshold press. Equality does not re-arm or trigger. If the twentieth
sample is already above threshold, it re-arms immediately after printing that
capture's velocity. A release earlier inside the fixed twenty-sample window
does not interrupt the capture or start overlapping captures.

After re-arming, continued released values do not retrigger; either the same
key or a newly selected key can trigger the next capture. During a capture
or while waiting for release, a selected-sensor change prints a `WARNING`
and `Capture Armed` restart message. The host discards the incomplete capture's
sample count and velocity window; already printed partial values remain in
the output but are explicitly marked as discarded by the warning. If the
previous capture was complete, the warning instead notes that its release
was not observed; its completed result remains valid. The new sensor's current
below-threshold value becomes a fresh trigger (excluded from its next 20
readbacks). If it is not below threshold, the host waits until it is. No
device mode restart or reflash is needed.

The process stays alive through completed captures and idle periods until
Ctrl-C (exit 130). Existing safety failures—data loss, overflow, corruption,
device disconnect, output stall or report timeout—still exit nonzero. EOF in
repeat replay is also an error, not a successful end of the ongoing capture.
Sequence/checksum validation continues through the unprinted held/released
reports; no intermediate reports are dropped or skipped by the receiver.
The GUI's pinned-sensor capture also checks sequence/checksum continuity; its
separate 16384-sample host buffer fails the connection on overflow. It does not
silently delete samples to keep a waveform running. See
[GUI hold mode](KEYBOARD_GUI.md#keystroke-hold-mode).

Key labels default to the ANSI layout verified on this board. Use `--layout
iso` or `--layout jis` for other physical layouts; HKL1 does not carry layout
metadata. The device's existing compact mode can select another newly pressed
key. The warning-and-restart behavior applies to both one-shot and repeat
captures; samples from different sensors never contribute to the same velocity
fit. Missing/truncated reports still fail. In one-shot mode, reports after the
completed 20-sample interval are outside the capture and are not consumed.
This behavior is host-only and uses the firmware's compact capture protocol.

`--buffer-frames` sets the pending host output limit (default 8192). Output is
nonblocking and may batch consecutive lines into writes without omitting any.
A full output queue fails immediately rather than blocking acquisition.
`--timeout` sets the maximum interval without a complete report (default five
seconds), including mode negotiation; a blocked output also times out. Valid
unselected reports keep the capture armed indefinitely until a trigger.
These options only apply to `--last-key`;
display modes, `--rate`, `--duration`, `--summary` and `--hex` cannot be combined
with it. Ctrl-C ends capture with status 130 (interrupted, not a successful
complete capture); the tty settings are restored on exit.

The keyboard remains in compact mode after the reader exits. Another
`--last-key` invocation starts a fresh capture session. To restore the existing
whole-keyboard HKS1 stream, send `stream on` through a CDC command client.
`stream off` stops either stream without stopping scanning. Do not run two
readers on the CDC device. Files or non-tty stdin can replay a captured HKL1
session; they send no command and must include its START record.

## Rate, buffering and failure behavior

The device performs selection on every accepted full optical scan and transmits
one **20-byte HKL1 report**, instead of the 160-byte HKS1 whole-keyboard report:
eight times less CDC payload. At a hypothetical 8,000 scans/s this is 160 kB/s
instead of 1.28 MB/s, excluding USB overhead. There is no changed SPI clock or
ASIC scheduler, and **this is not a claim of achieving 8 kHz**.

Both formats reuse the existing 5120-byte firmware queue and 640-byte stable
USB transfer buffer. Compact mode holds 256 queued reports and up to 32 in
flight. A compact queue overflow latches a fault, stops accepting samples for
that session, drains the intact queued reports and then sends an OVERFLOW
report—even if no further scans arrive. No implicit restart, overwrite,
GPIO retry or MCU reset is performed. USB cancellation/disconnection also
faults the session; a new explicit command is required to resume it.

The host exits with status 1 and an error on **stderr** for device overflow,
host output overflow, sequence gaps/duplicates, unexpected session changes,
bad framing/checksum, invalid raw values/layout changes, truncation, I/O
failure or report timeout. Once the session starts it never resynchronizes
past damaged bytes. Already-emitted numbers are a prefix of a failed capture,
not proof the entire run was lossless; consumers must check the exit status.
No host can reconstruct reports lost before capture starts or guarantee
detection of every possible corruption with a finite checksum. Loss checking
applies to reports in the acknowledged session, not device drops before the session or
unobserved internal ASIC conversions.

## HKL1 wire format

[Telemetry](TELEMETRY.md#per-key-stream-stream-key) defines the 20-byte
little-endian record and checksum. Sequence starts at zero and wraps at 2^32.
Before selection the sensor is 255 and raw value zero. Session defaults to
zero for manual commands; host tools supply a nonce.

## Build and validation

Use the complete `huntsman` preset and matching host tools. See
[Building](BUILDING.md) and [Validation](VALIDATION.md). PTY and ARM tests cover
session negotiation, trigger/rearm, mixed-key restart, velocity windows,
sequence loss, overflow, timeouts and GUI stream switching.
