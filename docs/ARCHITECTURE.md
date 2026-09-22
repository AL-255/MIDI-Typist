# MIDI-Typist architecture

MIDI-Typist is a portable C11 analog-keyboard application with separately
selected board ports. A port is a firmware build, not a universal binary:
MCU startup, USB descriptors, pins, scan transport, LEDs, flash ownership and
updater entry must match the actual hardware.

The complete supported physical port is the Razer Huntsman V3 Pro Mini/LPC5528.
The [M1 backend](MONSGEEK_M1.md) provides HAL/application libraries, an
experimental application image, a guarded factory updater and an 82-key GUI
preview. Its transfer is verified on hardware, but application USB startup
is not working; it is not a supported daily-use keyboard port.
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
    monsgeek_m1_v5_tmr/
      board.cmake                native tests, ARM libraries and development ELF
      include/ + src/            82-key wiring/layout, ADC scanner and SPI LED HALs
      linker/                    application-only link and SRAM flash code
  services/
    include/ + src/              SysEx sessions, scan queues and profile journal
  platform/
    nxp_lpc55/                   NXP USB integration and silicon workarounds
    at32f405/                    official Artery driver configuration
third_party/nxp/                 unmodified pinned official SDK components
third_party/artery/              pinned official AT32F402/405 SDK submodule
tools/                          host tools and offline hardware audits
tests/                          native behavior and portability tests
```

All board targets compile the same `MT_APP_SOURCES`. The `midi_typist_app`
target sees only `firmware/app/include`, standard C headers and its
compile-time capacity definitions. A CTest architecture check rejects leaked
board headers and hardware symbols. The SDK-free reference build independently
links every shared application source against the synthetic board.

The NXP integration is selected by the Huntsman board, not by the application.
Its existing USB descriptors, updater protocol, interrupt ownership and MCU
initialization sequence remain hardware-specific. A different MCU uses its
own vendor-supported stack; it need not implement an NXP compatibility shim.

The GUI's `keyboard_boards.py` registry selects physical geometry and profile
identity by the firmware build target. It reads the M1 key definitions directly
from the same table compiled by C. A key count is a validation field, not a board
identifier. Flashing adapters remain separate from live configuration backends;
read-only M1 discovery does not imply a working custom USB application.

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
the Huntsman keep calibration registers and the shared app/readback cache in
its application-image RAM while other state remains in SRAMX, without device-specific section attributes in
shared code. There is no heap allocation in the application.

## Board contracts

[defaults.h](../firmware/app/include/defaults.h) owns factory settings and
behavioral tuning shared by firmware and host tools. Saved profiles take
precedence; hardware/protocol constants remain with their owning modules.
See [changing defaults](BUILDING.md#changing-defaults) for editing and validation.

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
load/save/clear operations; it cannot erase a flash address. The SDK-free
`services/device_store` serializer and two-slot journal use board-selected
record sizes and identities. Huntsman's writer and physical addresses stay in
its board directory. M1's foreground restores the journal and schedules autosave
through an explicit outer-owner pause/power gate; its SDK/SRAM writer is tested
separately against a controller model.

## Scheduling and outputs

```text
board acquisition / DMA completion
  → canonical complete frame + layout + valid/ready state
  → keyboard_app_frame
      → Schmitt + velocity → physical/Fn routing → keyboard mapping → report
      → menu action → calibration → MIDI state
  → keyboard_app_lights → board LED transfer

owner loop/task
  → board USB/peripheral service
  → keyboard_app_service
      → stale/fault guard → ordered MIDI events → HID retry/heartbeat
  → board watchdog refresh / wait
```

During calibration, the shared application takes an observation-only path:
every sample is validated, normalized for display and copied to readback, but
key edges and velocity fits are suppressed. Electrical samples still feed each
key's independent calibration hold. Output stays disarmed until calibration
finishes or aborts and a fresh neutral frame is observed.
Held application-menu previews likewise skip unusable key events/velocity fits
while output is disarmed; their menu logic still observes every fresh sample.

Every acquisition is passed once. Neither repeating the most recent scan nor
silently discarding a scan preserves velocity semantics. The board invalidates
the application on missed/invalid acquisitions or USB reset and supplies fresh
neutral input before resuming. Main-loop service is not the acquisition clock.

All application calls are serialized by one owner. USB/ADC ISRs only publish
board-owned events or buffers. Send callbacks return true only after copying
or taking immutable ownership; busy output is retried. A new board must
implement the transport completion rules, not another note/keyboard queue.
See [scheduling and FreeRTOS](SCHEDULING.md).

Each board's `config/keymap.def` supplies default physical-key-to-keycode
mapping through its layout descriptor. It changes normal keyboard destinations,
not physical labels, sensor identity, fixed Fn actions or MIDI note assignments.
The raw-key state owns one runtime usage per sensor. `cfg key` validates edits,
releases output and requires neutral before rearming. The engine records outputs
by physical source and rebuilds their union, so duplicate destinations release
only when the last source releases. GUI dropdowns verify telemetry readback;
Huntsman's whole-profile journal saves these mappings alongside calibration.

## Compatibility boundary

The Huntsman port uses a 30-byte NKRO report, MIDI channel/packet encoding,
GUI telemetry, MTG4/HKL1/HBD1 streams, MIDI SysEx commands, updater entry, calibration
record format and flash limits. Shared MTG4 telemetry and SysEx sessions serve
both board capacities; the GUI selects verified board geometry instead of
inferring it from sensor count. Unknown targets are rejected. Firmware-update
and flash-dump operations remain board-specific.

Generic builds default to 128 sensor slots and a wider NKRO usage bitmap.
The Huntsman selects 65 slots, its 204-byte LED frame and HID usages through DF
plus modifiers E0…E7. Up to 254 sensors fit the current opaque 8-bit ID/count interface;
larger devices require a deliberate interface extension. MTG4 telemetry supports
up to 128 sensors. USB descriptors must
always match the selected report size.
