# Fn system menu, trigger point and brightness

Physical keys, original editor tables and LED timing in this guide describe
the `huntsman` build. Menu/text state machines live in the shared application;
the board supplies layout, editor policy and RGB placement. See
[architecture](ARCHITECTURE.md) and [porting](PORTING.md).

Hold Fn in keyboard mode: keyboard shortcuts below are **green**, settings
**C, Tab, Caps, K, L, R and \\** are white, and Enter shows the target mode color
(blue for MIDI, green for keyboard). Other keys are dark. In MIDI mode only
the Enter/Tab/K/L/R/\\/Left Shift/E/S/J/V settings are available; no keyboard shortcuts are advertised.
Unimplemented profile/media functions are not advertised.

Every settings option follows **hold to preview, release to execute once**.
Release either member to finish. K/L can be tapped repeatedly with Fn held;
other choices require all keys released before selecting again. RESET opens
a confirmation screen instead of clearing immediately.

| Chord | Preview | Action on release |
| --- | --- | --- |
| Fn+C | CALIBRATION | Start parallel calibration (keyboard mode) |
| Fn+Tab | TRIGGER | Enter trigger-point editor (keyboard mode) or the raw trigger page (MIDI mode) |
| Fn+Caps | RAPID | Enter compatibility rapid editor (keyboard mode) |
| Fn+Enter | MIDI / KEYBOARD | Switch to the displayed target mode |
| Fn+K | LIGHT- | Lower brightness one step |
| Fn+L | LIGHT+ | Raise brightness one step |
| Fn+\\ | RAINBOW / WHITE | Switch to the displayed lighting effect |
| Fn+R | RESET | Open RESET? confirmation (Y confirms, N cancels) |
| Fn+Left Shift | LOWER-OFF / LOWER-ON | Toggle Caps/Shift-row MIDI notes (MIDI mode only; ignored in Jankó mode) |
| Fn+J | JANKO | Toggle the built-in Jankó note layout (MIDI mode only) |
| Fn+V | VELOCITY | Transmitted-velocity start, ten steps: `1` = 0%, `0` = 100% (MIDI mode only) |
| Fn+E | KEY | Open root selection (MIDI mode only) |
| Fn+S | SCALE | Open scale selection (MIDI mode only) |

A shortcut key already held before Fn requires a fresh press. Simultaneous
new choices are rejected; once one preview is selected, additional keys cannot
replace it. Preview entry clears held output; a clean release executes once.
USB reset, invalid/stale scans, disable and calibration cancel the pending
action without executing it. GUI calibration entry remains immediate.

## Keyboard shortcuts

| Physical keys | Output while Fn is held |
| --- | --- |
| Esc | Backtick/grave |
| 1 2 3 4 5 6 7 8 9 0 - = | F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12 |
| Backspace | Delete |
| Y / P | Insert / Print Screen |
| N / M | End / Page Down |
| H / J | Home / Page Up |

Left Shift stays a normal modifier in the Fn layer, so **Fn+Shift+Esc is a
tilde** and Fn+Shift+1 is Shift+F1; a shortcut key held before Fn keeps its
base output until it is released. Right-side modifiers keep their navigation
remaps (see below), which intentionally replace the modifier function.

All twenty shortcut keys are green in the keyboard-mode Fn hint frame.
They send held NKRO usages, with no text preview or settings action on release.
Keep Fn held for repeated taps or simultaneous shortcuts. Releasing either
member releases its shortcut; releasing Fn first does not emit the base key.
A base key held before Fn keeps its original output until released.

In keyboard mode, Right Alt / Menu / Right Ctrl / Right Shift send
Left / Down / Right / Up, with no right-side modifier bits. Left-side modifiers
remain normal. MIDI controls and mappings are unchanged. The GUI captions
still identify physical keys. JIS has no Menu sensor.

The application resolves these overrides above the recovered action tables;
the tables, reference event entry point and modal trigger editor remain intact.
Other recovered Fn aliases retain their existing behavior but are not advertised.
Settings previews, confirmation and calibration take priority over keyboard
shortcuts and clear held output. Native tests exercise all three layouts;
compiled ARM tests check all twenty shortcuts, both release orders, repeated
taps, arrow reports and green LED channel output using modeled ASIC input.

## MIDI trigger point

In MIDI mode Fn+Tab opens a raw trigger page instead of the calibrated editor:
the number row is the same ten-step bar, but each step selects the **raw press
threshold for every key**. `1` is the deepest point, the bottom-out floor of the
velocity window (1500), and `0` is the shallowest, one count below the 3600
release threshold (3599); the steps between are spread across that range, so
the trigger point can be moved deeper for MIDI playing without starving the
velocity fit. Per-key release thresholds are never touched, and the pair stays
valid (`press < release`).
The selected step lights green with the steps below it lit; Escape leaves the
page. Because the page's own threshold decides what counts as "pressed", a deep
selection needs firm digit, chord and Escape presses. `menu status` and GUI
telemetry report the resulting thresholds.

## Transmitted-velocity start

Fn+V is a white MIDI-only settings hint that opens a modal page modelled on the
trigger-point editor: the number row becomes a ten-step bar, `1` is 0% and `0`
is 100%, and the selected step lights green while the steps below it stay lit.
Selecting a level applies immediately, so the bar can be auditioned without
leaving the page; Escape leaves it. While the page is open no key reaches
HID/MIDI, and leaving it requires all keys released before playing resumes.
The page reports its level through `menu status` (`velocity_start=1..10`).
Level 1 transmits the measured velocity unchanged, level 10 transmits every
note at full velocity, and the steps between raise the floor of the curve
(see [MIDI design](MIDI_DESIGN.md#transmitted-velocity-start)). The setting
survives mode switches and power cycles after automatic save; RESET clears it.
MIDI-mode Fn+Tab follows the same persistence rule
([device storage](DEVICE_CONFIG_STORAGE.md)).

## Jankó layout toggle

Fn+J is a white MIDI-only settings hint that turns green while the layout is
active. Its preview `JANKO` is followed by a release-only commit, then all keys
must be released before output resumes: the toggle aborts voices and pending
strikes. The layout replaces the playing notes of the letter, number and
punctuation rows with a staggered whole-tone arrangement; the configured
mapping is untouched and returns when the layout is switched off. The
root/scale filter still applies. The keys that play accidentals — the layout's
black keys — are tinted yellow: only the hue changes, so they keep the same
travel intensity as the diatonic keys and dim or brighten with them. A key the
root/scale filter disables stays dark like any other disabled note.
**Fn+Left Shift is ineffective in Jankó mode** — the lower rows always play —
and the J hint stays green as a reminder.

## MIDI lower-row toggle

Fn+Left Shift is a white MIDI-only settings hint. Its preview names the next action:
`LOWER-OFF` mutes notes on the physical Caps and Shift rows; `LOWER-ON`
restores them. Release either chord member to apply once, then release every
key before resuming output. A pre-held Left Shift requires a fresh press; invalid
scans, disable, calibration and configuration changes cancel the preview.
Keyboard-mode Left Shift/Fn+Left Shift behavior and keyboard shortcut hints are unchanged.

Filtering uses physical rows, independent of custom MIDI mappings. Muted
note keys go dark, except Enter's blue mode marker. Esc/Tab-row notes and
bottom-row controls/custom note mappings are unaffected. Left Shift remains discoverable
in the Fn menu while muted. Preview/toggle uses the ordinary MIDI cleanup path,
including pending strikes and shared-pitch owners, so held notes cannot stick.
No mapping or threshold is changed. Both groups start enabled; the mute
survives mode switches and power cycles after saving; confirmed RESET clears it.
It is not stored in host JSON. Telemetry continues reporting assigned mappings;
`menu status` exposes `lower_muted=0/1` for the current setting.

## MIDI root and scale selection

Fn+E/Fn+S preview `KEY`/`SCALE`, then open a modal selector on release. All
keys must be released once before a choice is accepted. Root selectors use
the fixed upper piano map (Tab/1/Q/2/W/E/4/R/5/T/6/Y and the corresponding
second octave) even when filtered, muted or remapped. Scale selectors are
J major, I natural minor, D Dorian, H Phrygian, Y Lydian, M Mixolydian,
L Locrian, P major pentatonic, O minor pentatonic, T chromatic/12T.

Available choices are PWM 77 white, the current value green at 255, and Escape
red. Holding a choice previews its full name through the text renderer;
sharps use `C-SHARP` etc., and 12T displays `CHROMATIC`. Release that key
above its release threshold to commit and exit. Escape cancels, not commits.
Multiple simultaneous choices require all-neutral before retrying. During a
selected preview, other selector keys cannot replace it; Escape can cancel.
The page consumes input until exit and uses ordinary all-neutral rearming.
Faults, disable, calibration and configuration revision changes cancel it.
The trigger editor's Escape-to-commit behavior is separate and unchanged.

Root and scale default to C/chromatic, survive mode switches and power cycles,
and reset with confirmed RESET or missing/corrupt storage. They filter assigned pitches,
intersecting with the physical lower-row gate and valid octave-transposed
note range. No mappings are rewritten. Enter/control indicators stay explicit
overlays. `menu status` reports root/scale IDs, names and the modal page.
See [table-driven musical selection](MIDI_SCALES.md) for the data and safety flow.

## Trigger-point editor

The original action dispatcher at `0x2000f41c` enters actuation mode on
Fn+Tab. The event handler at `0x200134fc` keeps the mode active after Fn
release, consumes ordinary key events, selects levels with 1–0, and commits
on Escape or another Fn+Tab. Existing layout-specific increment/decrement
controls also remain available. This application uses that state machine
inside the editor, with entry deferred until the menu chord is released.

Numbers 1–0 select levels 1–10 from the original table at `0x2001b486`.
They correspond approximately to 2.5%, 10%, 20%, …, 90% of the normalized
optical range, subject to original per-key minimums and fixed-threshold
exceptions. These are not calibrated millimeters or force units.

Number keys are dim white; the selected number is green. A brighter white
bar shows observed travel and moves one position per 20 ms toward its target.
Escape is red. The number-row colors match executed original renderer
`0x2000a078`; travel-bar exclusions/steps follow `0x2001a014` and
`0x2001505c`. Other keys are dark in this application's editor.
Calibration and MIDI-switch entry are unavailable until the editor exits.

Pending selection does not alter the normal raw pairs. A dirty actuation
commit converts the original normal-mode threshold rules for every key using
that key's current lower/upper calibration bounds. The conversion exactly
preserves the original strict normalized press/release comparisons:

```text
span = upper - lower
raw_press   = upper - ceil((normalized_press + 1) * span / 256) + 1
raw_release = upper - ceil(normalized_release * span / 256)
```

The original fixed-threshold keys retain their normalized exceptions.
Commit replaces custom GUI pairs, increments the raw configuration revision
once, cancels pending velocity captures and requires a neutral scan before
typing resumes. The GUI reads back the resulting raw pairs. Disabling output,
invalid scans or USB reset cancel an uncommitted edit. Committed levels and
all per-key pairs, including host edits, save after neutral and 250 ms without
further changes. RESET restores defaults.

The editor's last global level is not an inverse representation of arbitrary
per-key GUI pairs. GUI changes do not change that saved menu selection.
The separate Fn+Caps editor remains compatibility functionality; it does not
turn the raw Schmitt engine into a rapid-trigger implementation.

## Brightness and mode priority

K lowers and L raises brightness, using the original nonlinear 20-entry table
at `0x2001b49c`, from off to PWM 255. The table and directions match original
actions 9/8; this menu applies one step on release, with no auto-repeat while
held. Hold time controls only the text preview, not the amount of adjustment.
Keep Fn held and press/release K or L as many times as needed, including
alternating between them. Each shortcut release rearms only brightness; Fn
release ends the session. Menu edges preserve their own Schmitt history so
keys held in the hysteresis band or across previews cannot generate false taps.
Host keyboard/MIDI output stays disarmed until an all-keys-neutral scan, and
other menu actions cannot use the brightness exception. Fn hints remain visible
between taps. Faults, USB reset, disable, calibration or GUI configuration
revision changes cancel the session.

Normal LEDs, including idle MIDI markers, use the selected brightness. Fn-menu
hints use at least PWM 25 so the raise-brightness control is discoverable from
off. Editor and calibration feedback retain full intensity. These visibility
rules do not override explicit `light off` or stale/fault blanking.
Fn suppresses new MIDI notes, including K/L notes; existing notes continue
their normal release/cleanup path.

## Interruptible text display

Holding a shortcut shows its name without executing it. `MIDI` is blue and
`KEYBOARD` green, matching Enter's target-mode hint. Other names are white.
Every character appearing in the name lights at 30% (PWM 77/255); each
character in sequence becomes 100% (PWM 255) for
200 ms. For `MIDI`, the highlights are M, I, D, I, including the repeated I.
The first character highlights immediately. After the last character, all word
letters remain at 30% for 500 ms before the sequence repeats. Other keys are off.
The word retains these absolute PWM levels regardless of brightness selection.

Releasing **either** Fn or the shortcut key ends the preview and executes the
action at the first scan above that key's release threshold, including during
a character or repeat pause. K/L may restart with a fresh tap while Fn remains
held; other previews and host output still require all-keys-neutral rearming.
Stop/reset, invalid scans and
calibration entry also cancel. A physical LED change follows the existing
40 ms snapshot/upload scheduler; the animation never blocks USB, scanning,
MIDI cleanup or LED transfers. Explicit lighting-off and fault/stale blanking
remain authoritative.

`keyboard_text_start(state, profile, string, now_ms)` in
[keyboard_text.c](../firmware/app/src/keyboard_text.c) resolves ASCII letters to physical
sensors using the base layout, independent of MIDI mappings. It copies up to
32 supported characters (letters and `-`/`+`/`?`), accepts either case, ignores other characters and keeps
no borrowed string pointer. Minus uses the minus key; plus uses the `=`/`+`
key (base HID usage 0x2e); question mark uses the slash/question-mark key
(base HID usage 0x38). Empty/unsupported input stops the display.
`keyboard_text_color(state, r, g, b)` sets the highlight color, with the
background scaled to 30%; start defaults to white.
`keyboard_text_render(state, frame, now_ms)` renders only the current phase,
with no FIFO or catch-up work; `keyboard_text_stop(state)` cancels immediately.
An inactive render leaves its input frame untouched. All three layouts use
their existing LED channel maps. The common menu controller owns the animation state,
separately from the disarmed keyboard engine, and tracks held chord members
against their raw release thresholds after preview entry clears key-down state.
The text preview takes priority over ordinary Fn hints.

## RESET and flash boundaries

Releasing Fn+R opens an animated `RESET?` confirmation. Y stays solid green
and N solid red at PWM 255, independent of brightness. Release all keys once,
then press Y to confirm or N to cancel. A pre-held Y cannot confirm; pressing
both together cancels. Confirmation consumes all keyboard/MIDI input and has
no automatic timeout or erase. Invalid/stale scans, USB reset, disable,
calibration entry and GUI configuration changes cancel it without erasing.
After cancellation or confirmation, release all keys to resume normal output.

Only confirmed Y clears settings/calibration at `0x78000` and `0x78200`.
Both owned pages are erased and CMD5 blank-verified, older first. Corrupt
contents can be cleared; controller errors stop and report failure. Two-page
clearing is not atomic: interruption can retain the newest record or leave
defaults, but older-first erasure prevents stale-record resurrection.

After successful clearing, release all keys. The application restores default
thresholds, mappings, trigger and velocity levels, mode/octave and brightness,
and rebuilds optical defaults on neutral input without rebooting the USB device.
The host equivalent is explicit `cfg clean`; flashing preserves compatible
settings by default ([storage](DEVICE_CONFIG_STORAGE.md)). The next GUI snapshots show
no saved calibration (generation 0). Factory settings, serial-number storage,
bootloader, application image and optical-ASIC firmware are never erase targets.
Reset deletion is not undoable on-device; recalibrate or use a private backup.

## Diagnostics and tests

Offline protocol audits issue a SysEx `menu status` command
to read Fn state, editor mode, current/saved actuation level and brightness
index/PWM, `reset_confirm` and confirmation `ready` flags. The lower-row mute,
Jankó flag, root/scale, music page and velocity start appear as `lower_muted`,
`janko`, `root`, `scale`, `music_page` and `velocity_start`.
The GUI is the only supported PC application; no serial command client is needed. GUI telemetry framing is
defined by the Huntsman port; use the matching GUI from this checkout.

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset huntsman
cmake --build --preset huntsman
# Optional audit dependencies and separate read-only original reference:
cmake --build --preset huntsman --target audit-menu audit-keyboard
```

Native tests check all raw ADC values against normalized comparisons across
all three layouts/ten levels, simultaneous Fn chords, commit/cancel/neutral
behavior, hint masks, repeated K/L taps with Fn held, hysteresis and brightness
bounds/release/timer rollover. Confirmation tests cover pre-held Y, N cancel,
simultaneous Y/N, full-brightness colors, fault cancellation and output isolation. Compiled
ARM tests check actual I2C menu pixels, original number-row colors, calibrated
HID behavior, MIDI suppression, Fn+C entry, committed-setting persistence and
no writes from cancelled previews. RESET tests verify bounded erase, CMD5
blank verification and default recovery.
Native text tests cover every millisecond of both words across ANSI/ISO/JIS,
duplicate letters, timer rollover, copied input, cancellation and hysteresis.
Compiled tests inspect actual I2C word pixels, repeats, brightness independence
and cancellation by either chord member before its current word finishes.
See [current validation limits](VALIDATION.md).
