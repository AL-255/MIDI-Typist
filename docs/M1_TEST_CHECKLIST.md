# M1 post-flash test checklist

Run this after installing the current M1 application, and record what actually
happened. It is the hardware half of the evidence that
[validation](VALIDATION.md) describes as missing. The GUI remains the only
supported host application; use one control session at a time.

The default artifact is **USB-only**: `MT_M1_WIRELESS` is `OFF`, so Bluetooth and
2.4 GHz are not compiled into this image at all (see
[the M1 guide](MONSGEEK_M1.md#wireless-build-switch)). Fn+F1–F5 are therefore
inert by design and no pairing can start. A wireless build needs
`-DMT_M1_WIRELESS=ON` and a separate flash.

## 1. Before flashing

- [ ] Keyboard on the control cable, USB powered for the whole update; other
      MIDI clients closed.
- [ ] GUI identifies the build target as `MG-M1V5TMR` over the *USB-bound*
      control port (`Read firmware details…`).
- [ ] Image selected: `build-m1-hal/m1_development.bin` from the current build.
      The GUI shows its byte count and SHA-256; the worker verifies the same
      digest before any destructive step.
- [ ] Battery comfortably above empty if you intend to unplug afterwards. The
      application only arms IAP entry while externally powered.
- [ ] Understood: **every update erases the custom profile slots** — saved
      thresholds, mappings, MIDI settings and the custom calibration are gone.
      Factory sensor-calibration pages and the bootloader are preserved by the
      updater. Recalibration is required after the flash.
- [ ] Understood: early startup failure of a new application may need hardware
      debugging; power cycling is not a guaranteed recovery path.

## 2. Flash

GUI path (supported): start the GUI with an interpreter that has `python-rtmidi`
and `pyusb` (`tools/requirements-gui.txt`), select the MonsGeek M1 V5 TMR model,
read the firmware details, choose the image, pick **Reflash experimental
MIDI-Typist**, confirm, and approve the privilege prompt for raw USB access. Keep
the cable connected until the worker reports completion.

On this machine the checkout's `.venv-audit/bin/python` already carries Tk,
python-rtmidi and pyusb, while the default interpreter lacks pyusb, so either run
the GUI with that interpreter or install the requirements first:

```sh
.venv-audit/bin/python tools/keyboard_gui.py
```

Raw USB access is not granted by any udev rule here: `/dev/bus/usb/*` belongs to
root and the worker is normally launched through the polkit prompt. The flash
therefore needs one interactive authorization.

The same worker can be driven directly, which is what the GUI does:

```sh
python3 tools/device_flash_service.py --gui-worker flash \
  --model monsgeek-m1-v5-tmr --token <token> --action reflash \
  --image build-m1-hal/m1_development.bin --sha256 <digest shown by the GUI>
```

`--token` comes from the adapter's own discovery of the selected physical port,
and the digest is the padded application digest the GUI displays — do not
substitute the raw file hash. The command needs write access to
`/dev/bus/usb/...`, so run it with the same privilege escalation the GUI uses.

Expected sequence: the keyboard leaves the application, re-enumerates as the
factory bootloader on the same port, is programmed in 64-byte blocks without
automatic retries, and only reports success after the bootloader's checksum and
readback verdict. A failed or interrupted transfer leaves the device in the
loader, where re-running the same flash is the recovery path; record the exact
message instead of retrying blindly.

## 3. Immediately after the update (no typing needed)

- [ ] Identity: `build=v0.1.0-MG-M1V5TMR git=<checkpoint> state=clean`, target
      `MG-M1V5TMR`.
- [ ] Transport line reads **`USB (USB-only build): ready`** — that is the new
      capability report; there is no Bluetooth/2.4 GHz transport in this image.
- [ ] Idle for two minutes: scan and lighting error counters stay at zero and
      the GUI keeps receiving fresh snapshots (sequence advances).
- [ ] Settings line reflects reality (`settings pending`/defaults right after an
      update that erased the profile; `settings saved` after the first
      confirmed save).
- [ ] Calibration: run Fn+C and hold every blue key until green, or confirm the
      GUI marks provisional bounds as **unsaved**. Do not accept unsaved bounds
      as factory calibration.

## 4. Functional checks (needs hands on the keyboard)

| # | Check | Expected | Record |
| --- | --- | --- | --- |
| 1 | Type every key of all six rows in a text editor | Every physical key types its label; no stuck or missing key | |
| 2 | Modifiers and NKRO | Shift/Ctrl/Alt/GUI work; 8+ simultaneous keys all register (30-byte NKRO report) | |
| 3 | Base mapping | Keycodes match the printed layout; Fn layer shortcuts from the [manual](../USER_MANUAL.md) table | |
| 4 | Fn+F1–F5 | Nothing happens; transport stays USB; no pairing state in the GUI | |
| 5 | Fn+Tab (keyboard mode) | Actuation editor opens; digits select 1–10; Esc exits and commits | |
| 6 | Fn+Tab (MIDI mode) | Raw trigger page opens; 1 = bottom-out, 0 = release−1 | |
| 7 | Fn+Caps | Rapid-trigger compatibility editor opens and exits | |
| 8 | Fn+V | Velocity-start page; 1 = 0%, 0 = 100%; value survives a mode switch | |
| 9 | Fn+R | `RESET?` preview; Y confirms, N cancels; after release the GUI shows defaults and a saved record; recalibrate afterwards | |
| 10 | MIDI notes | Fn+Enter switches mode; notes on the Performance port at C4=60 | |
| 11 | Velocity | GUI velocity for a key matches the transmitted note velocity | |
| 12 | Aftertouch / chords | Poly aftertouch after the trigger; chords keep independent velocities | |
| 13 | Jankó / scales | Fn+J toggles the layout; Fn+E and Fn+S select root/scale and filter notes | |
| 14 | Wheels / sustain | LCtrl/LAlt pitch, LWin modulation, RAlt/RCtrl octave, Space sustain (CC64) | |
| 15 | Knob | Rotation changes volume, press mutes; no stuck consumer key after menu use | |
| 16 | Saved state | Change a setting, wait for `settings saved`, press the keyboard's normal reset → settings and profile survive | |
| 17 | Sleep/wake on battery | Unplug, leave idle: the device sleeps (LEDs dark); a key wakes it; the waking press must not type until released | |
| 18 | Cable return | Replug: USB typing returns after enumeration; no fault indicator | |

For rows 10–14 use a MIDI monitor or your DAW on the Performance port; the
control port carries configuration traffic and must not be used as a performance
output.

## 5. What a failure looks like

Stop and record the exact sequence if any of these appear:

- scan or lighting error counters increase, or snapshots stop advancing;
- a key or note sticks, or a mode switch leaves notes sounding;
- a menu cannot be left, or Fn+R cannot be confirmed/cancelled;
- the GUI reports a storage fault, `settings SAVE FAILED`, or an unspecified
  transport fault;
- sleep never wakes on key or cable, or the device faults instead of restoring;
- the device stops enumerating (record whether it is the application or the
  bootloader PID before doing anything else).

## 6. What this cannot establish

Wireless operation (absent by design), radio sleep behavior, battery life,
charging polarity or full-charge claims, electrical/timing margins, worst-case
pressed-key performance, and any statement that the port is ready for daily use.
Those remain open in [validation](VALIDATION.md#not-established).
