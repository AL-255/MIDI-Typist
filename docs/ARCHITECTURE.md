# MIDI-Typist architecture

MIDI-Typist is a portable C11 analog-keyboard application with separately
selected board ports. A port is a firmware build, not a universal binary:
MCU startup, USB descriptors, pins, scan transport, LEDs, flash ownership and
updater entry must match the actual hardware.

The supported physical port is the Razer Huntsman V3 Pro Mini/LPC5528.
The desktop synthetic port exercises a different layout and acquisition model;
it is not evidence that another commercial keyboard is ready to flash.
Use the [porting guide](PORTING.md) for a build-manifest pattern, a lifecycle
adapter example, callback contracts and the new-board acceptance checklist.

## Source ownership

```text
CMakeLists.txt                    select MT_BOARD
firmware/
  app/
    include/                     public contracts and state
    src/                         application behavior, no SDK/device headers
  boards/
    huntsman_v3_pro_mini/
      board.cmake                build, linker and memory budgets
      include/ + src/            optical ASIC, stock compatibility, storage
      linker/                    existing application-only RAM/flash contract
    synthetic/
      board.cmake                native build without NXP or extraction
      include/ + src/            104-key/7-key reference port and CLI simulator
  platform/
    nxp_lpc55/                   NXP USB/CDC integration and silicon workarounds
third_party/nxp/                 unmodified pinned official SDK components
tools/                          host tools and offline hardware audits
tests/                          native behavior and portability tests
```

Both boards compile the same `MT_APP_SOURCES`. The `midi_typist_app`
object target sees only `firmware/app/include`, standard C headers and its
compile-time capacity definitions. A CTest architecture check rejects leaked
board headers and hardware symbols. The SDK-free reference build independently
links every shared application source against the synthetic board.

The NXP integration is selected by the Huntsman board, not by the application.
Its existing USB descriptors, updater protocol, interrupt ownership and MCU
initialization sequence remain hardware-specific. A different MCU uses its
own vendor-supported stack; it need not implement an NXP compatibility shim.

## Application ownership

| Module | Responsibility |
| --- | --- |
| `keyboard_app` | Shared lifecycle, mode/menu dispatch, calibration orchestration, output retries, stale/fault cleanup |
| `keyboard_command` | Common `cfg` parsing, validation and acknowledgments |
| `keyboard_raw` | Per-key Schmitt state and independent bottom-out velocity windows |
| `keyboard_engine`, `keyboard_config` | NKRO ownership, Fn routing and editor state transitions |
| `keyboard_midi`, `midi_music` | Notes, velocity, pressure, wheels, sustain, octave and scale filtering |
| `keyboard_menu`, `keyboard_text` | Release-triggered menus and interruptible text lighting |
| `keyboard_calibration` | Parallel calibration holds, endpoints, timeout and feedback |
| `lighting_travel` | Travel/pressure normalization and inverse lighting |
| `keyboard_sample` | Optional ascending/descending ADC conversion to canonical units |

Application state is supplied by the board as separate allocations. This lets
the Huntsman keep calibration registers in its application-image RAM while
other state remains in SRAMX, without device-specific section attributes in
shared code. There is no heap allocation in the application.

## Board contracts

[keyboard_layout.h](../firmware/app/include/keyboard_layout.h) describes an
immutable layout: key count, opaque key IDs, Fn/editor keys, scan frequency,
editor level tables and the compact-navigation policy. The board translates
sensor indices into its key IDs and HID actions. The application never infers
Huntsman row membership from an ID range. Layout-specific editor exclusions,
threshold conversion and lower-row membership are board queries.

Samples use **1…4096, decreasing with travel**. These are application units,
not a required ADC resolution or signal polarity. Boards with a different ADC
range can use `keyboard_sample_normalize`; a board already producing canonical
units can pass them through unchanged. Invalid frames are reported separately.
Velocity uses the layout's acquisition rate; Huntsman retains its nominal
8000 Hz assumption and 0…4500000 counts/s normalization.

RGB is a board-owned byte framebuffer containing linear intensity components.
The application calls `keyboard_light_set` by sensor index; it knows neither
I2C addresses nor channel order. Clearing/scaling the byte buffer is portable.
The Huntsman setter retains its recovered channel map; the reference board
uses contiguous RGB triplets. Hardware framing and transfer snapshots belong
to the board.
The application composes global brightness and menu/calibration exceptions
before submission. Board code must not apply that global scale a second time.

[keyboard_app.h](../firmware/app/include/keyboard_app.h) defines the lifecycle
and storage hooks. Storage callbacks own erase sizes, slot addresses, record
formats, device identity and bounds validation. The shared application requests
load/save/clear operations; it cannot erase a flash address. Huntsman's MTP1 whole-profile
serializer and two-page journal remain entirely inside its board directory.

## Scheduling and outputs

```text
board acquisition / DMA completion
  → canonical complete frame + layout + valid/ready state
  → keyboard_app_frame
      → Schmitt + velocity → menu action → calibration → MIDI state
  → keyboard_app_lights → board LED transfer

owner loop/task
  → board USB/peripheral service
  → keyboard_app_service
      → stale/fault guard → ordered MIDI events → HID retry/heartbeat
  → board watchdog refresh / wait
```

Every acquisition is passed once. Neither repeating the most recent scan nor
silently discarding a scan preserves velocity semantics. The board invalidates
the application on missed/invalid acquisitions or USB reset and supplies fresh
neutral input before resuming. Main-loop service is not the acquisition clock.

All application calls are serialized by one owner. USB/ADC ISRs only publish
board-owned events or buffers. Send callbacks return true only after copying
or taking immutable ownership; busy output is retried. A new board must
implement the transport completion rules, not another note/keyboard queue.
See [scheduling and FreeRTOS](SCHEDULING.md).

## Compatibility boundary

The Huntsman port retains the 16-byte NKRO report, MIDI channel/packet encoding,
GUI telemetry, HKS1/HKL1/HBD1 streams, CDC commands, updater entry, calibration
record format and flash limits. Its existing host tools remain board-specific:
the GUI's physical drawing is ANSI Huntsman, not an inferred layout for an
unknown keyboard. A port's diagnostic/telemetry framing is part of its host
integration; shared `cfg` behavior is available regardless of transport.

Generic builds default to 128 sensor slots and a wider NKRO usage bitmap.
The Huntsman selects 65 slots, its 204-byte LED frame and the existing HID
usage limit. Up to 254 sensors fit the current opaque 8-bit ID/count interface;
larger devices require a deliberate interface extension. USB descriptors must
always match the selected report size.
