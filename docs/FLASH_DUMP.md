# Huntsman private, read-only main-flash acquisition

The `huntsman` application includes a bounded CDC
flash reader alongside MIDI, lighting and parallel calibration.
Dump commands are read-only; calibration has separate, tightly bounded
tail-page write APIs.
See [write scope and recovery](DEVICE_CONFIG_STORAGE.md).
The [flash reader](../firmware/boards/huntsman_v3_pro_mini/src/flash_dump.c)
and HBD1 geometry are board-specific, not a portable MCU memory-access API.
Another platform needs its own safe read boundary and updater integration;
see [porting](PORTING.md).

## Build and use

```
cmake --preset huntsman
cmake --build --preset huntsman
cmake --build --preset huntsman --target audit-dump
python3 -B tools/test_dump_flash.py
python3 -u tools/dump_flash.py --start 0 --length 0x10000 \
  --output device-dumps/bootloader.device-dump.bin
```

Install the application using the existing updater first. Build/test/dump
commands do not reset, erase or program the device. Close the GUI and all
other CDC users before acquisition. Host keyboard/scan/light processing
continues; the dumper takes ownership of the CDC stream only. It stops the
stream on exit; reopen the GUI or explicitly select another stream afterward.
Wait for any calibration run to finish before acquiring a dump. To read only
the calibration slots, use `--start 0x78000 --length 0x400` with a new private
output name. A raw dump alone does not decode or validate the HKC1 record CRC;
the format is documented in [storage](DEVICE_CONFIG_STORAGE.md).

Outputs are private mode-0600 files and are never overwritten. `device-dumps/`
and `*.device-dump.bin` / `*.device-dump.json` are ignored by Git. Do not force-add
these files, extracted disassemblies, or recovered vendor code. Dump metadata
is a companion `.bin.json`; keep it with the image. The tool reads the entire
requested range twice and requires identical data, geometry and per-word
status. CRC, timeout, identity, geometry or readback disagreement fails the
capture, with no retry/reset or claimed successful image.

## Device protocol and safety

Command: `dump read ID ADDRESS` followed by newline; both arguments are
decimal, ID is a nonzero uint32, address is 64-byte aligned. One outstanding
request is allowed; busy/malformed commands perform no flash access. Reads
are bounded by `0x00000..0x7f400` and the production PARTID size selector minus
a conservative 10KiB reserved tail. Unknown/flashless die IDs are rejected rather
than using the original's 640KiB fallback. Addresses never select
RAM, arbitrary MMIO, ROM, security/PFR configuration, or external ASIC storage.

Only explicit valid dump requests issue read commands through this interface,
using NXP's register
definitions and status codes at the existing SDK-initialized 96MHz clock.
There are no ROM calls or flash dereferences. Dump requests cannot write;
calibration and Fn+R profile reset have separate two-page-only write/erase APIs.
The command adapter independently implements the register transaction observed
in original routine `0x20001f94` (called by `0x2000ee04`): clear status, set the
16-byte word address, select normal margin/ECC-enabled/no-DMACC, issue command
3, check completion/errors, then copy the four data registers. It has a bounded
96,000-poll timeout; a timeout latches status `0x10001` and prevents additional
commands until reset. FAIL, ERR and ECC status priority follows the reference.
The production PARTID size selector is at SYSCON offset `0xfe0`, not the SDK's
`DEVICE_ID0` (`0xff8`, ROM revision). This distinction is tested explicitly.

Each request reads four 16-byte words separately, so one ECC/read failure does
not conceal readable neighboring words. Failed words are returned as zero
placeholders with their SDK status; **they are not original bytes and must not
be used as a restoration image**. The JSON lists every failed word address.
Repeated matching errors establish stable holes, not successful data recovery.
Do not infer that an ECC error means a word is blank.

HBD1 is exactly 128 bytes, little-endian:

| Offset | Content |
| --- | --- |
| 0 | `HBD1` magic |
| 4, 8, 12 | uint32 request ID, physical address, payload length (64) |
| 16, 20, 24 | uint32 flash size, page size, request status |
| 28 | reserved zero |
| 32 | four uint32 SDK read statuses, in address order |
| 48 | 64 readback bytes, failed words zero-filled |
| 112, 116 | uint32 production PARTID register and SDK DIEID register |
| 120 | reserved zero |
| 124 | standard IEEE CRC32 over bytes 0..123 |

The response uses the existing binary CDC buffers, not the lossy debug ring.
Pending USB memory remains immutable. A second request cannot overwrite a
queued response. Explicit stream changes cancel unsent data; the host's ID and
address checks reject stale/misrouted replies.

## Tests

The host PTY test covers fragmented responses, two independent reads, CRC
failure, per-word error metadata, private output permissions and no overwrite.
The compiled ARM test exercises full/high-speed CDC, command fragmentation,
address/overflow rejection, error holes, latched controller timeout, immutable pending
IN storage, stream restoration and absence of linked ROM erase/program APIs.
Dump requests themselves are checked to issue no erase/program commands.
The optional `--reference PATH` argument compares read-command register writes,
data and FAIL/ERR/ECC precedence with executed original ARM instructions. Flash
controller completion is modeled; only real acquisition validates board behavior.

## Configuration authority

The user's latest constraint permits writes **only to unused pages near the
end**, preserving the beginning containing the serial number. Selected and
twice-read FF pages are 0x78000 and 0x78200. Primary pages at 0x49000/0x49200
must not be erased. Future application flashes are authorized, but bootloader,
factory/security and secondary-ASIC writes remain out of scope.
The original configuration writer at `0x200156cc` stores its primary 701-byte
record at `0x49000`; its storage helper at `0x2000be9c` uses 512-byte page
read/modify/erase/program/verify. These are storage facts, not copied code.

## Device readback and backup limits

The application image occupies physical `0x8000..0x28000`. Its full readback
matches the current build without controller/ECC read errors.
See [validation status](CALIBRATION.md#validation-status).
Application/configuration acquisitions, endpoint CSV/JSON and their metadata
are private, Git-ignored files under `device-dumps/`.

The private bootloader-region acquisition has ECC holes in its first 304
bytes, including its vectors. **It is not a restorable bootloader image.**
Matching reads with matching holes do not recover the missing bytes. The
serial-number/primary-configuration backup is complete; do not publish it.
Keep backups and their error-map sidecars together, and never infer that
unreadable words are FF.
