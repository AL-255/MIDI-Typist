# MIDI-Typist keyboard user manual

For the **Huntsman V3 Pro Mini**, using the complete **`huntsman` firmware**
(also available as `keyboard-fn-menu`).

This manual covers the keyboard as it works now: typing, MIDI performance,
lighting, calibration, the configuration GUI, and troubleshooting. All
illustrations are embedded text; no images, downloads, or special Markdown
extensions are needed. The layout drawings show the **61-key ANSI model**.
Key names refer to the physical keycaps, even when their output is different.
This is the Huntsman operating guide, not a layout promise for other hardware.
Developers adding a keyboard or MCU should use the [platform porting guide](docs/PORTING.md).

## Contents

- [1. Start here](#1-start-here)
- [2. Find your way around](#2-find-your-way-around)
- [3. Read the lights](#3-read-the-lights)
- [4. Type and use Fn shortcuts](#4-type-and-use-fn-shortcuts)
- [5. Use the settings menu](#5-use-the-settings-menu)
- [6. Adjust the trigger point](#6-adjust-the-trigger-point)
- [7. Calibrate the keys](#7-calibrate-the-keys)
- [8. Play MIDI](#8-play-midi)
- [9. Use the configuration GUI](#9-use-the-configuration-gui)
- [10. Know what gets saved](#10-know-what-gets-saved)
- [11. Reset custom settings](#11-reset-custom-settings)
- [12. Inspect sensor readings](#12-inspect-sensor-readings)
- [13. Troubleshooting](#13-troubleshooting)
- [14. Firmware maintenance](#14-firmware-maintenance)
- [Quick reference](#quick-reference)

## 1. Start here

1. Connect the keyboard to USB with all keys released.
2. It starts in **keyboard mode**. Enter is green during normal operation.
   You can type without opening the GUI or a serial monitor.
3. For arrows, use **Right Alt = Left**, **Menu = Down**,
   **Right Ctrl = Right**, and **Right Shift = Up**.
4. To play music, hold **Fn+Enter**. The keyboard spells `MIDI` in blue.
   Release the combo, then release all keys. Enter is now blue.
5. Select the keyboard's MIDI input in your music application and route it
   to an instrument listening on **MIDI channel 1**. The keyboard sends
   musical control data; it does not generate audio itself.
6. Hold Fn+Enter again to preview green `KEYBOARD`, then release to return
   to typing.

> After a mode switch, settings change, or calibration, release **all** keys
> before trying to type or play. This prevents held keys from becoming
> accidental keystrokes or notes when input resumes.

## 2. Find your way around

### Physical keycaps

```text
[Esc ][ 1  ][ 2  ][ 3  ][ 4  ][ 5  ][ 6  ][ 7  ][ 8  ][ 9  ][ 0  ][ -  ][ =  ][Backspace ]
[  Tab  ][ Q  ][ W  ][ E  ][ R  ][ T  ][ Y  ][ U  ][ I  ][ O  ][ P  ][ [  ][ ]  ][   \   ]
[  Caps   ][ A  ][ S  ][ D  ][ F  ][ G  ][ H  ][ J  ][ K  ][ L  ][ ;  ][ '  ][   Enter   ]
[   LShift   ][ Z  ][ X  ][ C  ][ V  ][ B  ][ N  ][ M  ][ ,  ][ .  ][ /  ][    RShift    ]
[LCtrl ][LWin ][ LAlt ][               Space               ][  Fn  ][RAlt ][ Menu ][RCtrl]
```

This is a position guide, not a scale drawing. `[` and `]` are the two
bracket keycaps; `\` is backslash. **Fn is immediately to the right of
Space**, followed by Right Alt. The key marked Menu may have a menu icon.

### The right-hand cluster changes with the mode

```text
                   KEYBOARD MODE             MIDI MODE
  Right Shift           Up arrow             Unmapped by default

  [Fn][RAlt][Menu][RCtrl]                  [Fn][RAlt][Menu][RCtrl]
       Left  Down Right                       Oct-  Off  Oct+
```

Right Alt, Right Ctrl, and Right Shift **do not act as modifiers in keyboard
mode**. Left Ctrl, Left Alt, Left Windows, and Left Shift still have their
ordinary typing functions. The GUI keeps physical key names so that selecting
a key always selects the same sensor.

## 3. Read the lights

The meaning of a color depends on what you are doing:

| Situation | What you see | Meaning |
| --- | --- | --- |
| Normal keyboard mode | Green Enter; other resting keys white | Ready for typing |
| Normal MIDI mode | Blue Enter and six blue controls; mapped notes white | Ready for music; unmapped note keys are dark |
| Pressing an ordinary illuminated key | The key becomes dimmer | Greater optical travel; brightness is inverted |
| Holding Fn in keyboard mode | Green shortcut keys | These send keyboard functions while held |
| Holding Fn | White settings keys; Enter in the target mode's color | Hold a setting combo to preview it; release to act |
| Holding a settings combo | A word traced across the keycaps | The pending action's name, not typed text |
| MIDI octave shifted | Right Alt or Right Ctrl flashes blue | Negative or positive octave shift; faster means more octaves |
| Calibration | Purple → blue → amber → green | Release → waiting → holding → registered |
| Reset confirmation | Animated `RESET?`, solid green Y and red N | Confirm with Y or cancel with N |

### How to read a word on the keyboard

For `MIDI`, the M, I, and D keycaps form the background at 30% intensity.
One letter at a time rises to 100%, including repeated letters:

```text
Time:       0.0 s       0.2 s       0.4 s       0.6 s       0.8–1.3 s
Highlight:    M           I           D           I           pause
Background: M I D       M I D       M I D       M I D         M I D
                                                            then repeat
```

Read the highlighted letters in time order, not left-to-right on the board.
`MIDI` is blue, `KEYBOARD` is green, and other action names are white.
`+` uses the `=`/`+` key; `?` uses the `/`/`?` key. Releasing either combo
member stops the preview and triggers the action; you do not need to wait for
the word to finish. The visible change follows the next LED update.

Global brightness scales normal key lighting and mode indicators. Fn hints
remain visible at a minimum brightness even when normal brightness is off;
text previews and calibration/editor feedback use their own intensity.
An explicit diagnostic `light off` command or invalid scan can still blank
the lights.

## 4. Type and use Fn shortcuts

Multiple keys may be held at once (NKRO). The operating system controls
ordinary key-repeat behavior.

### The top row with Fn held

Read vertically: the upper line is the physical key; the lower line is its
Fn output. All fourteen keys below are hinted green.

```text
Key:  Esc   1    2    3    4    5    6    7    8    9    0    -    =   Bksp
Fn:    `   F1   F2   F3   F4   F5   F6   F7   F8   F9  F10  F11  F12   Del
```

### Navigation shortcuts

| Hold Fn and press | Sends |
| --- | --- |
| Y | Insert |
| P | Print Screen |
| H | Home |
| J | Page Up |
| N | End |
| M | Page Down |

These six keys are also green while Fn is held. Their positions are:

```text
         ... [T][Y:Insert][U][I][O][P:Print Screen] ...
         ... [G][H:Home][J:Page Up][K][L] ...
         ... [B][N:End ][M:Page Down][,][.] ...
```

Keep Fn held and tap several shortcuts in succession. They act like normal
held keys, with **no word preview and no release-to-execute delay**. Releasing
either member releases the shortcut. Releasing Fn first does not type the
underlying letter. A letter already held before Fn retains its original
output until released; release and press it again to use its Fn action.
Your operating system and active application determine what Insert, Print
Screen, and the other delivered keycodes do.

These shortcuts apply to keyboard mode, not MIDI mode. The GUI edits
thresholds and MIDI notes, not these keyboard keycode assignments.

## 5. Use the settings menu

Settings are different from the green typing shortcuts:

```text
Hold Fn → press a settings key → read the preview → release either key
                                                        |
                                                   action happens
                                                        |
                                             release all keys to resume
```

| Combo | Word shown | Result on release | Available in |
| --- | --- | --- | --- |
| Fn+Enter | MIDI or KEYBOARD | Switch to the named mode | Both modes |
| Fn+C | CALIBRATION | Start calibration | Keyboard |
| Fn+Tab | TRIGGER | Open the trigger-point editor | Keyboard |
| Fn+Caps | RAPID | Open the compatibility editor; see below | Keyboard |
| Fn+K | LIGHT- | Lower brightness one step | Both modes |
| Fn+L | LIGHT+ | Raise brightness one step | Both modes |
| Fn+Left Shift | LOWER-OFF or LOWER-ON | Mute or restore the Caps/Shift rows' notes | MIDI |
| Fn+E | KEY | Open root-note selection | MIDI |
| Fn+S | SCALE | Open scale selection | MIDI |
| Fn+R | RESET | Open confirmation; does not erase yet | Both modes |

Press Fn before the settings key, or press both together. A settings key
already held before Fn needs a fresh press. Choose one setting at a time;
simultaneous settings choices are rejected. A preview pauses normal output.
In MIDI mode, holding Fn prevents new notes, releases sustain and centers the wheel controls;
entering a settings preview also clears sounding/pending notes.

### Brightness: keep Fn held and tap

There are 20 brightness levels, including off. Each K/L release changes one
step; holding the combo longer only repeats its word, not the adjustment.

```text
Fn:  [---------------------- held -----------------------]
L:       press/release    press/release    press/release
Light:       +1 step          +1 step          +1 step
```

You can alternate K and L in the same Fn hold. This repeat exception applies
only to brightness. Release all keys before choosing another setting or
resuming typing/music. Brightness is stored and returns with the next power cycle.

## 6. Adjust the trigger point

A raw sensor reading **decreases as you press farther**. It is carried in
a 16-bit field, but valid readings are 1–4096, not a 0–65535 travel scale.
Typical rest/fully pressed readings are around 4000/1000; each key can differ.

### Understand press and release thresholds

Defaults are **press 3500 / release 3600**:

```text
Press farther → raw falls
Rest ~4000 ────── 3600 ────── 3500 ────── fully pressed ~1000
                  |            |
Release above here             Press below here
                  +------------+
                  keep previous state

3499: becomes down       3500–3600: stays as it was       3601: becomes up
```

The gap prevents small fluctuations from rapidly pressing and releasing a
key. Equality does not change state. A **higher press number triggers
earlier**, and a lower one requires deeper travel. Always keep
`1 <= press < release <= 4095`, with release safely below the key's idle
reading. A release value above idle can prevent the entire keyboard from
arming after a settings change.

### Quick adjustment without the GUI

1. In keyboard mode, hold **Fn+Tab** to preview `TRIGGER`.
2. Release the combo and all other keys. The editor stays open after Fn
   is released; ordinary typing is paused.
3. Tap **1–9 or 0** to choose level 1–10. A lower level triggers at shallower
   travel; a higher level requires deeper travel. The range is approximately
   2.5% at level 1, then 10%, 20%, …, 90% at level 10, with per-key exceptions.
4. The selected number is green; a white number-row bar shows observed
   travel. Escape is red. Other keys are dark.
5. Press **Escape** to apply and exit. Fn+Tab also applies and exits.
6. Release all keys to resume typing. Check the resulting raw pairs in the
   GUI if you want exact values.

> **Escape saves the pending editor selection; it is not Cancel.** A committed
> global trigger adjustment replaces individual GUI threshold pairs. To
> discard a pending edit without applying it, use **Disable keyboard** in
> the GUI, then release all keys and re-enable.

The editor uses each key's calibration bounds and retains fixed-threshold
exceptions for some keys, so it does not assign one identical raw pair to
every sensor. These levels are not millimeters or calibrated force. For an
exact common pair, use **Apply thresholds to all keys** in the GUI instead.
The chosen level is stored, so it returns with the next power cycle; per-key edits made from a host tool are temporary until restart, and a host profile can preserve the raw pairs.

**Fn+Caps / RAPID:** this is a compatibility settings screen, not an enabled
dynamic rapid-trigger feature. The active keyboard still uses the two raw
Schmitt thresholds. Numbers select a compatibility level, Caps alone toggles
its compatibility flag, and Escape exits. Use Fn+Tab or the GUI for actual
typing sensitivity. Exit any editor before calibrating or switching mode.

## 7. Calibrate the keys

Calibration teaches the device each key's resting and fully pressed optical
values. It improves travel lighting and MIDI aftertouch. It **does not
automatically change** raw trigger thresholds, MIDI note mappings, wheel
endpoints, or velocity scaling.

### Run a calibration

1. Enter keyboard mode. Hold **Fn+C**, read `CALIBRATION`, and release.
   Alternatively, connect the GUI and click **Calibrate keys → device flash**.
2. **Release every key.** During the purple phase, leave the keyboard untouched
   for 0.5 seconds so it can capture the resting values.
3. When the keys turn blue, fully press and hold one or more keys. Each
   active key turns amber. Hold steadily for one second until it turns green.
4. Continue with the remaining blue keys. You may press several together,
   start each at a different time, and leave green keys held. Include Fn,
   modifiers, Space, Enter, and all other keys—not just MIDI notes.
5. When all 61 keys are registered, the device saves the complete result.
   Release all keys to resume operation. Green briefly confirms completion;
   the GUI shows completion and a saved calibration generation.

```text
All keys:   PURPLE  ── untouched for 0.5 s ──>  BLUE

Key A:      BLUE ── hold fully ── AMBER ── 1 s stable ── GREEN
Key S:      BLUE ─────── hold fully ── AMBER ── 1 s stable ── GREEN
Key D:      BLUE ── hold ── release ── BLUE ── hold again ...
            Each key has its own timer; D does not reset A or S.

Every key registered ──> save complete calibration ──> release all ──> ready
```

Fully bottom out the keys; a shallow touch is not a full-travel calibration.
A candidate must fall to at most half its own rest reading. Motion over
64 counts restarts that key's stable-hold timer; releasing a pending key
also restarts only its timer. Completed keys do not need to be repeated.

**Do not pause for five seconds without progress.** Inactivity aborts and
discards the attempt. The GUI's **Cancel calibration** button also discards
it. Red feedback indicates cancellation, timeout, or failure. The previous
active calibration remains unchanged unless the entire new run saves
successfully. Do not disconnect USB while saving.

A successful calibration survives restart. Only the two reserved custom
calibration pages are used; the factory serial-number area is not overwritten.
A start acknowledgment in the GUI means the routine started, not that it
has already saved. Wait for completion and the saved-generation confirmation.

## 8. Play MIDI

### Connect to an instrument

1. Hold Fn+Enter, preview `MIDI`, release, then release all keys.
2. In your music application, enable the keyboard's USB-MIDI input, shown as
   **Huntsman V3 Pro Mini MIDI** or a related port name.
3. Route channel 1 to a software instrument; enable its monitoring/record-arm
   function as required by that application.
4. Play the white-lit note keys. A fast press affects attack velocity;
   continuing to press farther changes polyphonic aftertouch.

Typing is suppressed in MIDI mode. Notes send Note On/Off on channel 1, with
attack velocity 1–127 and polyphonic key pressure 0–127. Release velocity is
fixed at zero. Your instrument must support **polyphonic aftertouch** to
respond independently to each note's travel; channel aftertouch is different.
Aftertouch is an optical-travel proxy, not a calibrated force measurement.

### Default notes: two overlapping playing ranges

Read each key above its note. The groups show increasing pitch, not exact
physical key spacing. `#` means sharp; `Bksp` is Backspace, `LShift` is Left Shift.

```text
UPPER RANGE
Key:   Tab   1    Q    2    W    E    4    R    5    T    6    Y
Note:   C5  C#5   D5  D#5   E5   F5  F#5   G5  G#5   A5  A#5   B5

Key:    U    8    I    9    O    P    -    [    =    ]   Bksp   \
Note:   C6  C#6   D6  D#6   E6   F6  F#6   G6  G#6   A6  A#6   B6

LOWER RANGE
Key:  LShift A    Z    S    X    C    F    V    G    B    H    N
Note:   C4  C#4   D4  D#4   E4   F4  F#4   G4  G#4   A4  A#4   B4

Key:    M    K    ,    L    .    /    '
Note:   C5  C#5   D5  D#5   E5   F5  F#5
```

There are 43 default note keys. Other note-capable keys are unmapped and
dark until assigned in the GUI. Enter remains a blue mode indicator even
when unmapped. Left Shift is C4 only in MIDI mode; Right Shift is unmapped
by default but can be assigned a note.

This project calls MIDI note **60 = C4**, **72 = C5**, **84 = C6**, with each
semitone adding one. Some music software labels octaves differently; compare
the MIDI number rather than assuming its displayed C4 is the same pitch.
GUI mappings accept note numbers 0–127, note names, or Off.

The two ranges overlap. If two held keys map to the same MIDI pitch, they
share one sounding note: the first press starts it, the last release stops
it, and aftertouch follows the greater travel. The second key does not
retrigger that pitch or replace its attack velocity. Different pitches remain
independent; this is channel-1 polyphonic MIDI, not MPE.

### Move the trigger point in MIDI mode

In MIDI mode the press threshold is normally left at its 3500 default. Hold
**Fn+Tab** to preview `TRIGGER`, release either key, and the number row becomes
a ten-step bar: press `1` for the deepest point, which sits at the bottom-out
floor of the velocity window (1500), or `0` for the shallowest, one count below
the 3600 release threshold; the steps between are spread across that range. Every key gets the same point, release
thresholds are untouched, and pressing **Esc** leaves the page. The selected
step is green with the steps below it lit. Because the point itself decides
what counts as a press, deep selections need firm presses on the digits, the
chord and Escape. Velocity keeps working at every step.

### Set the transmitted-velocity start

In MIDI mode, hold **Fn+V** to preview `VELOCITY`, then release either key to
open the ten-step editor. The number row becomes a bar; press a digit to pick
the starting point of the velocity curve and press **Esc** to leave:

```text
1    2    3    4    5    6    7    8    9    0
0%  11%  22%  33%  44%  56%  67%  78%  89% 100%
```

`1` transmits the measured velocity unchanged, so soft presses stay soft.
`0` transmits every note at full velocity. The steps between raise the floor of
the curve: a soft press is lifted to the floor while harder presses still reach
full velocity, which is useful when a host instrument ignores low velocities.
The selected step is green and the steps below it stay lit; the page consumes
all key input, so release every key after pressing **Esc** before playing
again. The setting is global, applies in both playing layouts, and is reported
by `menu status` as `velocity_start=1..10`; it lives in RAM, so a power cycle
returns it to level 1.

### Play the built-in Jankó layout

In MIDI mode, hold **Fn+J** to preview `JANKO`, then release either key to
switch the playing notes to the built-in staggered whole-tone layout. Release
all keys before playing again. The J hint turns green while it is active, and
`menu status` reports `janko=1`. The black keys of the
layout - every key that plays a sharp - glow yellow, so the staggered rows can
be read at a glance. Only the colour changes: a black key dims and brightens
with pressure exactly like a white one, and the white keys keep their normal
backlighting.

```text
Esc  1   2   3   4   5   6   7   8   9   0   -   =   Bksp
A#3  C4  D4  E4  F#4 G#4 A#4 C5  D5  E5  F#5 G#5 A#5 C6

Tab  Q   W   E   R   T   Y   U   I   O   P   [   ]   \
B3   C#4 D#4 F4  G4  A4  B4  C#5 D#5 F5  G5  A5  B5  C#6

Cap  A   S   D   F   G   H   J   K   L   ;   '   Ent
C4   D4  E4  F#4 G#4 A#4 C5  D5  E5  F#5 G#5 A#5 C6

LSh  Z   X   C   V   B   N   M   ,   .   /   RSh
C#4  D#4 F4  G4  A4  B4  C#5 D#5 F5  G5  A5  B5
```

Holding Fn+J again previews `JANKO` and switches back; the notes you
configured are restored exactly, because the layout never edits your mapping.
The bottom-row octave/bend/modulation/sustain controls keep their normal
behavior, and the root/scale filter still applies to the
layout's notes. **Fn+Left Shift is ineffective while the layout is active:**
the lower rows always play. The GUI shows `JANKÓ layout (Fn+J)` in its status
line and labels the keys with the layout's notes until you switch back.

### Use only the upper playing range

In MIDI mode, hold **Fn+Left Shift** to preview `LOWER-OFF`, then release either key.
Release all keys to resume playing. The two lower letter rows become silent
and their note lights go dark:

```text
Esc  1  2  3 ... Backspace       kept
Tab  Q  W  E ... backslash       kept
Caps A  S  D ... Enter           notes muted
LShift Z X C ... RShift          notes muted
LCtrl LWin LAlt Space Fn ...     bottom row unchanged
```

Hold Fn+Left Shift again to preview `LOWER-ON`; release to restore those rows. Left Shift is
hinted white in the MIDI Fn menu, even when its note is muted. This setting
does not change keyboard mode or erase mappings. Any custom note assigned
to a key in the Caps/Shift rows is also muted—including Enter, although its
blue mode indicator stays lit. The octave/bend/modulation controls remain
available, and custom bottom-row note assignments are unchanged.

As with other settings previews, sounding notes are cleared and pending
strikes cancelled before resuming from an all-keys-released state. The choice
survives switching between keyboard and MIDI modes and a power cycle, and RESET
restores both groups. It is not stored in host profiles.
The GUI still shows the saved note assignments; muted keys are not remapped
to Off. For text diagnostics, `menu status` reports `lower_muted=1` or `0`.

### Choose a root and scale

This feature **filters notes; it does not move or retune your mappings**.
For example, C major leaves C, D, E, F, G, A, and B playable in every octave.
The other mapped notes are silent and dark. A mapped key must also be in an
enabled row and transpose to MIDI 0–127 to play. Blue controls and Enter's
mode marker remain visible even when a note is filtered out.

1. In MIDI mode, hold **Fn+E** to preview `KEY`, then release the combo.
2. Release all keys. The upper piano keys become root selectors. Available
   selectors are dim white, the current root is green, and Escape is red.
3. Hold the desired root key. The keyboard spells its name, such as `D` or
   `C-SHARP`. Release it to apply, leave the menu, and release all keys to play.
4. Hold **Fn+S** to preview `SCALE`, then release and let all keys up.
5. Hold one of the scale selectors below to preview the full scale name.
   Release to apply and exit. **Escape cancels**, including during a choice
   preview. Do not press two choices at once; if you do, release all and retry.

The root selectors are the fixed upper piano layout, not your current GUI
assignments. Either octave selects the same root pitch class. Filtered or
remapped keys remain usable as selectors; octave shifting does not change them.

```text
Key:   Tab/ U   1/8   Q/I   2/9   W/O   E/P   4/-   R/[   5/=   T/]   6/Bksp  Y/\
Root:     C     C#     D     D#     E     F     F#     G     G#     A      A#     B
```

| Selector | Scale | Semitone intervals above the root |
| --- | --- | --- |
| J | maJor | 0, 2, 4, 5, 7, 9, 11 |
| I | mInor (natural minor) | 0, 2, 3, 5, 7, 8, 10 |
| D | Dorian | 0, 2, 3, 5, 7, 9, 10 |
| H | pHrygian | 0, 1, 3, 5, 7, 8, 10 |
| Y | lYdian | 0, 2, 4, 6, 7, 9, 11 |
| M | Mixolydian | 0, 2, 4, 5, 7, 9, 10 |
| L | Locrian | 0, 1, 3, 5, 6, 8, 10 |
| P | major Pentatonic | 0, 2, 4, 7, 9 |
| O | minOr pentatonic | 0, 3, 5, 7, 10 |
| T | 12T / chromatic | All twelve semitones; no scale restriction |

Root and scale can be selected in either order. The default is **C + 12T**,
so all otherwise enabled mapped notes are initially available. Selecting T
previews `CHROMATIC` and removes the scale restriction without changing mappings
or lower-row mute. Root/scale survive mode switches and a power cycle, but confirmed
RESET restores C/chromatic. They are not included in JSON profiles or saved
calibration. The GUI still shows assigned notes, not their filtered status;
`menu status` reports the active root and scale names over text CDC.

Entering either menu clears sounding/pending notes. Menu key presses never
play notes. Changes take effect only when a choice is released; USB/scan faults,
output disable, or configuration changes cancel an unfinished choice.

### Octave, pitch bend, modulation, and sustain

```text
LEFT OF SPACE                            RIGHT OF SPACE
[ LCtrl ][ LWin ][ LAlt ] [ Space ... ] [ Fn ][ RAlt ][ Menu ][ RCtrl ]
  Bend-    Mod    Bend+     Sustain            Oct-              Oct+
```

| Control | How to use it | Feedback |
| --- | --- | --- |
| Right Alt | Tap to lower notes by one octave | Flashes blue for negative shift |
| Right Ctrl | Tap to raise notes by one octave | Flashes blue for positive shift |
| Left Ctrl | Press farther to bend pitch down | Steady blue |
| Left Alt | Press farther to bend pitch up | Steady blue |
| Left Windows | Press farther for modulation, CC1 | Steady blue |
| Space | Hold for sustain, CC64; release to lift the pedal | Steady blue |

Octave shifting acts once per press, within −10…+10. Faster flashing means a
larger shift: a full cycle is 1.2 s at ±1 and 0.12 s at ±10. At zero both
octave controls are steady blue. The GUI displays the exact offset. Tap the
opposite octave key until the offset is zero; switching modes alone does not
reset it. Held notes retain their original pitch. Transposed notes outside
0–127 are silent rather than wrapping to another pitch.

Wheels use fixed readings: **3800 or higher = 0%**, **1000 or lower = 100%**,
linear between them. They can respond before the key's trigger threshold.
Equal Left Ctrl/Left Alt depth cancels pitch bend to center. Modulation is
0–127; pitch bend is 14-bit, centered at 8192. Your instrument determines
the bend range in semitones and what CC1 controls. Calibration and threshold
changes do not change wheel endpoints.

Space sends **CC64 = 127** when its reading falls below its press threshold,
and **CC64 = 0** when it rises above its release threshold. Defaults are
**3500 / 3600**; equality holds the current state. This is an on/off pedal,
not half-pedaling, and its thresholds can be edited in the GUI. Choose an
instrument that supports sustain on MIDI channel 1. Space remains available
with every root/scale and with the lower rows muted; it is not assignable to
a note. Its blue light matches Enter and follows global brightness.

Fn releases sustain; menus, mode changes and fault cleanup also send pedal-off.
Release Space and press again to resume afterward. Sustain changes are ordered
with note events, including when USB is temporarily busy. In keyboard mode,
Space continues to type a normal space.

### What the velocity number means

Each key independently collects a velocity window from the first sample
**below** its press threshold. The window holds up to ten readbacks and closes
early when the key crosses the shared bottom-out threshold of 1500 (that
sample is excluded), so very fast presses fit on only a few samples. The
firmware divides the total drop by the number of intervals; windows longer
than five samples discard the interval furthest from their median first.
It scales the result to a float from **0 to 1**, with 4,500,000 counts/s as
the maximum. A fresh strike is armed after the key exceeds its release
threshold. The GUI displays the last completed strike's value, not current
pressure; a held key need not show a continuously changing velocity.

The calculation assumes 8000 scans/s. Actual 8 kHz hardware acquisition has
not been established, so this is not a calibrated speed in distance/time.
Very short taps still produce ordered Note On/Off once the window closes,
but the resulting sound may be very short or inaudible. Keep press thresholds
above 1500: below the bottom-out threshold no window can collect a fit.

## 9. Use the configuration GUI

The provided GUI runs on Linux with Python 3.10+ and Tk. It uses the Python
standard library; no pip packages are needed. Run commands from the repository
root. Install your distribution's Python Tk package if it is missing.

```sh
python3 tools/keyboard_gui.py
```

This auto-detects the keyboard's CDC port by its USB identity (`1532:02b0`).
Pass an explicit node with `--device` (for example
`python3 tools/keyboard_gui.py --device /dev/ttyACM1`) if several matching
boards are connected or detection finds nothing. **Detect** reruns the scan,
and **Connect** also runs it when the device field is empty or `auto`.

Use the actual device path if it differs. Your account needs serial-port
read/write permission; do not run the configuration GUI as root. Close other
serial readers before clicking **Connect**. For a no-device demonstration:

```sh
python3 tools/keyboard_gui.py --demo
```

### Read the screen

```text
+------------------------------------------------------------------+
| Device / Connect | Enable keyboard | Disable keyboard | Profiles  |
| Connection, mode, octave, scan health and configuration status     |
+------------------------------------------------------------------+
|                  Click a key on the keyboard drawing              |
|                  key name / MIDI mapping                         |
|                  raw reading / last velocity                      |
+------------------------------------------------------------------+
| Calibrate keys -> device flash | Cancel calibration | Progress    |
+-------------------------------+----------------------------------+
| Selected key and sensor ID    |                                  |
| Device-confirmed thresholds  |       Recent raw-value plot        |
| Press / release inputs       |                                  |
| Apply selected / apply all   |                                  |
| MIDI note / apply mapping    |                                  |
+-------------------------------+----------------------------------+
```

This is a simplified guide, not a screenshot. **Orange** means the sensor
is down; **cyan outline** means selected. Calibration temporarily uses its
purple/blue/amber/green colors. Raw numbers are live readings, while velocity
is the last completed fit. The GUI refreshes about 30 times/s; this does
not set the keyboard's scan rate. Its submitted NKRO report is a diagnostic
view, not proof that an application received a keystroke.

Checking **Hold first 20 pts of keystroke** above the plot changes it from
the scrolling waveform to a held per-keystroke capture for velocity-curve
tuning: the GUI switches the device to a full-rate per-key stream (every
optical scan frame — the fastest rate the keyboard produces, about
1.35 k samples/s on this hardware; the keyboard drawing pauses while it is
active) and holds the first 20 samples after the selected key's trigger, with
the trigger sample marked in orange. The firmware's velocity fit is
reproduced from the same bottom-out window the device uses and shown as
counts/s and 0–1. A new press replaces the held capture; changing the
selected key or unchecking the mode clears it and restores live telemetry.

The GUI supports editing the ANSI/61-key layout. Firmware also handles
ISO/62 and JIS/65, but the GUI rejects those layouts rather than placing
their sensors under incorrect keycaps.

### Change one key or all keys

1. Click **Disable keyboard** to prevent typing or MIDI while tuning. Despite
   its label, this disables both output modes; readings and velocity continue.
2. Click the desired key on the drawing.
3. Enter the press/release pair, for example **3500 / 3600**.
4. Click **Apply to selected key**, or **Apply thresholds to all keys** to
   assign that same pair to every key in one device update.
5. Wait for acknowledgment and matching readback. Editing a text field alone
   does not change the keyboard.
6. Release all keys and click **Enable keyboard** when ready.

Stale/disconnected telemetry and active calibration/editor states restrict
ordinary edits. Configuration changes release existing output and require
neutral input again. Disabling/re-enabling does not switch performance mode.

### Assign a MIDI note

Select the physical key, enter a note such as `C5`, `F#5`, `Eb4`, a number
such as `72`, or `Off`, then click **Apply MIDI mapping** and wait for readback.
Mappings can be edited in either performance mode. Fn, Left Ctrl/Windows/Alt,
Right Alt/Ctrl, and Space are reserved controls and cannot be assigned notes.
Enter may be mapped but still serves the Fn+Enter mode combo.

### Save and load a host profile

**Save profile…** writes the confirmed threshold pairs and MIDI mappings
to JSON on the computer. It does not save unsubmitted input fields or
calibration endpoints. **Load + apply profile…** validates the file, disables
output, applies settings with readback, and restores the previous enable state.
Load is not atomic: if it fails partway, some confirmed changes may remain
and output normally stays disabled. Inspect the status and retry explicitly;
do not assume the previous configuration was restored.

Close the GUI when finished; applied settings continue working without it.

## 10. Know what gets saved

| Item | After closing the GUI | After power cycle | How to change it |
| --- | --- | --- | --- |
| Completed calibration endpoints | Kept | Kept on the device | Complete a calibration run |
| Keyboard trigger level (Fn+Tab, keyboard mode) | Kept | Default | Fn+Tab editor |
| MIDI trigger point (Fn+Tab, MIDI mode) | Kept | Default | Fn+Tab raw page |
| Transmitted-velocity start (Fn+V) | Kept | Default (level 1) | Fn+V editor or the GUI |
| Brightness | Kept | Default | Fn+K/L |
| Keyboard/MIDI mode | Kept | Default (keyboard) | Switch with Fn+Enter |
| MIDI octave offset | Kept, including across mode switches | Default | Adjust with Right Alt/Ctrl |
| Lower-row MIDI mute | Kept | Default | Toggle with Fn+Left Shift |
| MIDI root / scale | Kept | Default | Select with Fn+E / Fn+S |
| Per-key thresholds set from the GUI or CDC | Kept | Default 3500 / 3600 | Save host JSON; load it again |
| MIDI note mappings set from the GUI or CDC | Kept | Default 43-note map | Save host JSON; load it again |
| Output enable state | Kept | Enabled, waiting for neutral | Use GUI enable/disable |

Only the calibration endpoints persist on-device, in the two authorized tail
pages. Every Fn-menu choice is RAM-only and returns to its default at the next
power cycle; **Fn+R clears the menu choices and deletes the stored
calibration**, returning each item above to its default. Host edits over CDC - per-key thresholds and note mappings - stay in
RAM and come back through a host profile, which contains
thresholds and MIDI mappings, **not calibration, brightness, mode, octave, row mute, root, or scale**.
Recalibrating does not overwrite your current raw threshold pairs. Committing
a Fn+Tab trigger level does replace those pairs. There is no on-keyboard
saved-profile selector or implemented media-control menu.

## 11. Reset custom settings

> **Reset deletes the saved custom calibration. It is not a factory-firmware
> restore, and the deletion cannot be undone on the device.** Save any wanted
> threshold/MIDI profile first; that JSON file is not a calibration backup.

1. Hold **Fn+R** to preview `RESET`.
2. Release the combo. The keyboard shows `RESET?`, with **Y green** and
   **N red**, both at full brightness.
3. Release every key once before answering.
4. Press **Y** to confirm, or **N** to cancel. Pressing Y and N together
   cancels. A Y already held when confirmation opened cannot confirm.
5. Release all keys to resume. After successful confirmation, calibration is
   cleared and thresholds, mappings, mode, octave, and brightness return to
   defaults. Recalibrate if needed.

Confirmation has no automatic timeout. Simply opening or cancelling it
does not erase anything. Reset touches only the two reserved custom pages,
not the bootloader, factory serial-number storage, application firmware, or
optical-controller firmware. Unknown page contents or storage errors stop
the operation; inspect the GUI/CDC status rather than repeatedly resetting.

## 12. Inspect sensor readings

These optional Linux tools use the same CDC serial port as the GUI. Use
**one reader at a time**, wait for calibration to finish, and do not confuse
a serial display mode with keyboard/MIDI performance mode.

### Whole-keyboard display

With the whole-keyboard stream selected:

```sh
python3 -u tools/decode_scan_stream.py --bars /dev/ttyACM0
# Or fixed-width decimal rows:
python3 -u tools/decode_scan_stream.py --live /dev/ttyACM0
```

These displays do not select their stream automatically. If the GUI, a dump,
or a compact capture selected another stream, use a serial command client
to send `stream on` followed by a newline, close that client, then run the
decoder. The scanner itself does not need restarting.

The block view has one caption row and one current-value row, with each key
occupying a single-character label plus a space. It is in **sensor order,
not physical keyboard order**. Letters/digits label themselves. Special
labels: `e` Esc, `t` Tab, `b` Backspace, `c` Caps, `r` Enter, `_` Space,
`m` Menu, `f` Fn, `^` Ctrl, `s` Shift, `a` Alt, `g` Windows. Left/right
modifiers share a symbol; their sensor positions distinguish them.

```text
RAW VALUE, not pressed depth:
low raw  ▁ blue  ── cyan / green / yellow ── red █  high raw
         deeper press                              nearer rest
```

The colors are a raw-value scale, **not the physical LED color guide**.
Invalid values appear as magenta `!`. Use a UTF-8/ANSI terminal; a full ANSI
view needs about 139 columns. On a narrow terminal, use `--start 40` to view
a later sensor window. The live display intentionally skips intervening
frames to show the latest received reading instead of scrolling a backlog.
Ctrl+C exits and restores the terminal.

### Capture a strike

```sh
python3 -u tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3600
# Keep capturing: release above the threshold, then press again.
python3 -u tools/decode_scan_stream.py /dev/ttyACM0 --last-key --threshold 3600 --repeat
```

This mode selects its device stream automatically. It prints **Capture
Armed** with diagnostics, identifies the triggering key, prints the next
20 decimal readings one per line, then reports velocity from the bottom-out window.
The triggering sample is excluded. The banner means the host is waiting;
it is not by itself confirmation that the device has replied.

The capture threshold is separate from the keyboard's 3500 press default;
`--threshold 3600` does not change typing settings. Triggering requires
raw **below** that value. Repeat mode rearms after the captured key rises
strictly **above** it; equality does not rearm. Release before starting
if you want a fresh strike rather than selecting an already held key.

Unlike the live display, capture fails on detected loss/overflow instead of
silently skipping samples. A key change prints a warning and starts a new
capture; do not treat the interrupted partial set as complete. Check the
exit status when saving results. Ctrl+C ends repeat mode. Its velocity is
raw counts/s using the same 8 kHz assumption and filter as the firmware,
before firmware normalization to 0–1.

After capture, reopen the GUI for GUI telemetry or select `stream on` for
whole-keyboard viewing. `stream off` stops serial streaming, not scanning.

### Private calibration backup

Advanced users can make a read-only backup of the two custom calibration
pages, using a new filename for each backup:

```sh
python3 -u tools/dump_flash.py --device /dev/ttyACM0 \
  --start 0x78000 --length 0x400 \
  --output device-dumps/calibration-backup.device-dump.bin
```

Keep its metadata companion with it and check the tool's success result.
Backups are private and Git-ignored; never publish or force-add device dumps.
This is a raw backup, not a GUI-importable profile or an automatic restore
mechanism. Do not write it to flash with a generic programmer.

## 13. Troubleshooting

| Symptom | Check first |
| --- | --- |
| Keys do not type | Is Enter blue? Switch out of MIDI mode. Exit any editor/confirmation/calibration, enable output in the GUI, then release every key. |
| Nothing works after changing thresholds | Inspect idle readings. Every sensor must exceed its release threshold to rearm; lower an unreachable release value in the GUI. |
| Right Alt/Ctrl/Shift no longer act as modifiers | Intentional keyboard-mode arrow mapping. Use left-side modifiers. |
| Fn shortcut types its base letter | Hold Fn before pressing it. A key held before Fn is not reinterpreted. Confirm keyboard mode and enabled, armed input. |
| Holding a settings combo does nothing | The word is a preview. Release either key to execute, then release all keys. Brightness changes only one step per release. |
| Cannot choose another setting after brightness taps | Release Fn and every other key to end the brightness session. |
| A MIDI key is dark/silent | Check root/scale, Fn+Left Shift lower-row mute, mapping, octave, and output enable state. Select T in the Fn+S menu to remove scale filtering. |
| MIDI arrives but there is no sound | Route channel 1 to an instrument and enable monitoring. The keyboard is not an audio synthesizer. |
| Aftertouch has no audible effect | Use an instrument/patch that responds to polyphonic key pressure and configure what it controls. |
| Pitch is shifted or bent | Check the GUI octave value and flashing Right Alt/Ctrl. Release Left Ctrl/Alt; their wheels use fixed endpoints, not calibration. |
| Velocity stays fixed while holding a key | Expected: it is the last strike estimate. Aftertouch, not velocity, follows continued travel. |
| Calibration key never turns green | Fully bottom out, hold steady for one second, and inspect its rest/candidate reading. Movement restarts only that key's timer. |
| Calibration turns red | Inactivity, cancellation, scan/USB fault, or save failure. Read the GUI reason; a partial run is not saved. |
| GUI cannot connect / shows stale values | Check device path and permissions, close every other CDC reader, and reconnect. Demo mode never connects. |
| Terminal is blank or shows binary garbage | CDC streams are binary. Use the matching decoder or GUI; send `stream off` before text status commands. |
| Live/bar decoder shows nothing after another tool | Select `stream on` with a serial command client, close it, and reopen the decoder. |
| Capture never triggers | Raw must fall below its capture threshold; check the actual readings and release before retrying. Keyboard thresholds do not set capture thresholds. |
| Settings disappeared after restart | Only completed calibration persists on-device; every Fn-menu choice and every host edit is RAM-only, so a power cycle returns them to their defaults. Reload a saved host JSON profile for thresholds and mappings, and repeat Fn+V or the Fn+Tab step for the menu choices. |
| Lights are off | Raise brightness with Fn+L. Check MIDI mappings, diagnostic `light off`, and scan/light error status in the GUI. |

Do not use repeated resets, reflashes, or forced bootloader entry as routine
troubleshooting. First inspect GUI connection, arming, thresholds, mode,
calibration state, and scan/light/MIDI errors. Avoid interrupting a save or
firmware update. If a physical key is stuck, a fresh neutral frame cannot be
achieved until that condition is resolved.

## 14. Firmware maintenance

The complete Huntsman build preset is **`huntsman`**; `keyboard-fn-menu`
is a supported alias producing the same artifact. The
`firmware` preset is USB-only and does not provide the features in this manual.
The current USB device provides NKRO keyboard, MIDI, CDC diagnostics, and
the compatible updater interface. The configuration GUI is not a flasher.
`tools/flash_application.py` flashes and then performs the cold boot: it sends
`cfg clean` over CDC so the new build starts from defaults instead of
inheriting the previous build's stored calibration
(`--keep-calibration` skips that).

For users building from source, prerequisites are Arm GNU bare-metal tools
(`arm-none-eabi-gcc`, tested 14.2.1), CMake 3.21+, Ninja, a native C compiler,
and Python 3.10+. Official NXP SDK sources are pinned and included in the
repository; the original firmware/updater EXE is not needed to compile.

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset huntsman
cmake --build --preset huntsman
```

The application binary is `build-keyboard-fn-menu/huntsman_firmware.bin`,
exactly 131072 bytes. Building/testing does not flash or reset the keyboard.
The latest build's tested scope and hardware-validation limits are recorded in
[validation status](docs/CALIBRATION.md#validation-status).

For installation, use the [custom firmware flashing tool](https://github.com/AL-255/Huntsman-V3-Pro-Mini-Flasher)'s
**application-only** workflow. Its GUI accepts the raw application `.bin`;
leave **Flash secondary firmware** unchecked. Close CDC tools, finish any
calibration, confirm the target device/image, and keep USB connected until
the update completes and the application returns. Prefer computer-initiated
bootloader entry. Do not program address zero or infer a flash address from
the application's RAM execution address.

An application-only update preserves the reserved calibration pages, but a
return to stock firmware may reuse that space. Neither Fn+R nor the
configuration GUI installs factory firmware. Bootloader, serial-number and
factory storage, and secondary optical-controller firmware are outside this
application's write scope. Do not overwrite them to solve a configuration issue.

## Quick reference

```text
TYPE       RAlt = Left   Menu = Down   RCtrl = Right   RShift = Up
FN KEYS    Esc = `   1…0/-/= = F1…F12   Backspace = Delete
           Y = Insert   P = Print Screen   H = Home   J = Page Up
           N = End      M = Page Down

SETTINGS   Fn+Enter = mode   Fn+C = calibrate   Fn+Tab = trigger
           Fn+K/L = dim/brighten   Fn+R = reset confirmation
           Hold to preview; release to act. Then release all keys.

MIDI       Channel 1   C4 = 60   RAlt/RCtrl = octave down/up
           LCtrl/LAlt = bend down/up   LWin = modulation   Space = sustain
           Fn+Left Shift = mute/restore Caps and Shift rows (release to apply)
           Fn+E = root menu   Fn+S = scale menu   Escape = cancel selection
           Scales: J major, I minor, D Dorian, H Phrygian, Y Lydian,
                   M Mixolydian, L Locrian, P/O pentatonics, T chromatic

CALIBRATE  Purple: release 0.5 s → blue: choose → amber: hold 1 s
           Green: registered. All keys complete → saved to device.
           Multiple holds allowed. No progress for 5 s → discard.

REMEMBER   Lower raw = deeper press. Defaults: press <3500, release >3600.
           Calibration saves on-device; thresholds/MIDI maps need host JSON.
```
