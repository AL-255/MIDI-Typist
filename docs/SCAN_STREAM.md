# Whole-keyboard visualization

The GUI draws the identified board's keys at their physical positions (61-key
Huntsman ANSI or 82-key M1) and refreshes readouts, press state and last velocity
from latest-only snapshots.
No separate terminal display or whole-scan binary stream is provided.

Snapshots travel over the dedicated MIDI SysEx control cable at no more than
one per 33 ms. A newer unsent snapshot replaces the older one; this display
is not a recording of every scan. Readbacks are 16-bit containers, valid
1…4096, with smaller values indicating deeper presses.
On M1 these are per-key normalized travel values, not electrical ADC readings;
calibration retains its separate electrical endpoints.

For every-acquisition capture of one selected sensor, use
[GUI hold mode](LAST_KEY_STREAM.md). See the authoritative
[wire layout](TELEMETRY.md) and [GUI guide](KEYBOARD_GUI.md).
