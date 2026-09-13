# Huntsman optical scan and Fn behavior

The complete `huntsman` application starts scanning and
keyboard reporting after USB configuration. It uses the recovered optical
ASIC path and key/action maps, per-key raw Schmitt thresholds, MIDI routing,
travel lighting and parallel user calibration.
Optical transport, recovered tables and editor policy belong to
`firmware/boards/huntsman_v3_pro_mini`; event/menu processing lives in
`firmware/app`. The [layout adapter](../firmware/boards/huntsman_v3_pro_mini/src/layout_port.c)
connects them. Addresses below are behavioral reference facts for this board,
not portable API constants. See [porting](PORTING.md) for another platform.

## Production reference and entry points

Only the supplied production decompilation, listing, and raw application were
used as behavioral evidence. `../extracted_firmware` remains read-only. The
old broken sibling implementation was not consulted.

Raw reference: `Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin`, SHA-256
`d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27`.

Addresses below identify `FUN_<address>` in
`../extracted_firmware/analysis/primary_app_decompiled.c`; the listing contains
the corresponding actual instructions. Some functions have noncontiguous bodies.

| Address | Recovered role | Implementation |
| --- | --- | --- |
| `0x2000d5d4`, `0x200142fc` | Key-event routing and action dispatch | `firmware/app/src/keyboard_engine.c` |
| `0x2000b4d8` | Normal/FN action lookup | `firmware/boards/huntsman_v3_pro_mini/src/keyboard_layout.c` |
| `0x200141f8` | FN state; release held actions whose layers differ | `firmware/app/src/keyboard_engine.c` |
| `0x2000f41c` | Control action `0x70` enters actuation editor, `0x71` rapid-trigger editor | `firmware/app/src/keyboard_config.c` |
| `0x200134fc` | Editor key handling, level changes, switching and exit | `firmware/app/src/keyboard_config.c` |
| `0x2001a3bc` | Commit thresholds/profile changes | Level subset persisted by the Fn-menu settings record; per-key pairs stay RAM-only |
| `0x2000cbf0`, `0x2000c1cc` | Layout patch and raw-sensor mapping | `firmware/boards/huntsman_v3_pro_mini/src/keyboard_layout.c` |
| `0x20015dec`, `0x2000e354` | Calibration endpoints and inverse raw-to-level conversion | `firmware/boards/huntsman_v3_pro_mini/src/optical_key.c`, `firmware/boards/huntsman_v3_pro_mini/src/keyboard_scan.c` |
| `0x20015c04`, `0x200164ac`, `0x2001620c`, `0x20015bc0` | Normal thresholds, rapid-trigger exclusions, editor previews | `firmware/boards/huntsman_v3_pro_mini/src/keyboard_scan.c` |
| `0x2001a918` | Ordinary/rapid-trigger hysteresis | `firmware/boards/huntsman_v3_pro_mini/src/optical_key.c` |
| `0x2000ca68`, `0x2000dbac`, `0x200174bc` | GPIO sequence, SPI/DMA setup, ASIC scheduler | `firmware/boards/huntsman_v3_pro_mini/src/optical_bus.c`, `firmware/boards/huntsman_v3_pro_mini/src/optical_transport.c` |

The production key IDs are **not HID usages**: FN=`3b`, Tab=`10`, Caps=`1e`,
Escape=`6e`; number-row 1 through 0 are IDs `02` through `0b`.
The compressed grid starts at runtime `0x20020800`. ANSI/ISO initialization
patches positions 52/53 to FN/right-Alt; ANSI FN is raw sensor 43, Tab 17,
Caps 33, Escape 8.
`tools/production_arm.py` runs the original scatter-loader/decompressor to
recover these initialized tables, rather than treating compressed bytes as C arrays.

## Recovered user interaction

- Holding FN sets the FN layer/configuration latch (`0x04000e42`). It is
  distinct from the actuation/rapid editor mode byte (`0x04000901`).
- FN+Tab enters actuation mode 1; FN+Caps enters rapid-trigger sensitivity
  mode 2. Entry requires a press, live FN, FN-layer action, and unlocked profile.
- Releasing FN **does not exit either editor**.
- Number keys 1–0 select levels 1–10. Releases and unrelated keys are consumed.
- Escape commits a dirty edit and exits. FN+the current editor's shortcut
  also commits/exits. The other shortcut commits then switches editor.
- In rapid mode, Caps without FN toggles rapid-trigger enable without exiting.
- Increment/decrement keys are layout-dependent. ANSI/ISO uses IDs `39/40`
  and `3e/81`; JIS uses `19/28` and `26/27`. IDs `53/59` and `4f/54`
  are also accepted. Bounds remain 1–10; pressing at a bound still marks dirty.

Threshold values come from the production tables at `0x2001b486` and
`0x2001dd7a`. Special keys retain production fixed thresholds; actuation
preview excludes editor-control keys. Normal rapid deltas clamp to at least
8, whereas rapid-editor preview uses the table's unclamped high byte.

## Optical transport

Initialization follows the recovered GPIO 8/26/29/30 sequence, 10 ms then
150 ms waits, active-low ready on P0_19, SPI3 mode 1 at 8 MHz, TX DMA0 channel
9/priority 3 and RX channel 8/priority 2. CTIMER2 supplies nominal 125 µs ticks,
below USB and its CTIMER3 PHY workaround in interrupt priority. This timer
period does not establish an 8 kHz accepted-frame rate.

The unmodified NXP SDK LPC DMA driver provides transfers. LPC5528 uses fixed
request mapping, not LPC55S69's inputmux request-enable feature. Probe, metadata
and table reads precede scanning. The command table at `0x2001ddd6` distinguishes
request from reply (mode 8 sends A4 and expects C0/A8). Layout is metadata A2
byte 7. Six three-byte-per-sensor tables are retained; modes 6 and 8 provide
external lower/upper endpoints. B6/0 starts A0 sampling; two initial responses
are skipped. C0/AC markers are counted, never treated as samples. Settling
accumulates 128 frames before the endpoint rebuild.

The scanner performs one GPIO route sequence per boot, with no automatic
route cycling or DMA/SPI restart. Pending-transfer timeout is 20 ms and ready
timeout is 125 ms. On fault, requests and channel interrupts are disabled
without waiting on DMA BUSY; buffers remain allocated. USB service continues
and keyboard/MIDI cleanup is requested. Do not infer external watchdog or
power-control behavior beyond the original register sequences.

## Keyboard integration

Raw thresholds default to press 3500 / release 3600. Down requires strictly
less than press; up requires strictly more than release. Neutral arming,
invalid/stale scan handling, GUI changes and USB reset protect against stuck
reports. CDC does not need to stay open for normal keyboard operation.

The Fn+Tab/Fn+Caps editors implement the recovered interactions above.
Actuation commits convert the original normalized threshold rules into raw
Schmitt pairs using calibrated bounds; the chosen level is stored with the
Fn-menu settings while per-key pairs stay RAM-only. Fn+Enter
selects MIDI and Fn+C starts calibration in keyboard mode, outside editors.
The [Fn menu](FN_MENU.md) supplies action hints, brightness controls and a
tail-profile RESET. All settings choices preview their names while held and execute
once on release, except RESET opens a green-Y/red-N confirmation before any
erase. Brightness K/L taps can repeat with Fn continuously held; other choices
require all keys released to rearm. Editor-internal controls retain the recovered behavior.

Normal keyboard output uses an application override layer for right-side
arrows and the green-hinted [Fn shortcuts](FN_MENU.md#keyboard-shortcuts).
This leaves the recovered tables, reference event path and MIDI role mapping
intact. Fn shortcuts are held NKRO keys rather than preview/release settings.

See [GUI operation](KEYBOARD_GUI.md), [MIDI flow](MIDI_DESIGN.md),
[calibration](CALIBRATION.md) and [lighting](TRAVEL_LIGHTING.md).

## Diagnostics and validation

Send `stream off` before text diagnostics. `scan status` reports phase,
layout, frames, errors and settling; `scan sample XX` selects a hexadecimal
raw sensor index. `keys off` suppresses output without stopping observation;
`scan stop` quarantines the scanner and is not a routine pause/restart tool.

Isolated `test` commands exercise a separate key engine without physical HID
output, GPIO operations or flash writes. For example, `test key 3b down`,
`test key 10 down`, `test key 10 up`, `test key 3b up`,
`test key 0b down`, `test key 6e down` enters actuation mode, retains it
after Fn release, selects level 10 and exits. Trace/text logging is bounded
and best-effort; use [binary scan capture](SCAN_STREAM.md) for readbacks.

Use the [build guide](BUILDING.md) to run native tests and `audit-keyboard`.
Reference-backed checks execute original initialized tables and compiled
application paths with modeled peripherals. They do not prove electrical
timing, physical scan cadence or full factory-feature equivalence. The
[validation record](CALIBRATION.md#validation-status) describes the current
build and its hardware verification limits.
