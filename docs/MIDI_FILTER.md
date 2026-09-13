# MIDI mapping, velocity pop filter and octave indication

The shared application implements the filter, default map and octave indicators.
This guide's physical keys, timing examples and commands describe the complete
`huntsman` build; see [porting](PORTING.md) for different layouts or rates.
No device access or reset is needed for the offline checks here.

## Mapping

The [user manual](../USER_MANUAL.md#default-notes-two-overlapping-playing-ranges) contains the default 43-key map. The upper
row spans Tab=C5 (72) through backslash=B6 (95); the lower row spans Left
Shift=C4 (60) through apostrophe=F#5 (78). These ranges intentionally overlap:
for example, Tab and M both send C5. Existing duplicate-note ownership rules
apply: first down starts the note, final release ends it, and aftertouch uses
the maximum travel of the held keys mapped to that note.

Left Shift is identified from its recovered modifier action, not as a printable
HID usage. It remains Shift in keyboard mode and becomes a configurable C4 note
in MIDI mode. Fn is a menu control; Right Alt/Ctrl shift octave, Left Ctrl/Alt
bend pitch down/up, Left Windows supplies modulation, and Space supplies sustain. These controls
cannot be remapped to notes; wheel values do not use the velocity filter.
The GUI displays sharp note names and still accepts flat spellings as input.
It reads mapping state from the device. Importing a host profile replaces
defaults with that file's stored mappings.

## Bottom-out velocity window

Every key collects its press velocity from a window of consecutive ADC
readbacks that starts at the **triggering sample** (the first below the key's
press threshold) and grows until one of:

- ten readbacks are collected, or
- a readback crosses below the shared **bottom-out threshold** of 1500; that
  sample closes the window and is excluded, so very fast presses fit on as
  few as two readbacks. If only the trigger has been retained, include the
  next readback even when below 1500, ensuring one interval.

Decreasing ADC values indicate increasing press depth, so positive differences
mean positive press velocity. Intervals use `1 / layout.sample_hz` seconds;
Huntsman declares 1/8000 second. Because their durations are equal, filtering
raw differences is equivalent to filtering their counts/second rates.

The speed is the total drop divided by the interval count — `d(x)/count` —
multiplied by the declared scan rate. Windows longer than five samples
additionally apply the median interval filter: sort the differences, define
their median as the middle value (or mean of the two middle values), and discard exactly one
interval with the largest absolute distance from that median (earliest wins
ties). Shorter windows skip the filter entirely, so their estimate is exactly
the unfiltered mean. This deterministic rule applies even when no strong
outlier exists; it does not introduce an extra noise threshold or discard two
values.

The estimate is then clamp/normalized to 0…1 using the existing maximum of
4,500,000 counts/s. Fractions are preserved; there is no integer division
before normalization. The MCU supplies this float to the GUI and rounds it to
MIDI attack velocity 1…127. Signed nonpositive estimates normalize to zero,
but Note On still uses at least velocity 1 because zero-velocity Note On means
Note Off. Aftertouch calculation is unchanged.

Examples at Huntsman's declared 8000 Hz (intervals are signed canonical-count differences):

| Window | Intervals | Discard | Mean | Counts/s |
| --- | --- | ---: | ---: | ---: |
| 10 samples, 100, 1000, 100, 100, … | 1000 glitch among nine | 1000 | 100 | 800000 |
| 10 samples, 10, −500, 10, … | −500 glitch among nine | −500 | 10 | 80000 |
| 4 samples, 300, 300, 299 | — (no filter) | — | 899/3 | 2397333.333… |
| 10 samples, 0, 10×7, 20 | tied 0 vs 20 | first interval, 0 | 90/8 | 90000 |
| 10 samples, 20, 10×7, 0 | tied 20 vs 0 | first interval, 20 | 70/8 | 70000 |

This is an **interval-outlier** filter, enabled only above five samples. One
corrupted interior ADC sample can perturb two adjacent intervals; removing
exactly one interval cannot guarantee repair of arbitrary sample spikes.
Invalid samples outside the accepted ADC range still invalidate the raw frame
and cancel pending strikes. A newer press always owns the window: an
unfinished collection is discarded when the same key triggers again. Each
key's window, release-threshold rearming and pending state remain independent.
The existing 8 kHz assumption is not a measured scan-rate claim.

## Host capture consistency

`decode_scan_stream.py --last-key` collects the same window in the host
capture helper (triggering readback plus following values, cut before the
first below-1500 readback, ten maximum) and applies the same median-interval
gate. It prints signed raw counts/s to three decimals, rather than normalizing
or rounding away the fractional mean. All captured readbacks are still
printed; filtering changes the velocity estimate, not the data stream.
The host helper is fixed at 8000 Hz; do not use its velocity result as an oracle
for a differently timed board without adapting it. The configuration GUI
reproduces the same window math for its held keystroke captures; ordinary
telemetry still displays the float received from firmware without host-side
filtering. Telemetry is the 1152-byte GUI stream.

## Octave LEDs

Only the control matching the shift direction blinks: Right Alt for negative,
Right Ctrl for positive. It alternates blue (PWM 0,0,255 before global brightness)
and off with equal duty cycle. The
full period is `120 * (11 - abs(octave))` ms, for offsets limited to ±10. Thus
each additional octave strictly increases blink speed. At zero, neither has
an octave overlay and all five octave/wheel controls remain steady blue;
in keyboard mode the stored offset does not flash either key and all five use
inverse-travel lighting.
The persistent Enter mode marker remains; the held Fn+Enter mode-name display
takes priority. Existing lighting-off, invalid-scan and stale-frame blanking apply.
All channels come from the recovered per-profile LED map.

## Build and verification

See [Building](BUILDING.md) and [Validation](VALIDATION.md). Coverage includes
all default mappings, simultaneous notes, short/long windows, outliers, tie
ordering, fractional means and both octave-indicator phases across layouts.
