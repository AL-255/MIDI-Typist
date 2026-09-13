# Production-derived per-key travel lighting

Travel lighting is included in the `huntsman`
application. The recovered controller/mapping details below remain applicable;
see [current validation](VALIDATION.md).
User calibration supplies independently saved per-key endpoints, but
optical counts are not a validated linear millimeter or force measurement.
Travel normalization and effect composition live in `firmware/app`.
The Huntsman board supplies LED channel placement, bus transactions and upload
timing. None of the controller addresses below is a generic platform contract;
see [lighting ports](PORTING.md#5-add-lighting-storage-and-host-integration).

## Behavior and limits

The `travel-lighting` preset brings up USB first, automatically starts one
optical scan attempt after USB configuration, then initializes lighting once
ASIC layout discovery succeeds. It does not require CDC to be opened or a
`scan start` command. The current complete preset also enables standalone NKRO
after neutral arming; the lighting-only preset requires `keys on`.
Diagnostic/USB-only presets retain their own startup policy.

Each key is white with inverse endpoint-normalized travel: fully lit at rest,
dimming toward black as it is pressed. It does not use binary pressed/released state, the FN editor, actuation
threshold, rapid-trigger hysteresis, gamma correction, or the keyboard engine's
low-level deadband. MIDI mode/shift and calibration indicators overlay the
base white effect. MIDI mode masks keys whose configured note is unmapped;
the mask follows GUI mapping changes on the next lighting frame. Mode/shift
indicators remain exceptions as described in [MIDI design](MIDI_DESIGN.md) and
[calibration](CALIBRATION.md):

```text
travel = round(255 * (upper - raw) / (upper - lower)), clamped to 0..255
PWM = 255 - travel
raw >= upper: maximum white; raw <= lower: off
```

Invalid input, unsettled scanning, stopped/faulted scanning, USB unconfigured,
or no new scan for 100 ms requests a black frame. A new full lighting frame is
started at most every 40 ms, corresponding to production's alternating
20 ms controller slots. Each upload snapshots the newest desired frame;
there is no queued-frame FIFO. The primary and secondary controller
uploads use the same frozen snapshot.

Invalid per-key raw values or endpoints remain dark, not inverted to full
brightness. `lighting_travel_pwm` retains its press-increasing normalization
for MIDI aftertouch; only the LED frame inverts it. Keyboard mode lights all
keys independently of their MIDI mappings. Enter's mode marker and active
octave-shift blink remain explicit overlays. The five octave/wheel controls and Space sustain
use Enter's full-intensity blue in MIDI mode, with the same global brightness
scaling as ordinary keys; the active right-side octave indicator
blinks blue/off. Other unmapped MIDI keys stay dark.

**This is linear in production-normalized optical travel, not a validated
linear millimeter measurement or perceived-brightness curve.** The existing
scanner attempts the original ASIC endpoint calibration. If rejected, it keeps
production defaults 2240/3360 from `0x20016464`. Using defaults may leave
dark/saturated portions of the physical stroke. Saved user calibration overrides these endpoints
after startup settling; see [validation limits](VALIDATION.md).

Production additionally reads CRC-checked persistent calibration blocks
(`0x2001ad30`: flash offsets `0x49000`, `0x49600`, `0x49c00`) and applies
overrides in `0x20015dec`. Those persistent reads/overrides are not implemented
here. Instead, our own calibration writes only the verified unused tail pages
0x78000 and 0x78200; it does not replace the primary factory/settings blocks.
Accurate full-stroke millimeter proportionality still requires physical
measurements. The LED mapping/protocol and PWM arithmetic are independently
testable without assuming that accuracy.

## Production evidence

Reference: read-only `../extracted_firmware/analysis/primary_app_listing.asm`,
`primary_app_decompiled.c`, and hash-pinned raw application
`Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin` (SHA-256
`d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27`).
The broken sibling implementation was not consulted.

| Production entry/data | Recovered behavior |
| --- | --- |
| `0x2000ddc8`, `0x20002410`, `0x20002ddc` | FLEXCOMM1/I2C1, FRO12M, 400 kbit/s; P0_13/P0_14 IOCON `0x101` |
| `0x2000cfa8`, `0x2000dfa4` | Write P0_8 and P0_26 low, wait 10 ms, both high, wait 5 ms, initialize controllers |
| `0x2001d4ff`, `0x2000cb14` | Stored address bytes `a0 d8` are shifted right before SDK calls: 7-bit addresses `0x50`, `0x6c` |
| `0x200092d0(0,0)` | Primary initialization, including page unlock/select and 24 channel-enable bytes |
| `0x2000de70(1,0)` | Secondary initialization only for ASIC layout 3 (JIS) |
| `0x2000e204`, `0x200092d0(0,3)` | Primary page maintenance after 26 primary update slots |
| Runtime `0x04000004`, 75 nine-byte records | Key ID, row/column, controller, individual R/G/B channel indices, effect metadata |
| `0x2000cbf0`, `0x2000e410` | Separate ANSI/ISO FN/right-Alt patches for scan and lighting maps |
| `0x200098b0` | Actual normal-effect color-to-controller renderer used for differential tests |
| `0x20016464`, `0x20015dec`, `0x2000e354` | Default endpoints, endpoint calibration, inverse raw normalization |

The RGB channels are **not packed per key**. For example A's red/green/blue
channels are `a1/91/b1`, and ANSI FN (raw index 43) uses `ae/9e/be`, not the
unpatched FN record's `af/9f/bf`. ANSI uses 183 primary channels, ISO 186;
JIS uses 183 primary plus 12 secondary channels. Unmapped channels stay zero.

`tools/lighting_reference_tables.py` executes the original scatter initializer,
the two layout patches and controller initialization/maintenance routines.
It emits `firmware/boards/huntsman_v3_pro_mini/src/lighting_reference_tables.c`, not a handwritten physical-row
guess. The observed register programs are:

- Primary: `fe=c5`, `fd=03`, `00=05`, `01=ff`, `0f=07`, `10=07`,
  `fe=c5`, `fd=01`, 192 zeros at `00`, `fe=c5`, `fd=00`, **24 ff bytes
  at `00`**, `fe=c5`, `fd=01`.
- JIS secondary: `2f=00`, wait 5 ms, `00=01`, twelve `10` bytes at `17`,
  `26=00`, `27=00`, twelve zeros at `04`, `13=00`.
- Primary maintenance: `fe=c5`, `fd=00`, 24 ff bytes at `00`, `fe=c5`,
  `fd=03`, `00=01`, `01=ff`, `e0=01`, `e1=e2=e3=00`, `e0=00`,
  `fe=c5`, `fd=01`.

Steady updates write 192 primary bytes at `00`; JIS additionally writes twelve
bytes at secondary `04`, then `13=00`. No speculative registers are used.
The pins' electrical roles are not inferred beyond this recovered sequence.

## Fn menu and brightness

The [Fn menu](FN_MENU.md) replaces the travel frame with supported-key hints.
In MIDI mode Fn+Left Shift adds a white hint for the lower-row mute toggle. Muted
Caps/Shift-row note keys are dark independent of their mappings; Enter's blue
mode marker and bottom-row control hints remain visible. Text previews use
the same white 30%/100% `LOWER-OFF` / `LOWER-ON` renderer.
Fn+E/S add white root/scale menu hints. Normal note lighting shares the MIDI
eligibility predicate: disabled rows, nonmembers of the selected root/scale,
unmapped and out-of-range transposed notes are dark. Root/scale menus instead
show available selectors dim white, the current value green and Escape red;
held choices preview their names. Mode/control markers remain independent.
Keyboard function/navigation shortcuts are green; settings are white except
Enter's target-mode color. The trigger editor draws number-row feedback;
calibration keeps its independent progress colors. Fn settings preview names
while held and act on release; green keyboard shortcuts send held NKRO keys.
RESET instead opens a persistent `RESET?` confirmation with Y solid green and
N solid red at full brightness; the question mark animates on the /? key.
Outside calibration/editor feedback, Fn+K/L scales all channels
using the original 20-step brightness table. Fn hints remain visible between
repeated K/L taps with Fn held, with a minimum intensity so brightness can be
restored from zero. Held shortcut text
overrides travel/menu markers at absolute 30%/100% PWM, ending and executing on either
key's release without blocking the scheduler. Mode names share Enter's target
color (blue MIDI, green keyboard); other names are white. The final upload still obeys
lighting-off, validity, stale-frame and pending-buffer rules.

## SDK and failure handling

The build uses the existing official MCUXpresso Installer-selected NXP source
snapshots and Arm GNU 14.2.1 toolchain. No vendor sources were modified. LED
GPIO, clock, FLEXCOMM and I2C operations use the SDK. The blocking diagnostic
`firmware/boards/huntsman_v3_pro_mini/src/lighting.c` is not linked into the application.

Production uses DMA channel 7 and retries/reinitializes on errors. The application
deliberately uses the SDK's **nonblocking interrupt** I2C API: its DMA error
paths call `DMA_AbortTransfer`, which can busy-wait indefinitely. LEDs have
FLEXCOMM1 IRQ priority 3, below USB and optical DMA. LED initialization never
calls `DMA_Init`, so it cannot reset the scanner's shared DMA controller.

An I2C error or 20 ms transfer timeout latches one fault and disables LED I2C
interrupts; the driver handle and payload remain allocated. There is no bus
abort spin, retry, GPIO cycle, MCU reset, or reinitialization. USB/CDC and optical
scanning remain serviced. On a bus fault, the last already-applied LED values
may remain visible: blacking them cannot be guaranteed over a failed bus.
`light on` does not clear a latched fault or restart hardware.

## Build and commands

```sh
cmake --preset host-tests
cmake --build --preset host-tests
cmake --preset huntsman
cmake --build --preset huntsman
cmake --build --preset host-tests --target audit-lighting
cmake --build --preset huntsman --target audit-lighting
```

The audit targets require the Python dependencies in `tools/requirements-audit.txt`.
They neither open nor flash a device. Current output is in
`build-keyboard-fn-menu`; the binary is 131072 bytes.

Runtime diagnostics are:

```text
stream off
light status
scan status
scan sample 20
light off
light on
stream on
```

`light status` reports phase, requested state, layout, transfers, complete frames,
errors and accepted calibration count. Phases: 0 off/not started, 1 low wait,
2 high wait, 3 primary init, 4 secondary init, 5 running, 6 maintenance, 7 fault.
`light off/on` changes the desired effect without hardware reinitialization.
As before, text replies are suppressed while the binary stream owns CDC.

## Validation

[Reference and ARM tests](BUILDING.md) compare sensor/channel maps, init and
maintenance transactions, scaled/inverse frames and SDK I2C uploads.
Fault injection checks stalled/NACK transfers, immutable pending data and
continued USB/scanning. These are register models, not measurements of light,
travel or power; see [Validation](VALIDATION.md).
