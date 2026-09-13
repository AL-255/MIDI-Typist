# Whole-keyboard CDC scan stream

HKS1, ASIC metadata, tick units and the supplied label maps are Huntsman-specific.
They live in the board-selected NXP transport, outside the shared application.
Another port must define matching host framing and geometry; see [porting](PORTING.md).

HKS1 whole-keyboard streaming is available in the complete
`huntsman` application. No periodic USB heartbeat text
is emitted.

## Operation

Current complete firmware starts scanning automatically after USB configuration.
Select `stream on`
to restore HKS1 after using GUI/compact/dump modes; every newly accepted A0
scan frame then produces one binary CDC record. No text-formatted
per-sensor loop, normalized values, interpolation, or repeated snapshots are
substituted for raw uint16 readbacks. Values are in ASIC sensor-index order,
with the metadata-selected 61/62/65 count; mapping is documented in
[KEYBOARD_RECOVERY.md](KEYBOARD_RECOVERY.md).

- `stream off`: stop binary output, keep the ASIC scanning, resume text commands.
- `stream on`: resume binary output on the next fresh scan; does not initialize
  the ASIC, reset anything or enable host keystrokes.
- `scan stop`: stop streaming and quarantine the scanner as before.
- Text logs/command responses are suppressed while streaming. Commands still
  execute; send `stream off` before `scan status` or other text diagnostics.
- Current host keyboard output is enabled by default and arms on neutral
  samples; `keys off` or GUI disable suppresses output independently of streaming.
- Wait for calibration to finish before selecting diagnostic stream modes;
  those mode commands are rejected while calibration is active.

There may be existing text/an in-flight text transfer before the first binary
record. The decoder resynchronizes on validated frame boundaries. Once binary
streaming owns CDC IN, text cannot be interleaved into its frames. After
`stream off`, an already-submitted binary batch finishes before queued text.

## Rate and buffering

The target is 8,000 complete keyboard records per second over high-speed CDC:
160 bytes × 8,000 = **1.28 MB/s**. The queue holds 32 records (4 ms at target
rate), and USB submissions batch up to four records/640 bytes. Dedicated SRAMX
buffers remain immutable while USB owns them; USB SRAM allocation is unchanged.
USB backpressure never blocks the scanner, watchdog or main loop. Entire new
records are dropped when the queue is full; there are no truncated records.
Disconnect/reset/explicit stream stop also discard queued records.

The achieved hardware scan rate is not established as 8 kHz. Production SPI is
8 MHz. Two header bytes plus 61/62/65 uint16 samples take at least 124/126/132 µs
on the wire, versus a 125 µs target period, before software/peripheral overhead.
The existing CTIMER2 scheduler retains its nominal 125 µs period. No unverified
SPI overclock or ASIC protocol change was introduced. Thus 62/65-key layouts
cannot reach 8 kHz unique full scans at that SPI rate, and even 61 has very
little margin. Full-speed USB likewise cannot be assumed to sustain this rate.

Use record ticks/sequence gaps to measure the achieved rate, not the nominal
timer or CDC baud setting. The line-coding baud is not a physical UART limit.
Dropped counters track enqueue failures and discarded queued records; they
are not proof of host receipt. USB cancellation/disconnect may lose an
in-flight batch too; sequence gaps expose missing observations.

## Wire format

[Telemetry](TELEMETRY.md#whole-scan-stream-stream-on) defines the 160-byte
little-endian HKS1 record, checksum, tick and drop fields. Values remain in
sensor order; invalid readbacks are retained and flagged, not clamped.
Ticks mark observation, not an independently measured ADC acquisition instant.
Startup-discard frames and non-A0 markers are not samples.

## Decode and inspect

For device-side selection and loss-detecting single-number output, see
[last-key streaming](LAST_KEY_STREAM.md), included in the current complete
firmware; unlike whole-keyboard display modes, it fails on data loss.

Capture CDC in raw mode to a binary file with a serial client. Avoid terminal
echo and newline transformations; drain/resynchronize on opening. Then:

```sh
python3 tools/decode_scan_stream.py capture.bin --hex
python3 tools/decode_scan_stream.py capture.bin --summary
# Or pipe already-raw CDC bytes to the decoder:
python3 tools/decode_scan_stream.py --hex < capture.bin
```

Rows are `sequence,tick,dropped,flags,raw0,...`. Without `--hex`, samples are
unsigned decimal, right-aligned in five-character fields (including the full
uint16 range); counters occupy ten characters. With `--hex`, samples use
four-digit hex. The decoder accepts arbitrary
USB/read chunk boundaries and skips malformed records/text. Terminal rendering
8,000 long rows/s may bottleneck the host; binary capture is preferable.

## Build/validation

Use the complete `huntsman` preset; see [Building](BUILDING.md) and
[Validation](VALIDATION.md). Decoder/ARM tests check framing, checksum,
queue overflow, FS/HS transfers and buffer immutability without a device.

## Latest-only live display

The stream is binary, not terminal-readable lines. From the repository root,
with permission to access the serial device, inspect the running stream using:

```sh
python3 -u tools/decode_scan_stream.py --live /dev/ttyACM0
```

Do not run two readers concurrently. To inspect text status instead, send
`stream off` before `scan status`; `stream on` resumes without reinitializing
the running scanner.

Live mode configures raw tty input and discards existing host tty input on
opening. It continuously drains/validates input on an independent reader
thread, retaining only one latest-report slot. The display samples that slot
at most 20 times/s (`--rate` overrides this), discarding intervening reports.
A slow stdout consumer does not stop serial draining or create a display
FIFO; there is no catch-up burst or repeat of an unchanged report. Reports
older than 250 ms since host reception (or two display intervals, whichever
is longer) are not newly printed. `--duration 5` provides a bounded test and
prints received/displayed counts to stderr. Ordinary file decoding without
`--live` still preserves all records.

"Latest" means the newest complete, validated report received by the host
when a row is selected. USB/firmware buffers, an in-flight transfer and the
terminal's own rendering have unavoidable latency; this is not a guarantee
of the sensor state at the instant pixels appear. The existing firmware's
transport queue can still contribute latency.

## Labelled ANSI block display

```sh
python3 -u tools/decode_scan_stream.py --bars /dev/ttyACM0
# Narrow terminal: start the visible window at raw sensor index 40.
python3 -u tools/decode_scan_stream.py --bars --start 40 /dev/ttyACM0
```

`--bars` implies latest-only live mode, including the independent draining
reader and default 20 Hz display cap. It redraws just two terminal rows in
place: a caption with each key name, then its colored block directly below.
No numeric sample row or scrolling history is emitted. The scale is fixed:
raw 1–4096 spans eight equal bins from blue `▁` to red `█`, with cyan/green/
yellow intermediate levels. Invalid raw values appear as magenta `!` rather
than being clamped. These are raw readings, not millimeters, calibrated travel
or a pressed/released indication; no autoscaling is performed.

Columns retain ASIC sensor-index order. Labels come directly from the same
production-derived grid/base-action tables used by firmware, including the
ANSI/ISO boot-time FN/right-Alt patch. They identify the base key, not its
current FN-layer action. Every displayed label is one ASCII character followed
by one space; each block below uses the same two-character cell. Letters,
digits and punctuation keep their usual labels. Special labels are `e` Escape,
`t` Tab, `b` Backspace, `c` Caps, `r` Enter, `_` Space, `m` Menu, `f` FN,
`^` Ctrl, `s` Shift, `a` Alt and `g` GUI. Left/right modifiers share a label;
their sensor positions distinguish them. International usages use `i` without
guessing localized keycap legends; the extra ISO backslash uses `\`.

Use a UTF-8, ANSI-compatible terminal. Each key/bar occupies two columns;
the full ANSI/ISO/JIS rows require 139/141/147 terminal columns respectively,
including the range/scale prefix and a spare column to avoid wrapping.
On narrower terminals only the fitting sensor window is shown; the caption
explicitly reports its zero-based inclusive range and total sensor count.
`--start` changes its first index, and resizing adjusts its length. Cursor
movement/line clearing prevent scrolling; wrapping is disabled during the
display and restored, together with color/cursor visibility and input tty
attributes, on normal exit or Ctrl-C. No firmware change or flash is needed.

Host tests cover all three label maps, raw scale boundaries, invalid values,
viewport resizing, in-place redraws, layout changes and cleanup. They verify
every label and bar occupies exactly two characters, including its trailing
space. Blocked-output tests check that intervening reports are replaced by
the latest report rather than replayed.
