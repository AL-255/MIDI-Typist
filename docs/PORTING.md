# Port MIDI-Typist to a new keyboard or MCU

Factory settings and behavioral tuning live in
[defaults.h](../firmware/app/include/defaults.h), including a clearly marked
Huntsman optical section. New ports use the shared defaults but provide their
own acquisition rate, native calibration and hardware contracts; do not copy
Huntsman timing assumptions into another board. Host tools read the same header.
See [changing defaults](BUILDING.md#changing-defaults).

The porting boundary is the **board**, not the manufacturer's name. A port
owns acquisition, key identity, LED wiring, MCU startup, transport and storage.
The same shared C11 application supplies typing, MIDI, menus, thresholds,
velocity, calibration and effect composition.

The repository contains the complete Huntsman V3 Pro Mini/LPC5528 port,
the [M1 HAL/application libraries](MONSGEEK_M1.md), and an SDK-free reference
port. Another keyboard is not ready to flash until its
board implementation and hardware checks are complete. This guide does not
authorize overwriting an unknown bootloader or factory data.

## Contents

- [Start with the reference port](#start-with-the-reference-port)
- [1. Create a board directory and build target](#1-create-a-board-directory-and-build-target)
- [2. Describe keys independently of scan order](#2-describe-keys-independently-of-scan-order)
- [3. Acquire real analog samples](#3-acquire-real-analog-samples)
- [4. Connect the common lifecycle](#4-connect-the-common-lifecycle)
- [5. Add lighting, storage and host integration](#5-add-lighting-storage-and-host-integration)
- [6. Prove the port](#6-prove-the-port)
- [Troubleshooting a new port](#troubleshooting-a-new-port)

## Start with the reference port

Start with [the architecture](ARCHITECTURE.md), then study the small
[synthetic board](../firmware/boards/synthetic/src/synthetic_board.c) and its
[main loop](../firmware/boards/synthetic/src/main.c). They build without NXP
headers, stock firmware, a keyboard or a USB library.

With a native C compiler, CMake 3.21+, Ninja and Python 3.10+:

```sh
git clone https://github.com/AL-255/MIDI-Typist.git
cd MIDI-Typist
cmake --preset simulator
cmake --build --preset simulator
ctest --preset simulator
./build-simulator/midi_typist_sim
```

At the simulator prompt:

```text
status
set 100 40000
step 4
set 100 0
step 4
cfg all 1 3500 3600
status
quit
```

Sensor 100 produces an A press/release. `set` changes an ascending 16-bit
ADC input; `step` produces two modeled acquisitions per millisecond.
Input zero maps to released 4096. `cfg all` returns `ACK 1 1` on acceptance.
Output lines are diagnostic renderings, not USB captures; the HID printout
omits the report's reserved byte. No host key is injected. Saved calibration
exists only in the simulator process's memory.

Establish the new hardware's boot/update contract, memory map, analog range,
matrix order, scan cadence, LED protocol, power sequencing and watchdog
requirements from reliable board-specific evidence before implementing it.

## 1. Create a board directory and build target

Add `firmware/boards/<name>/board.cmake` and your board sources. Select it with
`-DMT_BOARD=<name>`; use an appropriate CMake toolchain for a cross build.
The root includes `firmware/app/CMakeLists.txt` and your board manifest.
Do not add device checks to `firmware/app` or copy its algorithms into the port.

Compile `MT_APP_SOURCES` into an object or static-library target with only
`firmware/app/include` visible. Add SDK include paths to hardware targets,
not to the application target. Apply the same ABI options to both: CPU,
instruction set, float ABI, alignment restrictions and structure layout.
The provided board manifests are complete examples of target wiring.

A typical port has this shape; only the manifest name is prescribed:

```text
firmware/boards/my_keyboard/
  board.cmake
  include/my_keyboard.h
  src/layout.c             layout/action/editor/RGB contract
  src/acquisition.c        ADC, Hall, optical or other analog transport
  src/storage.c            bounded persistent records
  src/usb.c                vendor stack, descriptors, completion ownership
  src/main.c               startup and serialized application owner
  linker/application.ld    this bootloader's application region only
```

Reusable MCU integration can live under `firmware/platform/<family>`, selected
by the board manifest. Keep vendor code/licenses under `third_party` with
pinned provenance. The existing `nxp_lpc55` integration still uses Huntsman
USB configuration and updater hooks; it is not a complete drop-in BSP for
every LPC55 board.

This CMake pattern requires your hardware sources and a `my_vendor_sdk`
target; it is not a finished hardware manifest:

```cmake
add_library(port_config INTERFACE)
target_compile_definitions(port_config INTERFACE
    MT_KEY_CAPACITY=128 MT_LIGHT_FRAME_BYTES=384 MT_HID_USAGE_MAX=0xdf)

add_library(midi_typist_app OBJECT ${MT_APP_SOURCES})
target_include_directories(midi_typist_app PUBLIC
    ${CMAKE_SOURCE_DIR}/firmware/app/include)
target_link_libraries(midi_typist_app PUBLIC port_config)

add_library(board_io STATIC
    ${MT_BOARD_DIR}/src/layout.c
    ${MT_BOARD_DIR}/src/acquisition.c
    ${MT_BOARD_DIR}/src/storage.c
    ${MT_BOARD_DIR}/src/usb.c)
target_include_directories(board_io PUBLIC
    ${MT_BOARD_DIR}/include ${CMAKE_SOURCE_DIR}/firmware/app/include)
target_link_libraries(board_io PUBLIC port_config PRIVATE my_vendor_sdk)

add_executable(my_keyboard_firmware ${MT_BOARD_DIR}/src/main.c)
target_link_libraries(my_keyboard_firmware PRIVATE midi_typist_app board_io)
```

Set matching CPU/float ABI options on all compiled objects and the final link.
Add startup/vectors, the linker script, image conversion, size checks and
bootloader-specific integrity checks. Do not expose SDK headers through
`port_config` or global `include_directories`.

After creating the port and its toolchain, configure a separate build directory:

```sh
cmake -S . -B build-my-keyboard -G Ninja \
  -DMT_BOARD=my_keyboard \
  -DCMAKE_TOOLCHAIN_FILE=cmake/my-mcu-toolchain.cmake
cmake --build build-my-keyboard
```

Add a named preset if useful. Each board/toolchain needs its own CMake cache.

Select consistent capacities for every object that includes public headers:

| Definition | Default | Huntsman |
| --- | ---: | ---: |
| `MT_KEY_CAPACITY` | 128 | 65 |
| `MT_LIGHT_FRAME_BYTES` | 3 × capacity | 204 |
| `MT_HID_USAGE_MAX` | 0xDF | 0xDF |

Counts must be nonzero and no greater than capacity; capacity must be below
255. Modifier usages E0…E7 are handled separately. Match your HID descriptor
to `KEYBOARD_NKRO_REPORT_BYTES`; do not reuse a descriptor with a different
report budget. Arrays are fixed-capacity, not allocated
per interrupt. Inspect the linker map and stack margins for your own MCU.
The allowed HID upper usage is 0x73…0xDF; current platforms use DF and
30-byte reports. No Report ID byte is included in
`keyboard_report_t`. A hardware FPU is not required by the API, but
software-float velocity calculation needs a measured execution budget.
Transport serialization owns byte order; GUI telemetry specifically requires
IEEE-754 32-bit float encoding rather than arbitrary native struct copying.

## 2. Describe keys independently of scan order

Implement the functions declared in
[keyboard_layout.h](../firmware/app/include/keyboard_layout.h):

- `keyboard_layout`: immutable description or NULL for an unknown layout ID.
- `keyboard_key_for_sensor`: scan index to opaque board-local key ID.
- `keyboard_action`: base/Fn actions for each valid key ID.
- `keyboard_lower_group`: membership in the optional lower MIDI group.
- Editor digit/step/exclusion, actuation-pair and travel-level queries.

| Query | Required result |
| --- | --- |
| `keyboard_editor_digit(profile, key)` | Level 1…10, or 0 for a non-selector |
| `keyboard_editor_step(profile, key)` | +1, -1 or 0 |
| `keyboard_editor_preview_control(profile, key)` | Whether to exclude this key from travel-bar sensing |
| `keyboard_actuation_pair(config, key, press, release)` | Normalized 8-bit press/release levels, using `config->profile` |
| `keyboard_travel_level(lower, upper, raw)` | Increasing travel 0…255, consistent with editor comparisons |

Descriptors need nonzero count and scan rate, both eleven-entry level
tables (indices 1…10 are used), and a 256-entry `keymap` indexed by physical
key ID. Layout and action pointers must remain stable.
Queries are called frequently: use bounded lookups, not I/O or allocation.

Key IDs 0 and 255 are reserved. Use unique nonzero IDs, independently of
USB HID usages. A plain keyboard action uses type 2, modifier bits in
`arg0`, and HID usage in `arg1`. Fn is identified by the layout's
`fn` field, not by its ID value or scan position. Editor actions use type
0x11 with arg0 0x70 (trigger) or 0x71 (rapid), as the example demonstrates.
Other action types are consumed/unmapped, not guessed as keyboard reports.

Every board provides `config/keymap.def`, with `KEYMAP(physical_id, hid_usage)`
entries compiled into that descriptor's immutable default keyboard map. HID
Keyboard/Keypad usages E0…E7 represent modifiers; 00 emits no keyboard key.
Keep Fn's entry zero. The application resolves physical events through this
mapping step before constructing reports; scanner and USB code do not choose
base-layer destinations. Huntsman's four right-side arrow defaults live here,
while M1 retains its ordinary modifiers and dedicated arrows.

Physical `keyboard_action` semantics remain separate: they identify Fn/menu
controls, text-label keys and MIDI roles. Fn-layer actions never read the base
keyboard map, and MIDI note mappings are independent. `keyboard_raw_t.keycode`
holds runtime overrides initialized from the board file. `keyboard_raw_map`
and `cfg key` validate usage/sensor, reject Fn, release outputs and require
neutral. Use the shared GUI dropdown and telemetry field, not a board-specific
remapping command. All output transports must consume the resulting mapped
report, including wireless paths. Preserve duplicate-destination ownership.

Implement durable board/layout-bound storage for this array alongside all
other settings. The shared journal retains Huntsman's lossless 512-byte format
and tests a separate 2048-byte M1 format; neither permits borrowing factory
pages. M1 reserves two application-tail slots and audits its SDK/SRAM writer,
and gates foreground autosave through outer-owner safety callbacks. Its
experimental application has verified flashing, recovery and live GUI telemetry;
physical pairing/wireless/cable/power qualification remains incomplete. Follow the
[mapping contract](../AGENTS.md#physical-key-mapping-contract).

Describe the board's supported editor keys even if their physical arrangement
differs. A keyboard missing a menu letter cannot show that letter or offer
that physical selector; do not silently invent a hardware key. Board-local
action mappings can assign suitable physical controls.
Present an unambiguous sensor for each control role. Missing letter keys may
shorten text previews; the renderer cannot invent missing physical keys.

## 3. Acquire real analog samples

Implement clocks, watchdog/power sequencing, ADC/ASIC scanning and readiness
using the MCU vendor's supported peripheral libraries. Supply one complete
frame per real acquisition, in the described sensor order.

Canonical values are 1…4096 and decrease on press. For an ascending 16-bit
ADC, `keyboard_sample_normalize(value,0,65535,&sample)` maps its full scale
to 4096…1. This conversion is not per-key travel calibration. Choose electrical
full-scale endpoints and retain real travel variation for calibration to learn.
Equal conversion endpoints are rejected. Bus/ADC errors must make the frame
invalid rather than becoming a valid zero-pressure sample.

The converter rounds and saturates at its endpoints. It does not detect
broken wires, ADC saturation or stale DMA data; those are acquisition faults.
Wider-than-16-bit readings need safe reduction in the board, not truncation.

| Coordinate system | Meaning |
| --- | --- |
| Native ADC and full-scale endpoints | Board-owned electrical units and polarity |
| Electrical canonical samples | ADC full scale normalized to 1…4096, used for calibration |
| Per-key lower/upper calibration | Pressed/resting electrical canonical bounds, stored without travel normalization |
| Control samples and Schmitt thresholds | 1…4096; electrical units by default, per-key normalized travel when opted in |

Startup Schmitt defaults are 3500/3600; wheels use 3800…1000 and velocity
saturates at 4,500,000 control counts/s. A layout's optional `input` policy
selects per-key travel normalization before detection, velocity, menus, wheels,
lighting and aftertouch. `NULL` retains electrical-domain controls, as on Huntsman.
The application always passes unmodified electrical samples to calibration and
stores its electrical endpoints. Send `app.raw->raw`, not board ADC frames, to
capture when these domains differ; GUI telemetry already uses control samples.
`keyboard_samples_travel` combines electrical-bound validation and conversion
for descending per-key endpoints, with the same rounding as the scalar helper.
On failure its entire output must be discarded, including any converted prefix;
the application invalidates the frame and never emits partially validated keys.

Default calibration requires rest at least 2048, a candidate no higher than half
rest and a span of at least 512. A non-NULL policy supplies `minimum_release`,
`minimum_span` and `press_drop`; zero drop retains the fractional criterion.
M1 uses travel normalization with electrical minimum release 1001 and span/drop
128. These custom choices live in `defaults.h`, not guessed physical millimetres.
Calibration changes the travel transform, not the user's threshold numbers;
release outputs and require neutral before using the newly saved transform.
If importing factory calibration, resolve its native cell index separately
from physical key IDs and convert endpoints using the same ADC full-scale
normalization as scans. Stage and validate every mapped key before publishing
any bounds. Never infer physical travel from ADC rails or repair unknown stock
pages during a read. M1's read-only loader rejects absent/invalid records. Its
separate, explicit startup fallback uses a real released frame and provisional
RAM floors; it never claims measured/saved calibration or overwrites stock pages.

Set `sample_hz` to the actual intended frame rate. Post-trigger samples span
their window's intervals, so a different frame rate changes the velocity
multiplier. Nominal configuration is not a measurement of hardware timing:
measure cadence, dropped frames and worst-case service time under polyphony.
Provide milliseconds separately as a monotonic uint32 timer; wrapping is
expected. At 2 kHz, consecutive scans can share one millisecond timestamp.
Never derive acquisitions from GUI refreshes or a wall-clock catch-up loop.
Track acquisition continuity separately from publication/capture numbering.
A missing or duplicate frame must invalidate held outputs and partial velocity
windows; require neutral before rearming. If a lossless per-key stream is
active, call `scan_stream_lost()` to emit a terminal loss marker after accepted
records drain. One-shot wake scans must not enter the periodic velocity path.

### Digital auxiliary inputs

Do not add rotary phases or buttons as artificial analog sensors. The SDK-free
`keyboard_encoder` module accepts two-bit quadrature samples and a logical button
state at a declared cadence; it returns direction and button-edge event bits,
not HID usages. Debounce tuning lives in `defaults.h`. The board owns GPIO reads,
regular sampling, ISR-to-foreground queue ownership and explicit overflow
reporting. Rebaseline after sampling gaps; do not replay movement across a
transport change or turn a held-at-start button into a new press. Keep event-to-
action mapping separate from this decoder and from USB packet construction.
`keyboard_aux` maps digital events into ordered consumer pulses using board-
supplied usages. Its send callback has copy-on-acceptance semantics; a busy
transport must return false. Cancellation requires a neutral report, not merely
clearing a software queue. Include consumer transfers in USB/radio drain, fault,
save and power handoffs. M1 demonstrates routing over its selected transport;
GUI knob remapping and saved auxiliary mappings remain unfinished.

## 4. Connect the common lifecycle

Allocate `keyboard_raw_t`, `keyboard_midi_t`, `keyboard_menu_t`,
`keyboard_calibration_t` and `keyboard_app_t` in suitable RAM. Call
`keyboard_app_init` once, then:

1. Service your USB/peripheral stack and copy/publish completed acquisitions.
2. For each complete acquisition, call `keyboard_app_frame` with canonical
   samples, layout/count, mutable lower/upper bounds, validity and milliseconds.
3. Call `keyboard_app_lights`, then submit the board framebuffer safely.
4. Call `keyboard_app_service` frequently, including between acquisitions,
   with hardware/USB health and keyboard/MIDI send callbacks.
5. Refresh the actual board watchdog and wait using your platform's mechanism.

Callbacks must not reenter application code. A USB reset/discontinuity calls
`keyboard_app_invalidate` in the owner context, even if USB reconfigures
before the next scan. Do not call it directly from an interrupt. No data for
100 ms disarms output; a known transport fault must be reported immediately.
If the layout changes, staged calibration is discarded before reading the
new frame, and calibration storage is loaded for the new layout.

The send callbacks copy or take ownership before returning true. Return false
while busy. This preserves the ordered Note On/Off/sustain queue and immutable
HID submissions without a port-specific copy of the performance engine.

### Lifecycle adapter example

This is an application adapter, not peripheral initialization. Implement the
declared `port_*` functions in your board. `port_read_frame` returns true only
for a new complete acquisition, supplies canonical samples with matching
profile/count and current bounds, and reports validity separately.
`port_discontinuity` consumes a latched reset or lost-frame event.

```c
#include "keyboard_app.h"

static keyboard_raw_t raw;
static keyboard_midi_t midi;
static keyboard_menu_t menu;
static keyboard_calibration_t calibration;
static keyboard_app_t app;
static uint16_t samples[MT_KEY_CAPACITY];
static uint16_t lower[MT_KEY_CAPACITY], upper[MT_KEY_CAPACITY];
static uint8_t rgb[LIGHTING_FRAME_SIZE];

extern const keyboard_app_ops_t port_storage_ops;
extern uint32_t port_milliseconds(void);
extern void port_service_io(void);
extern bool port_healthy(void);
extern bool port_discontinuity(void);
extern bool port_read_frame(uint16_t *samples, uint8_t *count, uint8_t *profile,
                            uint16_t *lower, uint16_t *upper, bool *valid);
extern bool port_send_keyboard(const keyboard_report_t *report);
extern bool port_send_midi(uint8_t cin, uint8_t status, uint8_t a, uint8_t b);
extern void port_offer_lights(const uint8_t *frame, unsigned bytes);
extern void port_idle(void);

void application_start(void)
{
    keyboard_app_init(&app, &raw, &midi, &menu, &calibration, &port_storage_ops);
}

void application_poll(void)
{
    port_service_io();
    uint32_t now = port_milliseconds();
    if (port_discontinuity())
        keyboard_app_invalidate(&app, now);

    uint8_t count = 0, profile = 0;
    bool valid = false;
    if (port_read_frame(samples, &count, &profile, lower, upper, &valid))
        keyboard_app_frame(&app, samples, count, profile, lower, upper,
                           valid && port_healthy(), now);

    keyboard_app_service(&app, now, port_healthy(),
                         port_send_keyboard, port_send_midi);
    keyboard_app_lights(&app, lower, upper, rgb, now);
    port_offer_lights(rgb, sizeof(rgb));
    port_idle();
}
```

Call start after board initialization and poll from the single owner.

Boards with additional system controls may bind `keyboard_app_t.system_input`,
`system_lights` and `system_context` after initialization. The input hook runs
after raw-frame processing and before the shared Fn menu; return true to consume
that frame's shared menu input. Invalidate raw output when consuming a chord,
and require neutral keys before rearming. The lighting hook runs after shared
menu rendering. Both hooks must be nonblocking and use the same owner context;
their context objects must outlive the application. The M1 transport menu is an
example, not a dependency of USB-only boards.

For wireless backends, enforce the MIDI permission at input processing, not
just by hiding a menu hint: restored settings must not enable MIDI on a transport
that cannot carry it. A transport change must release the old host's keys/notes,
finish the transport's documented neutral-output handoff, and establish a
neutral baseline on the new host. Document exactly what the protocol can confirm;
when it provides no host-delivery ACK, do not label local completion as one.
Radio/power handshakes belong to the board;
never perform them in the shared application or its lighting renderer.
Translate the common mapped report at the transport boundary; do not remap
physical keys again. The M1 radio adapter illustrates retaining per-report
ownership when a peer splits keys between usage slots and a smaller bitmap.
Bound every peer format independently of the USB descriptor. Queue acceptance,
DMA completion, a peer mode reply and host delivery are separate facts: do not
claim a local-idle predicate proves remote host receipt.
Keep GUI/control sessions independent from active keyboard host ownership when
a board supports wired configuration alongside wireless typing. A GUI USB reset
must not release keys on the radio host. Validate a transport adapter's selection
against actual endpoint readiness or peer mode confirmation, latch its handoff boundary through
asynchronous reconfiguration, and stop output if an attempted switch becomes
ambiguous. The M1 foreground binding accepts the initial mode and optional
transport callbacks explicitly. The runtime M1 owner waits for neutral SPI
ownership before issuing the reference mode command, confirms its status reply,
and leaves USB control available. Report eligibility is checked separately from
mode selection so an unpaired slot can still switch back to USB. Telemetry carries
portable transport/ready/switching/pairing fields, not raw radio mode bytes.
Pairing must be an explicit user action, separate from ordinary slot selection.
M1's optional nonblocking `pair` callback selects the target and issues one request
after the same neutral handoff, including when targeting the current slot. Its
completion means local command completion plus fresh matching mode status, not
a paired host. An absent callback disables the long-hold gesture. Do not infer
failure merely because the selected transport did not change; use the transition
result. Reconnection still requires a neutral report and released physical keys.
For GUI power readback, register `midi_control_power_handler` with a nonblocking
cached-state provider and enable `Board.power_status` in the GUI board catalog.
Populate the portable `keyboard_power_status_t`; the shared service returns its
versioned [ACK payload](TELEMETRY.md#power-status). Leave the provider absent on
boards without power telemetry. Do not relabel unverified charger pins as
charging/full, put hardware sampling inside the command handler, or reset the
key stream to answer a read-only status query.
Sleep commands need the same ownership discipline. Process activity/cable
cancellation before submitting a queued power command; after transmission has
started, require the board's real restoration path rather than just clearing a
software flag. A stronger shutdown request must invalidate completion of an
earlier retention-only command. Keep battery metadata latest-only without
letting it overwrite in-flight packets or delay accepted key releases.
When a power transition repurposes an input as an output, give it explicit
ownership: invalidate its normal telemetry and prevent other HAL initialization
from reclaiming it until restoration succeeds. Preserve unowned pin latches
and debug pins. Do not translate sequential wake-pin reads into synthetic key
events without the platform's filtering and restoration logic.
For cold startup, model each power-source branch explicitly and bound scan
completion. Discard warmup samples instead of reporting them as key or velocity
events. Start settling delays from fresh time readings after blocking ADC/clock
initialization or sleep; do not derive a microsecond clock from a wrapping
millisecond counter. Keep failed clock restoration terminal with ordinary IRQs
disabled, and require explicit recovery rather than automatic rail cycling.
An interrupt-counted tick can lose time during flash. Use a free-running counter
or reconcile against a measured independent source, carrying fractional units
and handling microsecond/millisecond wrap independently. M1's
[foreground timebase](MONSGEEK_M1.md#foreground-timebase) owns 32-bit TMR2;
it runs through flash but must be suspended before clock changes/deep sleep.
Resume requires measured elapsed time, never an assumed requested sleep delay.
M1's [RTC bridge](MONSGEEK_M1.md#measured-rtc-time-bridge) qualifies the low-speed
counter against the awake timer, uses coherent calendar/subsecond reads and
refreshes shadow registers after wake. Start TMR2 before its battery startup;
the startup owner performs qualification and uses the timed sleep wrapper.
Bound rate qualification and resume, preserve fractional conversion carry,
and state the resolution/drift limits instead of promising wall-clock accuracy.
The [M1 startup HAL](MONSGEEK_M1.md#clock-rails-and-remaining-integration)
implements these contracts in the experimental application.
Its [cold application handoff](MONSGEEK_M1.md#cold-application-handoff) attaches
USB before acquisition, retains the first complete real scan, then pauses while
loading the profile and binding the application. Only then does scanning resume.
Radio startup uses a fresh timestamp after any blocking USB work.
Keep cold ownership separate from runtime reconnect/wake: initialization success
does not establish host readiness or permission to abandon an existing host.
The [M1 runtime controller](MONSGEEK_M1.md#runtime-battery-sleepwake) drains and
parks the application before taking sole peripheral ownership. It preserves RAM
settings through sleep, uses fresh post-WFI timestamps, and requires fresh radio
mode confirmation and neutral input before rearming. A new board must provide
its own verified rail/GPIO and source-change sequence; do not transplant M1 pin
writes. Its [awake source owner](MONSGEEK_M1.md#awake-usb-power-source-transitions)
distinguishes an aborted USB endpoint from a delivered neutral report, retains
wireless selection on power arrival, pauses all bus masters before PHY setup,
and preserves application RAM through re-enumeration. Cable arrival during sleep
must cancel a not-yet-submitted radio command or finish its actual completion and
restore the peer before USB attachment. Abort wake-scan DMA before cycling rails;
never reinterpret an in-flight transaction as cancelled. M1 models these paths,
but physical cable/sleep operation and overlapping Fn transport changes are not
qualified. Do not present state-machine checks as physical hot-plug validation.
Never write more than capacity into the arrays. Establish fallback bounds
at layout discovery; preserve successful calibration loads instead of
overwriting them on every frame. The state and ops table must outlive all calls.

The LED offer function copies into board-owned transfer memory or a latest-only
desired frame; it must not retain the scratch pointer across another poll.
Idle must not hide a ready frame or delay fault cleanup. Handle known faults
immediately; the 100 ms stale guard is a fallback, not an acceptable buffer age.

An RTOS port drives these same calls from one owner task. ISRs signal
completion through the kernel's ISR-safe facilities rather than calling the
application. Budget priorities, stacks, buffer ownership and maximum sleep.
FreeRTOS is optional and not linked by the current ports; see
[the scheduling decision](SCHEDULING.md).

## 5. Add lighting, storage and host integration

`keyboard_light_set` writes RGB intensities into the supplied byte framebuffer
for one sensor. Zero means off, 255 maximum. An unlit key may be ignored by
the setter; its scan/key behavior still works. If LEDs require gamma conversion,
bit packing or command headers, encode those at hardware submission, after the
application's linear intensity/brightness processing. Preserve a transfer
snapshot until the peripheral has finished using it.
The buffer must contain intensity bytes only: shared code clears and scales
it. Padding is acceptable; controller headers and checksums are not.
`keyboard_app_lights` includes the menu brightness pass and feedback
exceptions, so do not apply global brightness again in the board.
The board owns refresh cadence, gamma/protocol encoding and explicit light-off.

Provide calibration load/save/profile-clear callbacks as appropriate. The
callbacks own physical pages, checksums, rollback, identity and power-failure
handling. They must not modify unrelated bootloader, serial, security or
factory data. No callback means unavailable storage: initialization masks the
calibration/RESET Fn options when `save_calibration`/`clear_profile` is absent,
and calibration commands reject entry without a save callback. Other board
capability exclusions use `keyboard_menu_t.disabled_options`, with bit
`MENU_* - 1`; disabled options neither execute nor receive menu hints.
Never claim a persistent save without verified storage. The simulator saves only in
its process RAM. Huntsman's writer is a board-specific example, not a universal
flash layout. The reusable journal is described below.
Read-only stored calibration may set telemetry's saved flag while leaving the
calibration-supported flag clear. It must not advertise a writable profile or
invent a journal generation; the GUI renders this combination as read-only.

| `keyboard_app_ops_t` callback | Board responsibility |
| --- | --- |
| `load_calibration(profile, count, lo, hi)` | Validate identity, complete bounds and integrity before changing arrays; false means no valid load |
| `save_calibration(cal)` | Return `KEYBOARD_SAVE_COMPLETE` only after persisting and verifying all staged endpoints; `KEYBOARD_SAVE_FAILED` discards the candidate; `KEYBOARD_SAVE_DEFER` leaves storage unchanged and retains the candidate for retry |
| `clear_profile()` | Clear only owned custom records after an explicit Fn-menu confirmation or accepted MIDI SysEx `cfg clean`; false means clearing was not verified |
| `reset_sensors(profile)` | Rebuild board-owned fallback state without an unrelated USB reboot or destructive peripheral restart |
| `log(message)` | Optional bounded diagnostics, not a blocking serial write |

`keyboard_app_reset_profile` releases outputs and cancels unfinished MIDI
strikes, then defers defaults until a fresh neutral scan. This also works when
keyboard output is disabled: do not use output arming as the reset-completion
condition. The shared MIDI SysEx parser requires a valid scan younger than 100 ms for
`cfg clean`; its ACK confirms the erase, not that held keys have been released.

Callbacks are synchronous with no context argument; the board supplies its
single-owner storage context. A deferred calibration save is retried on fresh
frames in `CAL_SAVE`, without refreshing its inactivity deadline. Cancellation,
invalid input, stale scans and the normal inactivity timeout discard the pending
candidate without changing active bounds. Do not invalidate the application or
mutate its calibration state inside the save callback: it still owns that
candidate. Publish any acquisition gap after `keyboard_app_frame` returns,
before accepting new samples. A queued write is not a completed save.
Loading is attempted once after a valid layout
frame, not continuously. The board owns storage generation/error telemetry. The shared application
exposes the chosen Fn-menu levels and flags through `keyboard_menu_t`,
`keyboard_midi_t` and `keyboard_raw_t`; the board owns their persistence.
The SDK-free `firmware/services/src/device_store.c` journal requires the board
build to define `MT_STORE_PAGE_SIZE`, a four-character `MT_STORE_MAGIC`, and
`MT_STORE_INVALID_READ` (the recoverable invalid-content error, or zero for none).
Propagate these definitions to every journal caller; include
`firmware/services/include`. Size is one complete slot, including its CRC.
Select a distinct format identity for each board/layout namespace and validate
capacity for every layout with `tests/test_device_store.c`. The callbacks take
only slot 0 or 1, never caller-supplied addresses. The writer must own both slots,
blank-verify erasure and program the whole record; reads and erase verification
must report controller faults. No SDK headers or physical addresses belong in
the journal. See [record formats and limits](DEVICE_CONFIG_STORAGE.md).
For a board that must pause hardware before writing, use `device_store_poll`
to track pending/debounce status without making a write attempt. Defer until
power and output ownership are proven, then call `device_store_update`; a busy
deferral is not a controller fault. Preserve visible capture gaps and rearm only
on fresh neutral acquisitions after resume.
Distinguish unchanged deferral from fatal failure to acquire quiescence. M1's
save callback returns `M1_SAVE_DEFER`, `M1_SAVE_READY` or `M1_SAVE_FAULT`; only
READY permits a write and must be paired with end, including on write errors.
Its `m1_save_ops()` implementation retains links/rails and masks interrupts
through the scanner pause/write/resume boundary. Local drain during a retained
link is not proof of host delivery for a transport switch or radio power-down.
M1's scanner pause/resume contract discards partial/unread acquisitions and
battery samples without recalibrating ADC or changing rails. The outer owner
must retain clocks/power, coordinate the other peripherals and expose the gap;
unchanged complete-frame sequence counters are not evidence of continuous time.
If a flash operation stalls instruction fetch, keep the transaction, SDK
callees, literal pools and unmaskable exception path in RAM. Prove the complete
load-image boundary excludes the slots, including RAM initializers. M1's
[writer contract](DEVICE_CONFIG_STORAGE.md#m1-application-tail-backend) illustrates
these checks; its synthetic ELF is not an application linker/startup template.

Huntsman's `device_store` loads whole-profile snapshots through the calibration
callback, applies settings immediately after `keyboard_app_frame` and before
output service, then checks committed changes every 20 ms. Saving waits for
250 ms stability and neutral input with no editor/preview/calibration active.
Every snapshot preserves calibration. Report pending/saved/error separately
from RAM command ACKs. Restore outputs as neutral, never as sounding notes.
A new port must choose its own schema, geometry and safe write scheduling.

Prove page ownership, execution/interrupt safety during erase, watchdog
behavior, timeouts and power-loss recovery on the actual MCU. FF bytes alone
do not establish ownership. Huntsman's RAM-executing flash adapter is not
safe by implication for an MCU executing from the bank being erased.

USB exposes NKRO HID and two-cable USB-MIDI through the platform's stack:
performance on cable 0, bidirectional GUI SysEx on cable 1. Preserve the
separate control port, envelope framing/CRC, bounded command mailbox and
main-context dispatch. The portable codec is `midi_sysex.c`. Shared `firmware/services` owns session,
lease, command mailbox and stream queues. Initialize `midi_control` with a
persistent `midi_control_port_t`: monotonic millisecond clock, USB-ready query,
copy-on-accept USB event writer, interrupt lock and prior-state restore.
Only framing runs in the receive ISR; call service from the application owner.
On disconnect/reset, notify both services and invalidate the application.
Endpoint ownership and descriptors remain platform responsibilities.
The Huntsman additionally retains its updater HID at interface 3.
Feed newline-stripped configuration commands to `keyboard_app_command`;
it implements get/set/all/enable/MIDI/velocity/clean/calibrate/cancel validation and ACK
semantics. Use `keyboard_telemetry_encode` for count-aware MTG4 snapshots; supply
board fault/storage status with `keyboard_telemetry_status_t`. Feed its returned
length to `scan_stream_gui_push`. Allocate using `MT_GUI_SIZE` for the selected
capacity/HID report, not the protocol maximum. Board diagnostics and the MCU's
firmware update path remain in the port. Never copy the Huntsman reset cookie or flash
addresses to an unrelated bootloader.

The MIDI callback receives CIN, status and two data bytes. USB-MIDI 1.0
assembles cable 0/CIN plus those bytes; a UART MIDI adapter omits CIN and
budgets its own bandwidth. The finite output queue requires prompt service.
HID callbacks receive a transient report pointer and must copy before return.
Invalidate on disconnect even if the scanner remains healthy.

Choose appropriate product VID/PID/strings and a compatible bootloader
protocol; do not advertise Huntsman's updater interface without implementing
its reset/image contract. The
[Huntsman flasher](https://github.com/AL-255/Huntsman-V3-Pro-Mini-Flasher)
is a separate board-specific tool, not a universal firmware installer.

Check ELF program headers as well as section/symbol addresses: a default load
segment can include ELF metadata and extend backwards into the bootloader.
M1's [development link](MONSGEEK_M1.md#development-elf-and-reset-entry) uses
explicit segments, includes SRAM-code/data load addresses in the flash budget,
and executes reset-copy tests from poisoned RAM. A correct link alone is not
permission to enable flashing or claim complete runtime behavior.

The configuration view selects physical geometry from `keyboard_boards.py` by
build target, and binds host profiles to that target/layout. Register the allowed
layout/count pairs, HID report length and declared sample rate; all are checked
before accepting telemetry. MTG4 supports up to 128 sensors, with no fixed
Huntsman-sized bitmaps. The GUI uses the same declared rate for capture velocity. The [flashing tab](DEVICE_FLASHING.md) has a model-independent view:
implement and register a separate adapter for each product, with its discovery,
image checks, supported transitions and protected write boundary. Do not reuse
Huntsman addresses for another platform. Preserve the shared SysEx/MTG4 contract;
a different host presentation can call the same common configuration command engine.
Adapters declare `inspection_modes` separately from flash actions; identity-only
support must also reject flashing in the privileged worker, not just hide a
button. The [M1 adapter](MONSGEEK_M1.md) permits experimental conversion only
after factory ID2949 verification, or custom reflashing after querying the build
target on a MIDI port bound via ALSA/sysfs to the selected USB device. Friendly
MIDI names and another session's cached build are not identity proof. Adapters
declare `control_inspection_modes` to close configuration before owning the
control port. Discard torn sysfs enumeration snapshots during reset. A shared bootloader PID cannot establish
model identity or persistent recovery state. A transfer verdict must not be
reported as proof that the new application enumerated or functions correctly.
Where clocks and power allow, keep the control channel available after a
startup failure instead of making USB depend on valid calibration or scanning.
The reserved `Boot failed: ` and `Runtime failed: ` SysEx log prefixes make the GUI reject configuration
with an actionable error; never substitute fabricated sensor data for diagnostics.
Pass bounded, NUL-terminated lines without CR/LF to `keyboard_app_command`.
False means another handler may inspect the line; true means it was consumed,
not necessarily accepted. Inspect the ACK ID/result and serialize requests.
Unparseable IDs leave the previous ACK unchanged. The parser itself neither
emits text replies nor serializes GUI telemetry; provide settings readback in the port.

## 6. Prove the port

The root CMake build injects platform-neutral Git metadata into C targets and
orders generation before compilation. Use `keyboard_build.h`'s
`MT_BUILD_INFO` and `MT_GIT_REPLY` for device identity; do not duplicate a
commit hash in board code. A new control transport should expose a read-only
provenance query and include the identity in its handshake. See
[build provenance](BUILDING.md#build-provenance) and the
[SysEx contract](TELEMETRY.md#text-replies).

Build/test the shared code without another board's include directories or
source files. Adapt the synthetic test to your descriptor and test:

- Normal and Fn HID output, duplicates, release order and USB backpressure.
- Every sensor independently, including indices above 64 where present.
- Velocity cadence, overlap/pop filtering, Note Off and sustain ordering.
- Root/scale and row filters, controls and LED positions.
- Calibration parallel holds, cancel, save failures, neutral arming and reset.
- Invalid samples, short frames, layout changes, stale input and USB reset.
- All protected flash boundaries and the computer-initiated updater path.

Then validate on hardware with a recoverable application-only update and
readback. A simulator or register model does not prove pin routing, electrical
power behavior, optical timing or real USB signal integrity.

### Minimum acceptance checklist

- [ ] A fresh checkout builds without private extraction or device data.
- [ ] Shared sources compile using only application headers and standard C.
- [ ] Map, vectors, application bounds and image format match this bootloader.
- [ ] Every physical key has a unique sensor/ID and tested action/LED position.
- [ ] Real resting/pressed values allow neutral arming, wheels and calibration.
- [ ] Measured acquisition and worst-case processing fit the frame budget.
- [ ] HID, notes, sustain and cleanup survive backpressure and reconnect.
- [ ] Menu previews cancel promptly and release gates every deferred action.
- [ ] An interrupted save cannot replace a valid record with partial data.
- [ ] Application readback matches and protected data remains unchanged.
- [ ] A board user guide lists controls, supported features and test limits.

Keep native tests, register models and physical evidence distinct. Huntsman's
[validation status](VALIDATION.md) is not evidence for a
different board. The synthetic port has no physical USB, flash or power circuit.

## Troubleshooting a new port

| Symptom | Check at the board boundary |
| --- | --- |
| Keys never arm | Canonical polarity/range, all idle values above release, readiness |
| Velocity is wrong | Distinct acquisitions, declared versus actual rate, normalization range |
| Menu selects the wrong key | Base HID action versus sensor index versus opaque ID |
| International keys disappear | HID usage limit, descriptor and report buffer size |
| Colors move between keys | RGB mapping and immutable transfer snapshots |
| Notes or sustain stick | Accepted-event ownership, servicing, lost reset events, overflow |
| Calibration never completes | Rest/half-rest/span requirements, stable holds, storage result |
| Only hardware fails | Power/watchdog/clock/ADC/USB evidence; do not conceal faults with blind retries |

Do not debug a new port by repeatedly erasing unrelated regions or replacing
known-good bootloader data. Establish a board-appropriate recovery path first.
