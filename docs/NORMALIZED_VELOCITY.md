# Firmware-normalized floating-point velocity

Normalization is shared application behavior. The `huntsman` build exposes
it over GUI telemetry; see [current validation](VALIDATION.md).

The keyboard collects a velocity window from the triggering sample onward
(ten readbacks maximum, closed early by the shared bottom-out threshold of
1500), computes the total drop divided by the interval count at the
board-declared rate, discards the furthest median interval only when more
than five samples were collected
([details](MIDI_FILTER.md)), then stores:

```text
normalized_velocity = clamp(raw_velocity / 4500000.0, 0.0, 1.0)
```

Velocities <= 0 become exactly 0.0; velocities >= 4,500,000 become exactly
1.0. Values between are linear. The register is a 32-bit float computed on
the MCU. Huntsman declares 8 kHz; the synthetic port uses 2 kHz. The
bottom-out window, per-key release rearming, newest-press window ownership,
validity and counters are shared application behavior. MIDI independently
converts this float to attack velocity 1–127, emitting each Note On when its
key's window closes. No physical scan-rate change is introduced by
normalization or calibration.

The GUI only decodes and formats the received float (three decimal places on
keys, six in the selected-key panel). It performs no scaling or clamping.

## Shared wire format

Each MTG3 sensor record contains a little-endian IEEE-754 float32 normalized
velocity at record offset 6, a completion counter at 10 and state flags at 14.
The result-valid flag distinguishes a completed zero from no result. The decoder
rejects NaN, infinity and out-of-range floats.
See [the complete wire layout](TELEMETRY.md#gui-snapshot-stream-gui).
The count-aware format supports up to 128 sensors; boards advertise their
layout and HID report size and allocate only the capacity they need.

## Build and tests

[Building](BUILDING.md) and [Validation](VALIDATION.md) cover clamping,
fractional fits, simultaneous captures, float32 encoding and invalid values.
