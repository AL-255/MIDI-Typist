# Building and testing MIDI-Typist from a fresh checkout

Use `huntsman` for the complete physical keyboard or `simulator` for the
SDK-free desktop reference. The Huntsman build uses Arm GNU and the pinned NXP
components; the M1 HAL libraries use Arm GNU and the pinned Artery submodule.
For another keyboard/MCU, follow the
[porting guide](PORTING.md), including its board-manifest and lifecycle examples.

## Prerequisites

The build uses a native C compiler for logic tests and Arm GNU bare-metal GCC
for the target. Tested target compiler: **14.2.1**. Put `arm-none-eabi-gcc`,
`arm-none-eabi-ar`, `arm-none-eabi-objcopy`, `arm-none-eabi-size` and their runtime
libraries on PATH. Also install CMake 3.21+, Ninja and Python 3.10+. The project
uses C11 and builds warnings as errors for application/logic code.

On Debian-family Linux, packages typically needed are `build-essential`,
`cmake`, `ninja-build`, `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`,
`python3`, `python3-tk` and `python3-venv`. Distribution compiler versions may
differ from the tested version. Review package installation normally; do not
put passwords or machine-specific credentials in build scripts.

The Huntsman firmware uses unmodified, vendored official NXP SDK source subsets. Their
versions came from the MCUXpresso Installer 26.06.123 catalog installed at
`/home/yukidama/MCUXpressoInstaller` on the development machine. A fresh checkout
does **not** require that absolute path or reinstalling MCUXpresso: CMake uses
`third_party/nxp`, not files outside the repository. Exact revisions and licenses
are recorded in [ORIGINS](../third_party/ORIGINS.md).

## Fresh build

```sh
git clone git@github.com:AL-255/MIDI-Typist.git
cd MIDI-Typist
arm-none-eabi-gcc --version
cmake --version
ninja --version
python3 --version

cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests

cmake --preset huntsman
cmake --build --preset huntsman
```

The ARM toolchain file is `cmake/arm-none-eabi.cmake`; no IDE-generated project
is necessary. `huntsman` builds the complete current application; no partial
or historical firmware presets are supported.

The SDK-free desktop port uses the same application sources with a synthetic
104-key layout and 2 kHz ascending 16-bit input:

```sh
cmake --preset simulator
cmake --build --preset simulator
ctest --preset simulator
./build-simulator/midi_typist_sim
```

Its commands include `status`, `set SENSOR ADC`, `step MILLISECONDS`,
the shared `cfg` commands, and `quit`. Time advances only through `step`;
HID/MIDI packets print to stdout, and calibration storage is process RAM.
It never opens USB devices or flashes hardware. See the
[porting guide](PORTING.md) for selecting another `MT_BOARD`.

Artifacts in `build-huntsman`:

- `huntsman_firmware.elf`: debug symbols, linked ARM instructions and memory map.
- `huntsman_firmware.hex`: addressed Intel HEX.
- `huntsman_firmware.bin`: exactly 131072 bytes, with FF padding through the
  original updater-image boundary.

The linker reserves 127 KiB for code/initialized data and 1 KiB for an unused
application-image configuration reservation. These are RAM execution-image
addresses; controller readback established physical application base 0x8000.
Calibration's 1324-byte per-key state is writable RAM inside the image region,
explicitly initialized at startup. Persistent records instead use only the
verified FF pages 0x78000 and 0x78200. Post-build checks enforce the reservation,
blank application-image reservation, reset/stack bounds, USB VID/PID, interface order,
endpoint layout, strings and HID descriptors. They do not validate an actual
bootloader's flash mapping or authorize flashing.

The complete application builds without the updater, extraction or private
device data. See [validation status](VALIDATION.md)
for the hardware boundary. Newlib may emit linker warnings about unimplemented
`_close`, `_lseek`, `_read` and `_write`; those functions are absent from the
final linked image after garbage collection. MIDI SysEx debug output uses the
application's USB transport, not libc file I/O.

## MonsGeek M1 development build

These commands compile the real shared application with the M1's 82-key layout,
test its board callbacks natively, and compile its HALs and development ELF for
Cortex-M4. They never access a keyboard. The application `.bin` is an
[experimental M1 recovery contract](DEVICE_FLASHING.md#monsgeek-m1-experimental-conversion),
not a working plug-and-play release.

```sh
git submodule update --init third_party/artery
cmake -S . -B build-m1-host -G Ninja -DMT_BOARD=monsgeek_m1_v5_tmr -DCMAKE_BUILD_TYPE=Debug
cmake --build build-m1-host
ctest --test-dir build-m1-host --output-on-failure
cmake -S . -B build-m1-hal -G Ninja -DMT_BOARD=monsgeek_m1_v5_tmr -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-m1-hal
python tools/test_keyboard_boards.py
python tools/test_m1_hal_arm.py build-m1-hal/m1_hal_audit.elf
python tools/test_m1_usb_arm.py build-m1-hal/m1_usb_audit.elf
python tools/test_m1_live_arm.py build-m1-hal/m1_live_audit.elf
python tools/test_m1_storage_arm.py build-m1-hal/m1_storage_audit.elf
python tools/test_m1_save_arm.py build-m1-hal/m1_save_audit.elf
python tools/test_m1_boot_arm.py build-m1-hal/m1_boot_audit.elf
python tools/test_m1_image_arm.py build-m1-hal/m1_development.elf
python tools/test_m1_runtime_power_arm.py build-m1-hal/m1_development.elf
python tools/test_monsgeek_identity.py
python tools/test_monsgeek_iap.py
```

The ARM artifacts are `libm1_boot.a`, `libm1_live.a`, `libm1_save.a`, `libm1_hal.a`, `libm1_storage.a`, `libm1_board.a`, `libat32_sdk.a` and shared
application archive `libmidi_typist_app.a`, plus `libmidi_typist_services.a`. Native CTest passes an
actual C-encoded 82-key snapshot through SysEx into the GUI decoder. There is
a generated `m1_development.bin` for the GUI's M1 factory-conversion action.
`m1_hal_audit.elf`, `m1_usb_audit.elf`,
`m1_live_audit.elf`, `m1_save_audit.elf`, `m1_boot_audit.elf` and `m1_storage_audit.elf` have synthetic emulator-only memory maps and no boot header
or vector table: none is a flash image. Each test
requires the same Unicorn/pyelftools dependencies as the Huntsman ARM audits.
`m1_development.elf` and its `.map` use the actual application addresses. The
link includes a reset/vector table, SRAM-code/data initialization and a basic
foreground loop. Its separate image audit checks all load ranges, executes the
reset code and tests main-loop ordering with component calls stubbed. This is
not end-to-end execution or hardware qualification. The build never flashes.
The runtime-power audit executes the installed controller, power policy, wake
filter and SDK GPIO writes. HAL completion, radio status and elapsed sleep time
are scripted; it does not replace the separate HAL audits or physical power tests.
The USB audit covers the composite class/GUI path and guarded hardware startup,
reset-IRQ dispatch and shutdown; clocks, completion flags and delays are modeled.
The cold-handoff audit composes actual startup, scan pause/resume, USB/radio
initialization and application binding on both power sources; only profile I/O
is substituted. It is not a reset-handler or runtime power-management audit.
The foreground audit connects scripted scan/battery/LED boundaries to the real
application, USB class, radio scheduler/SPI/DMA drivers and GUI codec, including
discontinuity, release handling and gated Fn transport selection. External
host-release/selection callbacks and radio replies are scripted. Profile I/O
and storage pause/resume gates are scripted in this foreground audit, including
pending status, neutral autosave, restart restoration and failure handling.
The factory-calibration reader runs on synthetic read-only flash pages; the
foreground audit imports those records rather than accepting supplied bounds.
The storage audit runs the shared journal and official SDK flash driver in RAM
against modeled erase/program effects. It supplies a synthetic image-end symbol
and safety gate, not a deployable image or real power/quiescence qualification.
The save-gate audit composes the actual scanner, timebase, battery and lighting
HALs with scripted supply/transport inputs; it checks retained links/rails,
unchanged deferrals, measured pause/resume and terminal ownership failures.
The HAL audit also executes the wireless report scheduler through actual
SPI/DMA drivers with scripted status replies; no radio host is simulated.
The SDK package selector
enables AT32F405 family headers; it does not establish the physical chip's exact
package/density. See [M1 contracts and verification limits](MONSGEEK_M1.md).

## Build provenance

Every CMake build checks Git and generates `generated/git_identity.h` in its
build directory before compiling. This runs on incremental builds as well as
fresh ones, so a changed HEAD or clean/dirty state does not require manual
reconfiguration. The header changes only when the metadata changes; it contains
no timestamp. Keep source and Git state stable while building.

The firmware reports the full commit hash plus `clean`/`dirty` in its SysEx
handshake and the read-only `git` command. The GUI shows this as part of the
firmware identity. Dirty builds identify their base commit, not all local edits;
use a clean checkout for reproducible releases. Git-less exports and unborn
repositories report `unknown`, never a guessed hash. The identifier is
provenance, not a signature or an authentication check.

## Changing defaults

Edit [defaults.h](../firmware/app/include/defaults.h), then rebuild. It owns
factory Schmitt thresholds, velocity scaling/window, MIDI wheel endpoints and
note maps, calibration timing, LED/menu settings and autosave timing. Constants
include units and compile-time checks for invalid combinations. Keep scalar
definitions literal: the GUI and capture tools read this same header through
[firmware_defaults.py](../tools/firmware_defaults.py), without a C compiler or
duplicated fallback values. Distribute the header with the host tools.

Valid saved profiles override factory settings. Rebuilding or flashing does
**not** apply new factory thresholds to an existing profile or erase calibration;
edit settings through the GUI, or deliberately use the documented profile reset.
Hardware addresses, flash ownership and protocol encodings are not defaults.
Changes to tuning values need behavioral validation, not just a successful build.
Reference-backed threshold-table comparisons deliberately fail if you retune
the values away from the reference; they still check the header's table data.

The `defaults` test compiles the actual initializers with both shipped and
alternate defaults, rejects invalid combinations and checks host consistency.

## Tests that need no original firmware or device

The CTest suites run in parallel and cover application portability and architecture boundaries,
core logic, raw keyboard/velocity, MIDI state and
interruptible text lighting,
Fn menu/threshold conversion, parallel calibration/storage, GUI model/MIDI mock transport, image reservation,
strict capture framing, offline device-flashing validation, build-time Git
provenance (including incremental rebuilds) and the current-only repository rule.
Every native board build also runs `tools/test_midi_backend.py`: process-isolated
MIDI ownership, bounded capture queues, no-retry failures and stuck-child cleanup.
It opens no physical MIDI ports; its optional widget guard check requires Tk.
Neither the updater EXE nor proprietary extracted firmware is needed for
these tests or the application build.

For linked-ARM USB tests:

```sh
python3 -m venv .venv-audit
. .venv-audit/bin/activate
python3 -m pip install -r tools/requirements-audit.txt
cmake --build --preset huntsman --target audit-usb
```

Pinned optional dependencies are Unicorn 2.1.4 and pyelftools 0.33. The USB
target covers the compiled descriptor/control/endpoint paths at modeled full
and high speeds, Device-memory alignment, updater reset deferral, startup,
clock setup and USB chirp behavior. It does not open physical devices.

For a real Tk GUI test against a simulated MIDI peer, install Tk and `Xvfb`:

```sh
python3 -B tools/test_keyboard_gui_tk.py
```

The test starts a private virtual display with TCP disabled and uses simulated MIDI peers,
not a physical MIDI port. Optional `--screenshot /tmp/gui.png` additionally needs Pillow.
The GUI runtime needs python-rtmidi, but not PySerial or Pillow.

## Optional reference-backed audits

For M1 scan-bank wiring and provisional calibration policy, execute the original
selector and startup validity/fallback instructions from your read-only,
boot-prefixed ID2949/v410 image. Compare GPIO writes with the compiled board
table and the RAM fallback with current defaults (no device access):

```sh
python tools/test_m1_scan_reference.py build-m1-hal/m1_development.elf --reference /path/to/private/M1-V5-TMR-ID2949-v410.bin
```

This tests instruction behavior, not electrical acquisition timing. No original
image or extracted executable bytes are distributed with the test.

For the complete offline suite, install the optional Python dependencies above
plus Tk and Xvfb, then run:

```sh
python3 tools/run_tests.py
```

This configures/builds native tests, the complete `huntsman` target, and the M1
host/ARM libraries, then runs 23 independent audit jobs with up to
eight workers. It includes original-reference comparisons, linked ARM USB,
optical/MIDI/LED/storage/menu tests and the real Tk UI against simulated MIDI peers.
The total deadline, including builds, is **300 seconds**; failures, missing
dependencies and timeouts fail the command, never silently skip coverage.
Use `--jobs N` to control parallelism or `--group usb` for a focused run.
Per-job output is in the ignored `build-test-logs/` directory.

Each audit owns its emulator or MIDI mock. Native tests include the synthetic port;
the simulator preset is a separate runnable example, not additional hardware
coverage. See [Validation](VALIDATION.md) for what the suite establishes.

The original primary application is not distributed. If you already have a
lawfully obtained extraction, keep it read-only. The default reference is the
sibling path `../extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin`.
To select another read-only location:

```sh
cmake --preset huntsman \
  -DHUNTSMAN_PRODUCTION_REFERENCE=/absolute/path/to/primary-app.bin
cmake --build --preset huntsman --target audit-keyboard
cmake --build --preset huntsman --target audit-lighting
cmake --build --preset huntsman --target audit-calibration
cmake --build --preset huntsman --target audit-menu
```

These execute compiled ARM scan/MIDI/LED paths using synthetic optical replies
and recovered reference tables, and check selected production-derived mappings.
Some tools pin the production hash; do not substitute another image silently.
Each alias runs only its named audit; lighting does not rerun keyboard or USB.
Use the complete runner above to cover all of them once. Running reference-backed targets without
the separate reference is expected to fail; it is not a build dependency.

## GUI access and troubleshooting

Install the runtime MIDI dependency in an isolated environment:

```sh
python3 -m venv build-gui-venv
build-gui-venv/bin/pip install -r tools/requirements-gui.txt
build-gui-venv/bin/python tools/keyboard_gui.py
```

Tk must be installed for that Python interpreter. Building python-rtmidi from
source on Linux also needs a C++ compiler, Python development headers and
ALSA development headers (`libasound2-dev` on Debian/Ubuntu).
`--demo` runs without opening a device. Runtime access is through ALSA MIDI,
not serial or raw USB. Linux is the hardware-tested platform; Windows/macOS
backends, especially large SysEx buffer limits, are not validated.

Text needs no configuration. The GUI resolves the best family its Tk build
can render and, when that build has no fontconfig/Xft (some conda packages
are like that), uses X11 bitmap faces at their native pixel sizes so the text
stays crisp instead of being scaled. Tk cannot antialias without
fontconfig/Xft, so for the smoothest text use an interpreter whose Tk has it:
the distribution `python3` with `python3-tk` and `python3-rtmidi` (the venv
documented above inherits whatever Tk its base interpreter has). Text
rendering is a property of that interpreter, not of the keyboard firmware.
In a short window, the configuration page scrolls to keep large keyboard layouts,
settings and the footer reachable. The bottom-left settings panel also has its
own scrollbar; use the outer page scrollbar to reach that panel when necessary.

Use the port selector for multiple keyboards. Auto-detection chooses only a
unique paired control port; performance MIDI belongs in the DAW. Linux can
truncate the control name; the GUI recognizes the board's second cable.
Only one GUI control session is supported. A new handshake replaces a previous
owner; it is not an OS-level exclusive lock.

If the GUI rejects telemetry, use the matching GUI from this checkout: the
count-aware MTG4 layout requires SysEx version 3, and the device
reports its build identity (`version`) for the record. If waiting for neutral,
release every key; inspect threshold/raw values without repeatedly resetting
the keyboard. A MIDI cleanup-pending indicator means the host has not yet
accepted all cleanup events. It does not prevent switching back to HID mode.

## Hardware updates are separate

The updater is vendored as the git submodule `third_party/huntsman_updater`
(`Huntsman-V3-Pro-Mini-Flasher`); a checkout needs
`git submodule update --init third_party/huntsman_updater` before flashing, and
`HUNTSMAN_UPDATER_SRC` overrides its location. Flashing is available from the
[GUI's **Device flashing** tab](DEVICE_FLASHING.md)
using the private `tools/device_flash_service.py` worker and model adapter. Its elevated worker is
an internal GUI implementation detail, not a standalone user application.

No build/test target flashes or resets hardware. Use only the supplied updater's
reviewed application-only path after explicit authorization and record the exact
binary hash. The updater is pinned as the submodule described above.
Do not overwrite bootloader, factory/security data, primary stock settings or
secondary-controller regions. Settings/calibration write only the two documented tail
pages. Flashing preserves compatible records by default; firmware initializes
missing/corrupt saves. Use the confirmed Fn+R action to clear custom settings and calibration. Do not use manual forced bootloader recovery as a
routine test. Application updates are authorized for this device; that does
not authorize writes outside the application and documented calibration slots.
Build validation does not establish comprehensive MIDI/DAW compatibility.
