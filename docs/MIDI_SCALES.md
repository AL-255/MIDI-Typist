# Root, scale and playable-note filtering

The complete `huntsman` application supports MIDI-only Fn+E root
selection, Fn+S scale selection and Fn+Left Shift lower-row muting. Defaults
are C/chromatic, both row groups enabled, raw press 3500/release 3600.
The [user manual](../USER_MANUAL.md#choose-a-root-and-scale) gives the complete
selector map and interaction instructions.

## Portable musical data

[midi_music.h](../firmware/app/include/midi_music.h) and [midi_music.c](../firmware/app/src/midi_music.c)
depend only on standard C integer/boolean types, not the SDK or board.

- `midi_scales` owns each scale's name, physical selector letter and 12-bit
  interval mask. Bit n means n semitones above the root. Readable `SEVEN` and
  `FIVE` initializers list intervals directly. Minor means natural minor.
- `midi_root_keys` owns the 24 fixed upper-piano HID-usage/pitch-class pairs;
  either octave selects the same root. These selectors deliberately ignore
  GUI remapping, octave transposition and the current playable-note mask.
- `midi_root_names` supplies text-renderer-compatible names. Sharps are
  spelled with `-SHARP`; chromatic uses `CHROMATIC`, avoiding unsupported
  digits or hash characters in the existing keyboard text renderer.
- `midi_music_config_t` stores only root (0..11) and scale table index.
  `midi_music_contains` validates both and the MIDI note range before testing
  bit `(note + 12 - root) % 12`. Chromatic's mask has all twelve bits set.

To extend a scale, change the scale enum/table, give it a unique selector,
add its interval oracle to the tests and document it. No per-scale branches
belong in MIDI rendering or USB code. Porting to another board requires
adapting physical key lookup/rendering, not rewriting interval membership.
The shared controller asks `keyboard_action` for base HID semantics and
`keyboard_lower_group` for row membership, then writes RGB through
`keyboard_light_set`. No Huntsman key-ID ranges appear in the musical code.
See [the platform guide](PORTING.md#2-describe-keys-independently-of-scan-order).

## One rule for notes and lights

```text
assigned note + octave offset
           |
           +-- unmapped or outside 0..127? -- reject
           +-- disabled physical row? ------ reject
           +-- outside root/scale mask? ---- reject
           |
           +-- eligible for Note On and ordinary note backlighting
```

`keyboard_midi.c` shares `note_enabled` between note creation and its LED
mask. Filtering never quantizes, retunes or edits a mapping. Octave changes
are whole octaves, so pitch-class membership stays the same, but out-of-range
notes become both silent and dark. Existing held notes keep their latched
pitch until release; selecting a root/scale performs cleanup first.
Enter's mode marker and the six wheel/octave/sustain markers are separate overlays,
not promises that a note assignment on those keys is playable.

`keyboard_midi_select_music` validates values, updates the music config,
increments the raw configuration revision, aborts voices/pending strikes and
requires all-neutral input. The existing cleanup sends Note Offs and neutral
controllers with bounded USB retry. Duplicate-pitch reference counts and
velocity windows are cleared together, preventing a delayed old strike from
appearing under a new filter. Thresholds, note mappings and calibration are
unchanged. The lower-row mute remains an independent intersecting filter.

## Table-driven menu integration

The `options` table in [keyboard_menu.c](../firmware/app/src/keyboard_menu.c) defines the
Fn selector's HID usage/modifier, keyboard/MIDI availability and preview word.
The same table resolves sensors, filters edges, recognizes keyboard settings
and paints Fn hints. A 16-bit Schmitt-history bitmap covers the ten options,
with a compile-time capacity check. New MIDI entries cannot consume ordinary
keyboard-mode E, S or Left Shift.

```text
Fn+E/S held: preview KEY/SCALE
       |
release either chord key
       |
modal page, wait for all-neutral
       |
one selector down: preview root/scale name
       |                             |
selector released: commit        Escape/fault: cancel
       |                             |
MIDI cleanup, release all keys, return to performance
```

Menu state records the page, initial/current selection, chosen sensor and
readiness separately from performance state. Raw values track a held choice
even while host output and raw down bits are invalidated. Equality to release
holds the choice. The page checks its entry revision and MIDI mode every
frame. Multiple simultaneous candidates do not choose a sensor-order winner;
they require a neutral retry. No GUI frame rate or delay loop controls the UI.

## Persistence and diagnostics

Root, scale and lower-row mute survive performance-mode switches and fault
cleanup, but init/RESET restores C/chromatic and both groups. They do not
write flash and are not serialized in host JSON or telemetry. Telemetry continues to
report assigned mappings and raw sensor state, not effective filter state.
After selecting `stream off`, `menu status` includes `root`, `scale`,
`key`, `scale_name` and `music_page` (0 idle, 9 root, 10 scale), alongside
`lower_muted`. Reopening the GUI selects GUI telemetry again.

## Verification

The native MIDI suite independently checks the ten interval lists for all
12 roots and 128 notes, selector uniqueness, all 120 root/scale output/LED
combinations, all layouts' menu choices, custom mappings, row-filter
intersection, held-choice hysteresis, simultaneous rejection, cancellation,
mode persistence, defaults and cleanup under USB backpressure.
Compiled ARM tests drive actual scan/CDC/MIDI/I2C application paths with
synthetic hardware replies: C and C-sharp major, filtered-key root selection,
H/P/T scale choices, Escape cancellation, output isolation and unchanged
mapping/threshold telemetry. These are modeled keypresses, not a physical
performance or timing measurement. Build/native commands and optional audits
are in [BUILDING.md](BUILDING.md); no test implicitly flashes a device.
