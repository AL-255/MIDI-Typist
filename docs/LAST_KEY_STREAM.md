# Per-key capture

In the GUI, select a key and enable **Hold first 20 pts of keystroke**.
The device pins that sensor and sends every acquired readback as HKL1 records
inside MIDI SysEx SAMPLES messages. Other keys do not change the selected sensor.

The GUI arms after observing release, captures 20 points including the
below-press-threshold trigger, then waits for release to rearm. Device status
snapshots and configuration edits pause during capture. Disable hold mode
to resume them. This does not stop the keyboard's performance output.

Each capture command carries a new nonce. Inner sequence numbers begin at zero;
missing, duplicated, invalid or overflow-marked records fail the capture.
Device buffering is 256 records, published in batches of up to 32.
The host uses a bounded MIDI callback queue and a 16384-sample buffer.
Overflow never silently discards waveform data. Reconnect to start a new session
after a fault; the GUI does not retry uncertain settings changes.

Velocity reproduction uses [the firmware's window and filter](KEY_VELOCITY.md).
Its declared timebase is 8000 Hz, not a claim of measured acquisition speed.
The physical Linux SysEx capture check delivers approximately 1.34 ksample/s.
See [telemetry](TELEMETRY.md#per-key-stream-stream-key) for fields and
[validation](VALIDATION.md) for evidence limits.
