# Keyboard and MIDI performance design

The processing engine is shared across platforms. Unless stated otherwise,
physical key positions, memory figures and USB/GUI framing here describe the
Huntsman board. New platforms use the same engine through the [porting API](PORTING.md).

Current Huntsman application: `huntsman` (alias `keyboard-fn-menu`). See
[current validation](VALIDATION.md) and
[filter design](MIDI_FILTER.md).
This is application behavior, not a claim that the stock firmware implements
MIDI. Existing production-derived sensor/LED maps and board initialization remain
the hardware reference. MIDI did not add a peripheral reset sequence.
Calibration uses a separately bounded two-page flash writer.

## Ownership and scan flow

`keyboard_raw.c` owns per-sensor Schmitt state and independent bottom-out
velocity registration. `keyboard_midi.c` owns performance mode, MIDI mapping,
octave, pending strikes, note ownership and MIDI transmission scheduling.
`keyboard_app.c` owns the shared frame/service lifecycle and storage callbacks.
The Huntsman board's `keyboard_live.c` joins that lifecycle to the optical,
USB and lighting services; `keyboard_command.c` handles common configuration.
See [architecture](ARCHITECTURE.md) for the SDK-free board contract.
`keyboard_menu.c` owns all held-shortcut previews and dispatches actions on release.
Brightness K/L can be tapped repeatedly with Fn held without rearming host
output. RESET opens a Y/N confirmation and remains output-suppressed until
confirmed or cancelled; only a fresh Y press after neutral can clear storage.
The USB stack continues to own a stable four-byte MIDI IN transfer buffer.

```
valid optical frame → raw Schmitt edges and per-key velocity windows
                   → common Fn preview/release dispatcher (output suppressed during preview)
                   → calibration / trigger / mode / brightness / RESET / root / scale / row actions
                   → keyboard: existing NKRO/Fn engine
                   → MIDI: delayed strikes + pedal edges → ordered Note On/Off/CC64 queue
                           held-note travel → latest poly-pressure values
main loop → queued note/pedal events first → latest wheels → changed pressure → NXP USB IN
```

The Huntsman MIDI state is 1540 bytes, with fixed capacities and no dynamic allocation.
Other boards select their sensor, light-frame and HID capacities at build time.
The board has separate 24 KiB SRAMX, 16 KiB USB SRAM and 8 KiB stack budgets;
the linker reports current usage. Calibration and persistence state use
explicitly initialized application-image RAM. The final 1 KiB image reservation
stays unused; the firmware binary is 128 KiB.

## Mode and key routing

Power-on mode is keyboard. Hold Fn, then press Enter (or press both in the same
scan) to preview the target mode. An Enter key already held before Fn needs a
fresh press. Preview clears HID output, cancels MIDI strikes, schedules note
cleanup and invalidates raw arming. Releasing either member switches mode once;
faults or configuration changes cancel without switching. All keys must be above
their individual release thresholds before arming again.

Keyboard mode applies the [keyboard shortcut overrides](FN_MENU.md#keyboard-shortcuts)
above the recovered base/Fn action maps. MIDI mode does not invoke
that HID/configuration engine for raw edges, so performance keys do not type
letters or activate the legacy Fn+Tab/Caps editor. The GUI uses a separate
performance-mode field, distinct from its Fn editor-mode field.

Fn, Left Ctrl/Windows/Alt and Right Alt/Ctrl are reserved MIDI controls.
Right Alt/Ctrl decrement or increment a signed octave offset once per down
edge, limited to −10…+10; Fn suppresses these edges.
Simultaneous opposite edges cancel. The offset survives mode switches and reboot
after automatic save. Transposition is applied when a strike starts. A held or pending
strike keeps its latched note even if the octave changes later. Out-of-range
transposed notes are silent, not wrapped or clamped to another pitch.

## Modulation and pitch wheels

Left Windows sends channel-1 modulation, CC1 (`B0 01 value`, USB CIN 0xB).
Left Ctrl bends down and Left Alt bends up, combined into one channel-1
14-bit pitch bend (`E0 LSB MSB`, USB CIN 0xE). These encodings follow the
[MIDI control-change table](https://midi.org/midi-1-0-control-change-messages)
and [channel-message table](https://midi.org/expanded-midi-1-0-messages-list).

Each input uses `depth = clamp(3800 - raw, 0, 2800)`. Modulation is
`round(depth * 127 / 2800)`. Pitch uses signed depth `Alt - Ctrl`, summed
before rounding: negative depth spans 8192 down to 0, positive depth spans
8192 up to 16383. Equal inputs cancel exactly to 8192. The synthesizer owns
the semitone range; firmware does not send an RPN bend-sensitivity setting.
Fixed wheel endpoints are independent of calibration, velocity, and the
configurable Schmitt thresholds. Wheels therefore react before a key-down
threshold is reached. Values outside the endpoints saturate; invalid scans
follow the normal output-invalidation/cleanup path.

The latest wheel registers update on every valid, armed MIDI scan. Fn returns
them to neutral. The main loop checks both once per millisecond and sends only
changed quantized values after ordered note events, before poly pressure.
Each pending wheel retains its latest value under USB backpressure; there is
no wheel FIFO or catch-up replay. USB acceptance updates the sent register,
while the SDK wrapper owns the immutable in-flight packet. Cleanup explicitly
sends modulation zero and centered pitch even after switching back to keyboard.
In keyboard mode, Left Ctrl/Windows/Alt remain modifiers; Right Alt/Ctrl send
Left/Right arrows. Right Shift sends Up and Menu sends Down. These keyboard
overrides do not change MIDI role detection or note mapping.

## Note mappings

Mappings are per raw sensor for the identified ANSI/ISO/JIS layout. Defaults
are assigned using the recovered base HID action, not guessed scan order.
All unmapped keys use sentinel 255; notes are 0…127. The current default has
43 mapped keys: Tab through backslash span C5–B6, and Left Shift through the
apostrophe key span C4–F#5. Left Shift is a note only in MIDI mode. The GUI supports ANSI
geometry; firmware behavior and native polyphony tests cover all three layouts.

## Physical row enable mask

The row gate intersects with the [root/scale filter](MIDI_SCALES.md), and is
bypassed by the Jankó layout (below).
Fn+E selects the root; Fn+S selects the scale. Both are table-driven modal
menus, with preview while a choice is held and commit on its release.
Only enabled, in-scale, in-range notes receive normal note backlighting.

### MIDI trigger point

MIDI mode uses the same per-key Schmitt pair as keyboard mode, but the press
threshold is normally left at its 3500 default because the calibrated
trigger-point editor is keyboard-only. Fn+Tab in MIDI mode therefore opens a
raw page: ten steps select one press threshold for every key, from the
bottom-out floor of the velocity window up to one count below the release
threshold (`keyboard_raw_press_level`: `1` deepest, `0` shallowest). Release
thresholds are preserved per key and the
stored pair is clamped to keep `press < release`. Deeper points stay safe for
velocity: when the trigger sits at or near the floor, the window keeps the
single follow-up readback that closes it, so a one-interval fit is still
recorded instead of no velocity at all.

### Transmitted-velocity start

A completed velocity estimate is normalized to 0..1 and transmitted as MIDI
velocity 1..127. `velocity_start` (1..10, default 1) moves the **start** of
that mapping: level 1 leaves the curve unchanged (`round(127 * v)`, at least
1), level 10 transmits 127 for every note, and the levels between use a floor
of `(level-1) * 127 / 9` and scale the remaining range:
`velocity = floor + round((127 - floor) * v)`. The keyboard honours the
measured dynamics at the low end while guaranteeing a minimum attack for
quiet or partially-travelled presses. Fn+V opens the ten-step editor; the
value applies to every note key (including Jankó mode), is reported by
`menu status` as `velocity_start=1..10`, and persists automatically with the
other committed Fn-menu settings.

Fn+J toggles the built-in **Jankó layout**, a replacement note
mapping for the letter, number and punctuation rows: two whole-tone rows
staggered against each other, so the physical keys form the arrangement
requested for this keyboard. The table lives in `keyboard_midi.c` keyed by
HID usage, so it stays portable; the two Shift keys are matched by their
modifier mask, and keys outside the table (the bottom-row controls and the
remaining modifier roles) keep their configured mapping and role. Each row is a
whole-tone run: the number row ends at Backspace (C6), the Tab row at backslash
(C#6) and the Caps row at Enter (C6). Keys whose layout note is an accidental are
tinted yellow - the piano's black keys - keeping the travel intensity the
diatonic keys show, so both dim and brighten together; a key muted by the
root/scale filter stays dark like any other disabled note. The toggle uses the same
preview/release menu path, is available in MIDI mode only, aborts voices and
invalidates raw arming. While it is active the Fn hint on J turns green, telemetry
reports it in flags bit 6, and `menu status` prints `janko=1`. The configured
mapping is not modified; leaving the layout restores it exactly. The
root/scale filter still applies to Jankó notes, and **Fn+Left Shift is
ineffective while the layout is active**: the lower rows always play.

Fn+Left Shift toggles a `lower_muted` flag via the shared preview/release menu; the flag persists with the other committed Fn-menu settings.
The layout setup caches the Caps/Shift rows in a sensor bitmap through the
board's `keyboard_lower_group` query. Huntsman uses nine bytes and physical
IDs 0x1e..0x39, including ISO/JIS extras; the application does not assume those
IDs for other boards. Note creation and the LED
mask share `note_enabled`; arbitrary GUI mappings cannot bypass the physical
row gate. The Esc/Tab rows and bottom row are unaffected. Enter's explicit
blue indicator remains visible even when its assigned note is muted.
Toggle aborts voices/pending strikes and invalidates raw arming, as for other
settings changes. Mapping arrays, velocity acquisition, thresholds, calibration,
octave and wheel roles are unchanged. The flag survives mode switches and
fault cleanup, but init/RESET clears it. It is not part of JSON or telemetry;
read `menu status` for `lower_muted`. The GUI continues to show assignments.

## Velocity and short strikes

For samples y1…y5 **after** the press threshold crossing:

```
d = [y1-y2, y2-y3, y3-y4, y4-y5]
outlier = earliest interval with largest abs(d[i] - median(d))
counts_per_second = (sum(d) - d[outlier]) / 3 * layout.sample_hz
normalized = clamp(counts_per_second / 4500000, 0, 1)
MIDI attack velocity = max(1, round(normalized * 127))
```

Huntsman declares a nominal `sample_hz = 8000`; other boards supply their rate.
For four values, median means the midpoint of the two middle sorted values.
Exactly one interval is discarded, even when all deviations tie. Fractions
are retained until float normalization. The filter adds no scan delay beyond
the bottom-out capture window. See [filter edge cases](MIDI_FILTER.md).

The MCU computes both the normalized float and the final MIDI byte. The GUI
does not normalize velocity. A Note On with velocity zero has Note Off semantics,
so the smallest strike is encoded as velocity 1. Release velocity is fixed at
zero; no release-slope measurement is claimed.

Each sensor has five pending-note slots allocated on press edges. A strike
latches its transposed note into the first free slot. Notes are queued when
that sensor's velocity window closes: at ten samples or on the bottom-out
condition, not after a fixed delay. The 8000 Hz velocity assumption is not a
measurement of acquisition cadence.

A per-key pointer and release-bit mask retain releases that occur before the
velocity fit completes. Such a short tap emits an ordered Note On followed by
Note Off at completion; it is not silently discarded. Its synthesized sound may
be very short or inaudible. A release and repress before the older fit completes
use separate slots; the newest press restarts the fit, and superseded taps use
that same sensor's completed fit when the window closes. Fits never borrow
samples or velocities from another sensor. A sixth overlapping strike cannot
be stored: it increments the MIDI error counter, cancels pending voices and
starts the cleanup sweep, then requires neutral input to re-arm. This uses the
same fail-safe as event-queue overflow rather than silently dropping a strike.
Invalid samples/config edits cancel all unfinished strikes.

## Polyphonic aftertouch

The message is **Polyphonic Key Pressure**, status `0xA0` on channel 1, not
channel pressure (`0xD0`). Every sounding pitch has its own pressure value.
Pressure increases with normalized optical travel, using the same lower/upper
endpoints and clamping as the travel normalization, then converting to 0…127.
Aftertouch increases with pressure; the inverse LED brightness does not change it.
This is a travel proxy, not an additional pressure sensor or calibrated force.
Saved user calibration overrides the recovered/fallback endpoints after scan
settling. See [calibration limits](CALIBRATION.md#measurement-choices).

Pressure is recomputed from the latest hardware frame. Changed values are
scheduled in fair note-number sweeps, at most one sweep start per 10 ms. A busy
endpoint retains its current immutable USB packet; unsent pressure is replaced
by newer values. Under bus load the update cadence can fall below 100 Hz. There
is deliberately no FIFO of old pressure samples. Note edges take priority.

When multiple keys map to the same pitch, a reference count merges their held
voices: the first key produces Note On, the last release produces Note Off,
and pressure is the maximum of their current values. A second held key on the
same pitch does not send a second Note On or steal the first key's velocity.
This prevents an early release from silencing another held key. Different
pitches remain independent; this is not MPE and does not allocate channels.

## Sustain pedal

Space is a reserved MIDI control selected by its base HID usage, across all
supported layouts. The raw engine's per-key Schmitt state drives channel-1
CC64: 127 for press, 0 for release. There is no velocity-fit delay or
half-pedal scaling. Root/scale and lower-row filters do not gate this control.
It uses the same steady blue overlay and brightness scaling as Enter.

The controller stores one boolean pedal state. Each edge enters the ordered
event queue shared with notes; unlike analog wheels, pedal transitions cannot
be coalesced. Within one scan, pedal changes are queued before note changes.
This allows a simultaneous pedal press and note release to arrive in that order.
Queue overflow uses the same explicit fail-safe as note overflow.

Fn forces pedal-off. A press during cleanup, or a pedal held through Fn, must
be released and pressed again before it can assert sustain. Aborting clears
the state and sends pedal-off first in the cleanup sweep. Keyboard-mode Space
remains normal HID Space; GUI note mapping/import rejects this reserved key
without rewriting existing host files.

## Backpressure, cleanup and faults

The 128-entry, three-byte event FIFO contains Note On/Off and sustain edges. USB
acceptance removes one event; rejection/busy leaves it queued. The vendor USB
wrapper owns the copied four-byte USB-MIDI event until completion. Pressure is
only serviced after the ordered queue is empty.

If that finite queue fills, the firmware increments the reported MIDI error
counter, stops the current performance state, cancels pending events/strikes,
invalidates raw arming, and enters cleanup. This is an explicit fail-safe, not
an unlimited lossless guarantee. Do not ignore a nonzero MIDI error count.

Mode changes, mapping edits, threshold/enable invalidation, scan faults/staleness
and USB resets also clear voice state and request cleanup. Cleanup sends
CC64=0 first, Note Off for all 128 pitches, then CC120 (All Sound Off), CC123 (All Notes Off),
CC1=0 and centered pitch bend on
channel 1. It is outside the ordinary queue and retries each packet when the
endpoint is busy. No new note events are produced until it finishes. Keys
pressed during cleanup require a fresh release/press edge afterward.

Repeated invalid frames do not restart the cleanup sweep. A host that stops
consuming MIDI must not trap the mode chord or block ordinary HID: the user can
still switch back to keyboard mode. Physical USB disconnection cannot deliver
Note Off to an absent host; cleanup resumes after configuration returns. The
synth/host must also handle device removal. Closing CDC alone does not affect
MIDI or keyboard operation.

## LEDs and configuration persistence

Holding Fn+Enter previews the next mode without switching: blue `MIDI` or green
`KEYBOARD`, matching Enter's target-mode hint. All word letters use 30% PWM, with one character at 100%
for 200 ms, followed by a 500 ms background-only pause between words. Repeated
letters occupy separate time slots. Release of either chord key executes the
mode change and stops the preview on the next valid scan; no letter or word must finish first. Enter otherwise remains
a persistent full-channel-intensity marker (green for keyboard, blue for MIDI),
matching unpressed note keys before global brightness scaling. Other keys keep
white inverse-travel PWM: lit at rest, dimming as pressed. In MIDI mode only
configured note keys receive this base lighting; unmapped non-control keys are dark.
The mask follows GUI edits without changing note/velocity/aftertouch behavior.
See [text renderer details](FN_MENU.md#interruptible-text-display).
Left Ctrl/Windows/Alt, Right Alt/Ctrl and Space use Enter's blue (PWM 0,0,255)
in MIDI mode, regardless of press depth. With a nonzero octave offset,
Right Alt (negative) or Right Ctrl (positive) instead blinks blue/off.
The on and off intervals are each
`60 * (11 - abs(octave))` milliseconds: the full period decreases from 1200 ms
at magnitude 1 to 120 ms at magnitude 10. The shortest half-period remains
longer than the existing 40 ms LED update period. The other controls stay
steady blue. Zero shift disables blinking; keyboard mode restores all
six keys' inverse-travel lighting. Brightness scaling still applies.
The mode word overrides ordinary travel lighting, octave markers and Fn hints.
The [Fn menu/editor](FN_MENU.md) otherwise overrides performance hints while active.
Fn suppresses new MIDI strikes; already sounding notes still release normally.
Fn+Enter and calibration entry are disabled inside a trigger editor.
The overlay uses recovered per-profile channels, not new GPIO or controller
initialization. Existing `light off`, invalid/stale frame blanking and transfer
ownership still take precedence.

Per-key note edits are acknowledged over CDC and invalidate held output, just
like threshold edits. Both persist in complete device snapshots after neutral
and 250 ms without changes. Version-2 host JSON also exports mappings and
threshold pairs, but not mode/octave. Primary stock settings and serial-number
data remain untouched. Calibration is included in every tail-page snapshot. The
calibration overlay takes priority while collecting keys, with independent
amber holds and green completion. See [storage](DEVICE_CONFIG_STORAGE.md).

## Validation boundaries

See [Validation](VALIDATION.md) and [Building](BUILDING.md). Native and compiled
tests cover shared pitches, short/overlapping strikes, packet ordering,
controls, backpressure, overflow cleanup, mappings and lighting.
