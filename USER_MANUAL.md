# MIDI-Typist keyboard user manual

For the Huntsman V3 Pro Mini with the complete `huntsman` firmware.
The keyboard works without the GUI. It sends keyboard/MIDI data, not audio.
For the experimental MonsGeek M1 V5 TMR, use the
[M1 board guide](docs/MONSGEEK_M1.md) for its 75% layout, transport controls,
volume/mute knob and current limitations. Huntsman-specific shortcuts and
power-cycle persistence below must not be assumed for that backend.

## Contents

- [Start here](#1-start-here)
- [Layout and lights](#2-find-your-way-around)
- [Typing shortcuts](#4-type-and-use-fn-shortcuts)
- [Settings menu](#5-use-the-settings-menu)
- [Triggers](#6-adjust-the-trigger-point) and [calibration](#7-calibrate-the-keys)
- [MIDI](#8-play-midi)
- [GUI](#9-use-the-configuration-gui)
- [Saving](#10-know-what-gets-saved) and [reset](#11-reset-custom-settings)
- [Diagnostics](#12-inspect-sensor-readings), [troubleshooting](#13-troubleshooting)
  and [firmware maintenance](#14-firmware-maintenance)

## 1. Start here

1. Connect USB with all keys released. Saved settings return automatically.
   A first installation starts in keyboard mode: Enter is green.
2. To play music, hold **Fn+Enter** to preview blue `MIDI`, then release.
   Release all keys; Enter stays blue.
3. Select this keyboard's MIDI input in your music application, route channel 1
   to an instrument, and enable input monitoring.
4. Fn+Enter again previews green `KEYBOARD`; release to return to typing.

After changing a mode or setting, release all keys before playing/typing.
Before unplugging, wait for settings to save; see [saving](#10-know-what-gets-saved).

## 2. Find your way around

### Physical keycaps

```text
[Esc ][ 1  ][ 2  ][ 3  ][ 4  ][ 5  ][ 6  ][ 7  ][ 8  ][ 9  ][ 0  ][ -  ][ =  ][Backspace ]
[  Tab  ][ Q  ][ W  ][ E  ][ R  ][ T  ][ Y  ][ U  ][ I  ][ O  ][ P  ][ [  ][ ]  ][   \   ]
[  Caps   ][ A  ][ S  ][ D  ][ F  ][ G  ][ H  ][ J  ][ K  ][ L  ][ ;  ][ '  ][   Enter   ]
[   LShift   ][ Z  ][ X  ][ C  ][ V  ][ B  ][ N  ][ M  ][ ,  ][ .  ][ /  ][    RShift    ]
[LCtrl ][LWin ][ LAlt ][               Space               ][  Fn  ][RAlt ][ Menu ][RCtrl]
```

The drawing and GUI describe ANSI. Fn is immediately right of Space; Right Alt
is next. Firmware also has ISO/JIS sensor maps, but the GUI rejects editing those
layouts rather than mislabelling them.

### The right-hand cluster changes with the mode

| Physical key | Keyboard mode | MIDI mode |
| --- | --- | --- |
| Right Alt | Left arrow | Octave down |
| Menu | Down arrow | Configurable note; unmapped by default |
| Right Ctrl | Right arrow | Octave up |
| Right Shift | Up arrow | Configurable note; unmapped by default |
| Left Ctrl / Left Alt | Normal modifiers | Pitch bend down / up |
| Left Windows | Normal modifier | Modulation wheel |
| Space | Space | Sustain pedal |

## 3. Read the lights

| Appearance | Meaning |
| --- | --- |
| Green / blue Enter | Keyboard / MIDI mode |
| White keys, dimming as pressed | Ordinary travel lighting |
| Dark MIDI note keys | Unmapped, muted, outside scale or out of MIDI range |
| Blue bottom-row controls | Octave, pitch, modulation and sustain |
| Blinking Right Alt / Ctrl | Negative / positive octave offset; faster means more octaves |
| Green keys while Fn held | Typing shortcuts |
| White settings keys while Fn held | Available settings; active Jankó/row choices may be green |
| Purple → blue → amber → green | Calibration: release/settle → pending → holding → registered |
| Red calibration feedback | Cancelled, timed out or failed; read GUI status |

Enter and control indicators remain visible even when their note is filtered.
Normal markers use global brightness. Fn previews, reset confirmation and
calibration feedback have their own visibility rules.

### How to read a word on the keyboard

All letters in the word light at 30%; one letter at a time reaches 100% for
0.2 seconds. Repeated letters flash repeatedly. After the word, a 0.5-second
pause precedes the repeat.

```text
MIDI:   M → I → D → I → pause → M …
        bright letter moves; the other word letters stay dim
```

Release either combo key to stop immediately and perform the action.
Mode names use Enter's target color; other action names are white.

## 4. Type and use Fn shortcuts

A press occurs strictly below the press threshold; release occurs strictly
above the release threshold. Equality retains the current state. Multiple
keys can remain down; the operating system controls typing repeat.

### The top row with Fn held

```text
Physical: Esc  1  2  3  4  5  6  7  8  9  0   -   =   Backspace
Fn:        `  F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12  Delete
```

Fn+Left Shift+Esc can type tilde. Press Fn before Esc for the Fn-layer action.

### Navigation shortcuts

| Combo | Output |
| --- | --- |
| Fn+Y | Insert |
| Fn+P | Print Screen |
| Fn+H / Fn+N | Home / End |
| Fn+J / Fn+M | Page Up / Page Down |

These green-hinted shortcuts act while held, unlike settings. Hold Fn and tap
them repeatedly. Releasing either key ends the shortcut. MIDI mode does not
send this typing layer; Fn+J and other settings have their MIDI meanings.

## 5. Use the settings menu

```text
Hold Fn + settings key → read preview → release either → action
                                                      → release all keys
```

| Combo | Preview / action | Mode |
| --- | --- | --- |
| Fn+Enter | MIDI / KEYBOARD: switch mode | Both |
| Fn+C | CALIBRATION: start calibration | Keyboard |
| Fn+Tab | TRIGGER: open the mode's trigger editor | Both |
| Fn+Caps | RAPID: compatibility editor, not raw rapid-trigger behavior | Keyboard |
| Fn+K / Fn+L | LIGHT− / LIGHT+: brightness down / up | Both |
| Fn+J | JANKO: toggle built-in layout | MIDI |
| Fn+V | VELOCITY: open velocity-start editor | MIDI |
| Fn+Left Shift | LOWER-OFF / LOWER-ON: mute / restore lower playing rows | MIDI |
| Fn+E / Fn+S | KEY / SCALE: select root / scale | MIDI |
| Fn+R | RESET: open confirmation, not erase yet | Both |

Press Fn first or both together; a setting held before Fn needs a fresh press.
Choose one setting at a time. In MIDI mode Fn suppresses new notes, releases
sustain and centers wheels; entering a settings preview clears sounding notes.

### Brightness: keep Fn held and tap

Hold Fn, tap K or L as often as needed, then release Fn. Each K/L release
changes one of 20 brightness levels. Other settings require all keys released
before another action. Fn hints remain usable at zero normal brightness.

## 6. Adjust the trigger point

### Understand press and release thresholds

Readback decreases as the key goes down. Defaults are **press 3500,
release 3600**, with allowed pairs `1 ≤ press < release ≤ 4095`.

```text
Released (~3900) ── press ↓ ── <3500: DOWN
Held             ─ release ↑ ── >3600: UP
                     3500…3600 retains the previous state
```

The gap prevents repeated triggers near one threshold. Leave margin below
each key's resting reading so it can release. Calibration measures travel
bounds; it does not change these raw thresholds automatically.

### Quick adjustment without the GUI

In keyboard mode, release Fn+Tab to enter. Choose 1…0 (levels 1…10);
the selected digit is green and the white bar shows travel. Escape or Fn+Tab
**commits and exits**; Escape is not Cancel here. Releasing Fn alone does not
exit. Arrow controls step the selection.

The global level converts calibrated travel into every key's threshold pair,
including recovered fixed-threshold exceptions. It replaces custom GUI pairs.
Use the GUI for independent per-key settings. Fn+Caps retains its editor
interaction but does not implement rapid-trigger operation in the raw engine.

## 7. Calibrate the keys

### Run a calibration

1. In keyboard mode release Fn+C, or click **Calibrate keys → device flash**.
2. Release every key. Leave untouched for 0.5 seconds while purple.
3. Fully press one or more blue keys and hold for one second. Each becomes
   amber, then green independently. Include Fn, modifiers and Space.
4. Continue until all 61 keys register. Green keys may remain held while
   you press others. Then release all keys to resume.

```text
Release all → 0.5 s settle → blue keys → hold 1 s → green
                              ↑             |
                              └─ more keys ─┘ → all complete → save
```

Moving or releasing an unfinished key restarts only its hold. Five seconds
without progress, GUI cancellation or scan/USB failure discards the staged
run; previous calibration remains. No partial calibration is saved.
Wait for completion and saved generation, not just the start acknowledgment.

New bounds affect lighting and aftertouch. They do not prove calibrated force,
millimeters or velocity. See [calibration design](docs/CALIBRATION.md).

## 8. Play MIDI

### Connect to an instrument

Use Fn+Enter to select MIDI, then select its input in your DAW/synth.
All messages use channel 1: notes, velocity, polyphonic aftertouch, pitch bend,
modulation and sustain. This is not MPE. Your instrument must support a message
for it to have an audible effect.

### Default notes: two overlapping playing ranges

Read the physical key above its note; these groups show pitch order, not spacing.
This project calls note 60 C4, 72 C5 and 84 C6. Other software may label octaves
differently; compare note numbers.

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

There are 43 default note keys. Other note-capable keys are dark until assigned;
Enter still marks the mode. Keys mapped to the same pitch share one note:
first press starts it, last release stops it, maximum held travel supplies
aftertouch. The second key does not retrigger or replace attack velocity.

### Move the trigger point in MIDI mode

Fn+Tab opens the MIDI raw-trigger page. Choose 1…0, then Escape to commit.
Level 1 is the deepest threshold (1500); level 10 is 3599. Each key retains
its release threshold; press is capped below it. The GUI offers the same
range and per-key editing. Release all keys afterward.

### Set the transmitted-velocity start

Fn+V opens VELOCITY. Choose 1…0 and Escape to commit:
1 preserves measured velocity, 10 sends maximum velocity; intermediate
levels raise the minimum without removing variation above it.
The GUI's **Velocity start** edits the same device setting.

### Play the built-in Jankó layout

Fn+J toggles Jankó on release. Whole-tone rows are staggered; sharp-note
positions glow yellow. Custom mappings remain stored and return when Jankó
is switched off. The GUI shows effective Jankó captions while active.

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

Scale and octave still apply. Jankó bypasses lower-row mute: both lower rows
play while it is active. Control roles do not change.

### Use only the upper playing range

Outside Jankó, Fn+Left Shift mutes/restores the physical Caps and Shift rows. The Esc/Tab rows
and bottom-row controls remain. Muted note keys go dark; Enter's blue marker
remains but its note is muted. Mappings are unchanged.

### Choose a root and scale

Release Fn+E to open KEY, then release all keys. Choose the upper piano-row
key for the root (Tab=C, 1=C#, Q=D, 2=D#, W=E, E=F, 4=F#, R=G,
5=G#, T=A, 6=A#, Y=B). The equivalent second octave works too.
Selectors use this fixed piano map even when Jankó or custom mappings are active.

Fn+S similarly opens SCALE:

| Key | Scale | Semitone intervals above root |
| --- | --- | --- |
| J | Major | 0, 2, 4, 5, 7, 9, 11 |
| I | Natural minor | 0, 2, 3, 5, 7, 8, 10 |
| D | Dorian | 0, 2, 3, 5, 7, 9, 10 |
| H | Phrygian | 0, 1, 3, 5, 7, 8, 10 |
| Y | Lydian | 0, 2, 4, 6, 7, 9, 11 |
| M | Mixolydian | 0, 2, 4, 5, 7, 9, 10 |
| L | Locrian | 0, 1, 3, 5, 6, 8, 10 |
| P | Major pentatonic | 0, 2, 4, 7, 9 |
| O | Minor pentatonic | 0, 3, 5, 7, 10 |
| T | Chromatic / 12T | All twelve |

Choices are dim white, current choice green and Escape red. Hold a choice to
preview its name, release to commit. **Escape cancels** these menus.
Root and scale can be selected in either order. Only in-scale, enabled,
mapped, in-range notes light and play. T removes the scale filter.
Control/mode markers remain visible. Default is C/chromatic.

### Octave, pitch bend, modulation, and sustain

| Control | Function |
| --- | --- |
| Right Alt / Right Ctrl | One octave down / up per press, limited to ±10 |
| Left Ctrl / Left Alt | Bend down / up; opposing depths sum |
| Left Windows | Modulation (CC1) |
| Space | Sustain (CC64): 127 while down, 0 on release |

Octave changes affect new notes; held notes keep their pitch. Out-of-range notes
are silent. Only the shifted direction blinks, faster for larger offsets.

Wheels use fixed readback endpoints: 3800 is 0%, 1000 is 100%; per-key
calibration/threshold changes do not rescale them. Pitch bend spans −100…+100%.
Space uses its editable Schmitt thresholds. Whether sustain or polyphonic
aftertouch is audible depends on the receiving instrument.

### What the velocity number means

The firmware estimates each key independently from up to ten readbacks starting
at the trigger, stopping near bottom-out. Windows longer than five samples
discard one outlier interval. The result is normalized to 0…1 with
4,500,000 counts/s as maximum, then converted to Note On velocity 1…127.

This is not force or measured physical speed. The math assumes 8000 scans/s;
measured pinned delivery is about 1.34 ksample/s. The GUI displays the device
float, not an estimate from its ~30 Hz snapshots. See [velocity design](docs/KEY_VELOCITY.md).

## 9. Use the configuration GUI

On Linux, install Python and Tk, then install the GUI's MIDI dependency and run:

```sh
python3 -m venv build-gui-venv
build-gui-venv/bin/pip install -r tools/requirements-gui.txt
build-gui-venv/bin/python tools/keyboard_gui.py
# Offline preview:
build-gui-venv/bin/python tools/keyboard_gui.py --demo
```

Your user needs access to ALSA MIDI. Select the dedicated control port in the
GUI; use the first, performance port in your DAW. Only one GUI should connect.
Linux may truncate the control name to `Huntsman V3 Pro Mini MIDI MIDI-`;
the GUI recognizes cable 1 automatically. No serial port is exposed. Live editing
supports Huntsman ANSI; the [M1 layout preview](docs/MONSGEEK_M1.md#board-and-gui-layout)
does not yet support configuration of connected M1 factory firmware.

### Read the screen

Connect, then select a drawn key. Tiles show live raw values, press state and
latest velocity. The panel shows thresholds, note/control role, waveform,
calibration progress and settings-save status. Stale telemetry disables edits.
A displayed HID submission is not proof of host receipt. If the window is
short, scroll the settings panel on the left: its fields, buttons and the
shortcut reference stay reachable instead of being cut off.

### Change one key or all keys

Disable keyboard output while tuning if needed. Enter press/release values,
then **Apply to selected key**, or confirm **Apply thresholds to all keys**.
The GUI checks command acknowledgment and actual readback. Invalid pairs change
nothing. Release all keys after an edit; remember to re-enable output.

### Remap a keyboard key

Select a key in the drawing, choose its output from the **Keyboard** dropdown,
and click **Apply keycode**. Choose **Disabled** to send nothing, or select a
letter, function/navigation/keypad key or modifier. Uncommon keyboard usages
are listed by hexadecimal code; the operating system determines their meaning.

Physical labels stay fixed. **Fn and every Fn combination are not remappable**:
for example, remapping Y still leaves physical Fn+Y as Insert. Keyboard remaps
do not change MIDI notes, thresholds or calibration. Two keys can share an
output; it remains held until both are released.

Release all keys after editing and wait for **settings saved**. The keyboard
then keeps the mapping in its own flash and works without the GUI after unplugging.

### Assign a MIDI note

Choose a note number 0…127, note name (sharps/flats accepted), or Off.
Fn, left Ctrl/Windows/Alt, right Alt/Ctrl and Space are reserved controls.
Changing a mapping releases held output and waits for neutral.

### Save and load a host profile

JSON export/import stores thresholds and keyboard/MIDI assignments, not calibration or
all menu settings. It is optional: device saves are automatic.
Import temporarily disables output, checks each edit, then restores output
on success. It is not atomic; a failed batch may leave confirmed edits applied.

The optional **Hold first 20 pts of keystroke** view switches to the selected
sensor's full-rate stream. GUI status pauses and edits disable until return.
It captures 20 points **including the trigger**.
Overflow fails rather than joining samples across a gap.
See [GUI guide](docs/KEYBOARD_GUI.md#keystroke-hold-mode).

## 10. Know what gets saved

The device automatically saves committed modes (including Jankó), brightness,
root/scale, row mute, octave, velocity start, per-key thresholds/mappings,
output enable and completed calibration.

Release all keys and wait for **settings saved** before unplugging.
Saving waits for 250 ms without changes and no open menu/calibration.
Without the GUI, leave all keys released for at least half a second.
Unplugging while pending can restore the previous complete save.

Ordinary playing does not write flash. Held notes, wheels, sustain, velocities,
unfinished previews and partial calibration are not restored.
Missing/corrupt custom saves initialize defaults; compatible firmware updates
retain valid saves. See [storage design](docs/DEVICE_CONFIG_STORAGE.md).

## 11. Reset custom settings

**Reset deletes custom settings and calibration; it does not restore factory firmware.**

1. Hold Fn+R to preview RESET, then release to open RESET?.
2. Release all keys. Y is green, N red.
3. Press Y to confirm or N to cancel. Pre-held Y cannot confirm; Y+N cancels.
4. Release all keys to apply defaults and save them.

Opening/cancelling the page does not erase. Reset writes only the two reserved
custom tail pages, never the Razer serial/settings area or bootloader.
Controller/verification failures stop and report an error. Deleted calibration
requires recalibration or a private backup.

## 12. Inspect sensor readings

The GUI is the only desktop application for this project. Its keyboard tiles
show the newest whole-keyboard snapshot. Raw readbacks are 16-bit containers
with valid values 1…4096; lower means deeper.

Select a key and enable **Hold first 20 pts of keystroke** to stream that
sensor on every hardware acquisition. The graph captures the first 20 points
including the trigger, then waits for release before rearming. Samples are
sequence-checked: corruption or overflow ends capture instead of hiding loss.
Normal status and configuration resume when hold mode is disabled.

JSON export saves thresholds and note assignments, not private flash or calibration.
There is no GUI flash-dump/restore action. Keep any existing private backups
outside the repository.

## 13. Troubleshooting

| Symptom | Check |
| --- | --- |
| No typing after a change | Release all keys; check enable state and idle values above release thresholds |
| GUI cannot connect | Close other MIDI SysEx tools, check permissions/port and use matching host tools |
| MIDI is silent | Blue Enter, instrument monitoring, channel 1, mapping, row/scale filter and octave range |
| No aftertouch/sustain effect | Receiver must implement the message; check instrument routing |
| Settings vanish after restart | Wait for saved, not just ACK; inspect storage errors and pending/open menus |
| Calibration turns red | Read timeout/cancel/scan/storage reason; previous calibration is retained |
| Capture stops with an error | Resolve overflow/disconnect/framing cause; do not splice across missing samples |
| LEDs fail but USB works | Read light status; a latched bus fault is not fixed by repeated reset commands |

Do not erase Razer data or force bootloader recovery as a routine diagnostic.
[Validation limits](docs/VALIDATION.md) distinguish tested behavior from assumptions.

## 14. Firmware maintenance

To identify your installed firmware, connect the GUI and read its firmware
identity in **Device flashing**. The full `git=` hash identifies the build's
base commit. `state=dirty` means it includes uncommitted changes;
`state=unknown` means Git provenance was unavailable. Include this identity
when reporting an issue. It is read from the keyboard, not your PC's checkout.

Follow [Building](docs/BUILDING.md) for dependencies, the complete `huntsman`
preset and tests. The current custom firmware and matching GUI are the only
supported implementation.

Open the GUI’s **Device flashing** tab. Select the keyboard model, refresh its
identity, choose MIDI-Typist or Razer, and select the appropriate application
image. Validate it, confirm the model, then review and flash. Keep the keyboard
connected until it returns and the tab refreshes its identity.

The tab also works when the keyboard starts in the Razer bootloader. Razer
restoration requires your own matching firmware file; no stock image is bundled.
See [Device flashing](docs/DEVICE_FLASHING.md) for supported files and safety
limits. Only the current custom firmware and matching GUI are supported.

Ordinary custom updates retain current-format settings and calibration.
Only a confirmed Fn+R reset clears them. Application updates never program
address zero, the bootloader, factory settings or secondary firmware.

## Quick reference

```text
Fn+Enter  keyboard / MIDI     Fn+C       calibration (keyboard)
Fn+Tab    trigger editor      Fn+K/L     brightness −/+
Fn+R      confirmed reset     Fn+J       Jankó (MIDI)
Fn+E/S    root / scale        Fn+V       velocity start (MIDI)
Fn+LShift lower-row mute      RAlt/RCtrl octave −/+ (MIDI)
LCtrl/Alt pitch −/+           LWin       modulation (MIDI)
Space     sustain (MIDI)
```
