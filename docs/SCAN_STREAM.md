# Whole-keyboard visualization

The GUI draws all 61 ANSI keys at their physical positions and refreshes their
raw values, press state and last velocity from latest-only snapshots.
No separate terminal display or whole-scan binary stream is provided.

Snapshots travel over the dedicated MIDI SysEx control cable at no more than
one per 33 ms. A newer unsent snapshot replaces the older one; this display
is not a recording of every scan. Readbacks are 16-bit containers, valid
1…4096, with smaller values indicating deeper presses.

For every-acquisition capture of one selected sensor, use
[GUI hold mode](LAST_KEY_STREAM.md). See the authoritative
[wire layout](TELEMETRY.md) and [GUI guide](KEYBOARD_GUI.md).
