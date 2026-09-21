# MonsGeek FUN60 PRO Wired backend

Target: **ID2304/v309**, USB `3151:502D`, AT32F405. ID2305 (`3151:502F`) is not
interchangeable. This backend builds a HAL/application library, **not a flashable
image**. Complete application integration, persistent calibration normalization and
physical validation remain required. The GUI's firmware tab lists this model
for read-only USB identification, with MonsGeek flashing explicitly disabled.

The configuration GUI has a FUN60 board contract selected by READY build target
`monsgeek_fun60_pro_wired`, with layout 4, the shared 61-key geometry table and
nominal 1000 Hz capture calculations. Model/transport and actual Tk tests use
simulated MIDI peers for threshold/note edits and captures. The shared HKG
encoder is compiled into the library and native tests decode its real C output
with the GUI model. The complete device-side application lifecycle still needs
integration before physical use.

## Source and build boundaries

The board uses the pinned [official Artery SDK](https://github.com/ArteryTek/AT32F402_405_Firmware_Library)
for peripherals. See [SDK provenance](../third_party/ORIGINS.md). Board glue is
independently authored. The private reference remains read-only outside this
repository; no stock image, disassembly or recovered function is bundled.

```sh
git submodule update --init third_party/artery
cmake -S . -B build-fun60-hal \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DMT_BOARD=monsgeek_fun60_pro_wired
cmake --build build-fun60-hal
```

Outputs are `libfun60_hal.a`, `libartery_usb.a` and `libartery_peripherals.a`; none can be
uploaded. This is not a reduced-feature firmware release. The complete supported
Huntsman build remains unchanged.

## Firmware update contract

[monsgeek_iap.py](../tools/monsgeek_iap.py) is a private backend library, not
another desktop application.

| Item | Contract |
| --- | --- |
| Factory application | `3151:502D` |
| Bootloader | `3151:502A`; not sufficient to identify the SKU |
| Application header | `0x08005000`, 512 bytes, starts with `AT32F405 8KMKB  ` |
| Vector table | `0x08005200` |
| IAP erase range | 70 sectors of 2048 bytes: `[0x08005000, 0x08028000)` |
| Host-to-device | 64-byte HID Feature SET_REPORT, `21/09/0300`, interface 0 |
| Device-to-host | 64-byte HID Feature GET_REPORT, `A1/01/0300`, interface 0 |

**Entering the factory bootloader is already destructive.** Command
`7F 55 AA 55 AA 00 00 82`, zero-padded to 64 bytes, passes the byte-sum gate.
It erases settings sectors at `0x08028000`, `0x08033800`, `0x08034000`,
`0x08034800`, `0x08035000`, `0x08035800`, `0x08036000`, `0x08036800`,
`0x08037000`, writes the request marker `0x55AA55AA` at `0x08004800` and resets.
The bootloader erases the application **before USB enumeration**, not on PREPARE.
Do not enter IAP merely to inspect it.

Commands carry a little-endian 16-bit opcode, 16-bit block count and 24-bit
field; the remaining bytes are zero:

| Opcode | Purpose |
| --- | --- |
| `FFBA` | PREPARE initializes reply; not erase or finish |
| `C0BA` | START latches block count/checksum and arms writes at `0x08005000` |
| raw 64 bytes | Data blocks have **no opcode**, address or sequence number |
| `C2BA` | QUERY compares expected checksum and reports readback verdict |

Replies start `AB`, opcode high byte, then echoed count. QUERY adds `55` for
success or `AA` for failure at byte 4, and checksum at bytes 5–7. Checksum is
the transmitted byte sum, including final `FF` padding, modulo `2^24`.

GET_REPORT releases the result latch. Success causes the bootloader to erase
its boot-request flag and reset. Failure also requests reset, leaving that flag
set. The host must verify the expected application returns before reporting
overall success.

There are **no automatic retries**. A timed-out write may have been consumed;
repeating it shifts subsequent blocks. A matching-checksum QUERY failure clears
the readback-error counter, so repeating QUERY can falsely report success.
The backend validates exact reply size, opcode, count, status and checksum;
each transfer object can be used only once.

The common header does not identify the model. Factory restoration accepts
only the independently identified ID2304/v309 full-image SHA-256 recorded in
the backend, stripping the bootloader prefix rather than uploading it. Custom
images require a board-specific header marker. These prevent accidental
wrong-target flashing; they do not authenticate a firmware publisher.

## Hall acquisition and lighting

One [key table](../firmware/boards/monsgeek_fun60_pro_wired/include/fun60_keys.def)
defines labels, HID usages, geometry, row/mux addresses and LED indices.
Its 61 logical sensors are in physical row-major order, not Huntsman scan order.
Fn is to the **right of RAlt** on this board.

| Peripheral | Wiring / configuration |
| --- | --- |
| Clock | 12 MHz HEXT; PLL `72 / 1 / 4`, core 216 MHz, APB1 108 MHz |
| Row drive | PA4 data, PA5 clock, PA6 gate; 14 shift positions |
| Analog mux | PC6/7/8 address; scan channels 1–5, park channel 0 |
| Hall ADC | PA2 / ADC1 channel 2, AHB/8, 28.5 sample cycles |
| LEDs | PA10 AF5 / SPI2 MOSI, master half-duplex TX, mode 1, APB1/16 |
| LED DMA | DMA1 **channel 1**, SPI2_TX request, 1464-byte frame |

The scan publishes only after all 70 electrical positions have been sampled;
unused positions are discarded. Failure leaves output untouched, asserts the
gate and parks the mux. The HAL returns native 12-bit samples. Normalization,
calibrated bounds and final scheduling must be integrated before the shared
application can use physical readbacks.

The reference seeds each key's resting baseline from live samples; it does
not read a factory per-key ADC calibration table. Its `1000..3300` check is a
baseline plausibility gate, **not** released/pressed endpoints. The actuation
routine (`0x0800DC64`) compares decreasing readings against that baseline and
normalizes against a learned minimum; the scan initially estimates a floor
500 native counts below the current level. These facts establish direction
and adaptation, not a fixed physical travel range. Do not map 3300 to released
and 1000 to fully pressed simply from the plausibility check, or pass native
readings directly into Huntsman's 3500/3600 default Schmitt pair. The complete
port must supply a consistent per-key normalization/calibration contract for
thresholds, velocity, wheels and lighting.

`FUN60_SCAN_HZ` requests **1 kHz full-matrix cadence**, not measured throughput.
Settling, timeouts and cadence are in `defaults.h`. The original's 8 kHz USB
polling does not prove 8 kHz scans of every key. The shared velocity engine uses
the declared frame cadence; the complete port must detect missed deadlines.

LED output is GRB, MSB first: SPI byte `C0` encodes zero, `F0` encodes one.
The DMA buffer remains immutable until DMA completion and SPI idle, followed
by a low latch interval. The application sees RGB in sensor order; only the
board translates to electrical LED order.

## USB implementation

The official Artery OTG-HS device stack drives three interfaces: a 16-byte
NKRO HID report and USB Audio/MIDIStreaming with separate performance and
GUI control cables. MIDI bulk endpoints use 64-byte packets at full speed
and 512 at high speed. Chip-unique USB identity is derived from the SDK's
96-bit MCU identifier; it is not presented as a recovered factory serial.

Endpoint buffers are copied before acceptance and remain immutable until
completion. HID idle resends run in main; the USB OUT handler only frames
control commands. The same portable MIDI session/lease and scan-stream code
is used by Huntsman. No CDC or factory updater HID is advertised.

Two link-time wrappers leave upstream sources untouched: device qualifier
and other-speed requests work at either speed, and endpoint requests are
bounded before the SDK indexes its endpoint arrays. Unsupported HID boot
protocol and alternate settings stall explicitly.

Run the optional Cortex-M4 class audit with Unicorn and pyelftools:

```sh
cmake --build build-fun60-hal --target fun60_usb_audit
python tools/test_fun60_usb_arm.py build-fun60-hal/fun60_usb_audit.elf
```

The audit ELF has no application header, vectors or startup and **must not be
flashed**. It executes the actual class, descriptors and shared protocol,
with vendor endpoint transfers and clocks stubbed. It covers FS/HS packet
sizes, malformed endpoint requests, HID idle, buffer ownership, control-cable
handshake and deconfiguration. It does not validate the PHY, FIFO timing or
physical enumeration; a complete application/timebase is still required.

## Application storage boundary

The linker reserves `0x08027000` and `0x08027800` as two independently erasable
2 KiB custom slots. Both are inside the application allocation; code/data load
sections must end before `0x08027000`. Factory storage at `0x08028000` and above,
the bootloader, and its request flag are not targets of the slot writer.
The application RAM budget is conservatively 64 KiB, including an 8 KiB stack.

`fun60_flash.h` accepts only a slot number, never an arbitrary address. Writes
stage RAM data, preserve/mask interrupts, unlock, erase and blank-verify the
selected sector, program/verify aligned words, then relock. The linker places
the writer and the unmodified official flash driver in the startup-copied RAM
data section. Any write/verification failure latches until restart, without
retrying or overwriting the other slot. If the controller remains busy after
the vendor timeout, execution deliberately stays in RAM with interrupts masked
rather than returning into the busy flash bank. This is a fail-stop condition,
not a promise of a responsive USB error reply.

The flash writer is not yet connected to the application lifecycle. Its audit
uses the actual linked SDK driver with modeled registers:

```sh
cmake --build build-fun60-hal --target fun60_flash_audit
python tools/test_fun60_flash_arm.py build-fun60-hal/fun60_flash_audit.elf
```

This test ELF is also **not flashable**. The audit checks RAM execution while
busy, exact erase/program bounds, blank/readback verification, IRQ-mask
restoration, failed-word handling and the retry latch. It does not prove real
flash timing, watchdog behavior or physical power-loss recovery.

**A FUN60 firmware update clears custom settings.** The factory IAP erases both
reserved sectors before USB enumeration, even if the uploaded image is shorter.
Factory data is not used as an alternate place to preserve custom profiles.

## Evidence and verification limits

Relevant ID2304/v309 reference addresses:

| Address | Observation |
| --- | --- |
| `0x08000420`, called at `0x080012E0` | 70-sector erase before IAP USB initialization |
| `0x0800048C`, `0x080006D0`, `0x080007F0` | Commands, block/readback path, IAP result state |
| `0x0800090C`, `0x08000878` | HID control setup and EP0 receive |
| `0x08012216` | Destructive factory boot entry |
| `0x08005774`, `0x080125E0`, `0x0801477C` | Acquisition, ADC setup, GPIO setup |
| `0x08014618`, `0x080143D4`, `0x080132A8` | Clock, SPI setup, DMA channel 1 |
| `0x0801C360`, `0x0801E140`, `0x0801E1A0` | Factory actions and electrical LED correspondence |

Native tests cover image bounds, wrong-model rejection, short/stale replies,
no retries, all 61 key/LED mappings, complete scans, failures at every ADC
position, RGB encoding, shared keyboard actuation, and the shared journal's
FUN60 layout restore/corruption/torn-write behavior. These journal tests use
RAM-backed callbacks; the separate ARM audit exercises the AT32 writer. Run
`ctest --preset host-tests`.

The optional original-code audit executes the private image's ARM command,
EP0, program/readback and result handlers and its erase loop. Per-sector erase,
flash busy state and USB transfer machinery are modeled:

```sh
python tools/test_monsgeek_iap_arm.py /path/to/FUN60-PRO-ID2304-v309.bin
```

It also checks the authored wiring facts against factory mappings. It needs
Unicorn; the private image is unnecessary for ordinary builds/native tests.
This is **not hardware validation**. USB pacing, ADC settling, actual MCU
package/memory geometry, cold boot, LED colors and physical placement still
require verification. FF-filled factory data is not automatically free space:
custom persistence needs independently established ownership and erase bounds.
