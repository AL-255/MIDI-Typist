# Huntsman device calibration and settings storage

Calibration and the Fn-menu settings use **only two whole 512-byte pages at
physical addresses 0x7d400 and 0x7d600**. The beginning of configuration
storage, including the serial number and primary settings at 0x49000..0x49400,
is not an erase target. The HKC1/HKS1 serializer and controller adapter belong
to the [Huntsman board](../firmware/boards/huntsman_v3_pro_mini/src/calibration_store.c).
Shared calibration requests storage through `keyboard_app_ops_t`; the board
mirrors the Fn-menu settings itself. These addresses and this 65-sensor format
must not be copied to another platform.
See [the storage port contract](PORTING.md#5-add-lighting-storage-and-host-integration).

## Evidence and ownership

Two independent controller reads of 0x49400..0x7d800 matched,
without read errors. Both selected pages contained exactly 512 FF bytes.
The original allocator's block chain at 0x54400 identifies five allocated
0x580-byte blocks followed by a free block starting at 0x55f80 with size
0x29480 (ending at 0x7f400). The selected pages are inside its free payload,
not its header or footer. The final footer lies outside the conservative
read boundary and was not read; it is not claimed verified.

Tail and primary-settings backups and their checksums belong in private,
Git-ignored device-dump metadata. No serial-number bytes are published.

The FF tail inside the primary settings' second page is deliberately **not**
used: erasing it would also erase existing settings in that same page.
The independent application does not use the original allocator. Returning to
stock firmware may reclaim or clear the free block and lose our records.
We do not alter allocator boundary tags or promise that stock preserves our data.

## Record and recovery

Each HKC1 page carries a calibration part and an optional settings part, each
with its own generation, in this little-endian layout:

| Offset | Field |
| --- | --- |
| 0 | Four-byte `HKC1` magic |
| 4, 5, 6, 7 | uint8 version 1, layout, sensor count, reserved zero |
| 8 | uint32 calibration generation |
| 12 | uint32 ownership marker `0x314c4143` |
| 16 | 65 uint16 lower bounds |
| 146 | 65 uint16 upper bounds |
| 276 | Optional settings block, otherwise FF padding |
| 508 | IEEE CRC32 over bytes 0..507 |

Layout 0 with count 0 and zeroed bounds marks a page that only carries
settings; a present calibration must name its layout, match the sensor count,
keep at least 512 counts of range inside the valid ADC domain and zero unused
sensors. The settings block is:

| Offset | Field |
| --- | --- |
| 276 | Four-byte `HKS1` magic |
| 280 | uint8 version 1, reserved zero |
| 282 | uint32 settings generation |
| 286 | 20-byte build identity, NUL-padded (`v0.1.0-RZ03-0499`) |
| 306 | 16-byte payload, see below |
| 322..507 | FF padding |

The payload holds the Fn-menu state: trigger source (0 defaults, 1 keyboard
trigger editor, 2 MIDI trigger page), calibrated trigger level, rapid level and
enable, MIDI trigger step, transmitted-velocity start, Jankó layout, lower-row
mute, brightness, music root and scale, octave and performance mode. Out-of-range
values, non-zero reserved bytes or unknown magic invalidate the block, never the
calibration part. Host `cfg set`/`cfg all` threshold edits and `cfg midi`
mappings are not stored.

Boot runs an **integrity pass** over both pages before anything is loaded. Each
page must be blank, or carry our markers with a CRC32 that matches over bytes
0..507; the checksum therefore covers the header, both bound arrays, the
settings block and the padding, so any single changed byte and any partial
program (a power loss mid-write leaves a written prefix and an erased tail)
fails it. A page that fails is corruption, and the pass then **clears the whole
region** and continues: the session is a cold boot from an empty store. A clean
store is only read - no boot-time erase or program happens unless corruption has
to be cleared. A page that cannot be read at all cannot be classified, so it is
left alone and the load path reports the read error instead of erasing the
region on every boot.

After that pass, boot takes the newest valid generation of each part
independently. Without a valid settings record, or with one whose build identity
differs from the running application, the application keeps its defaults: that
is the **cold-boot condition**, and it is why a freshly flashed build cannot
inherit the previous build's state.

A save writes only the inactive page, leaving the other record untouched. Before
erase, the target must read successfully and be entirely FF or carry our
recognizable HKC1 header. Unknown contents cause a save failure, not an erase: a
page holding data we did not write is skipped and the save falls back to the
other authorized slot, so one damaged or foreign page never disables
persistence. Rewriting one part preserves the other: a calibration save keeps
the settings block in its page and a settings save keeps that page's
calibration part.

Clearing wipes the whole region. Both callers are deliberate: Fn+R and
`cfg clean` from an operator, and the boot integrity pass after a checksum
failure. Every non-blank page is erased, including content we cannot identify
and a page that cannot be read at all - an interrupted program can leave
ECC-invalid data that nothing else can reclaim - because both addresses are
authorized pages. If an erase succeeds and only the read-back stays broken, the
clear is satisfied: such a page holds no record this application could load
again. If an erase fails, the clear fails and reports it, and the region is left
as it was.

The full page is erased, programmed, read back and compared byte for byte,
including CRC, before RAM state and the active generation are updated. Power
failure during a first save can leave no valid record, in which case defaults
are used. After a valid save, interrupted inactive-page writes leave the older
valid record available. This is a software-level recovery design, not a claim
that power-cut silicon testing has been performed.

## Mirroring the Fn menu

A menu change starts a 1.5 s debounce; the write happens only after every key is
released, so an edit never stalls the optical scan mid-keystroke. A save rotates
to the slot whose settings generation is older, keeping flash wear off one page
and the previous record available. The RAM baseline follows what boot loaded, so
a cold boot never writes a record of its own defaults.

Fn+R and the host's `cfg clean` erase both pages: `cfg clean` reads them back
blank before answering result 1, and `tools/flash_application.py` sends it after
every flash. Defaults then apply on the first neutral frame.

## Controller boundary

The application uses NXP SDK register definitions/status codes and the
controller sequence established by the original working application:
command 4 erases one page, 32 command-8 loads populate its buffer, and command
12 programs it. Status polling is bounded; a controller timeout latches out
further commands. Interrupt state is preserved, cache is flushed, and the
existing watchdog is serviced before/after operations. Code and stack execute
from RAM. SDK ROM-wrapper calls are excluded; the controller adapter is
checked against original ARM register transactions.

The adapter accepts a slot number, never an arbitrary write address, and
validates the page it is given with the same page predicate the store uses.
Invalid slot, geometry, clock or record rejects before erase.

**Write bounds.** Every erase and program in the whole application lives in
this adapter and derives its address from one of two constants, `CAL_SLOT_A`
and `CAL_SLOT_B`; the shared application contains no flash write path at all
(`keyboard_config.c` keeps its production commit RAM-only for exactly that
reason), and no ROM/IAP API is linked. `config_allowed` additionally requires
`slot<2`, the board clock and a flash size large enough to contain both pages
plus the reserved tail. The application image occupies 0x0..0x20000, and the
primary settings with the serial number sit at 0x49000..0x49400; neither is
reachable from the adapter. The offline flash model enforces the same rule: it
rejects any controller command whose page is not one of the two authorized
pages, and it rejects the application and primary-settings ranges explicitly,
so an ARM test fails rather than silently corrupting them. That test also
reports the address set every mirror, reload and cold-boot flow touched, which
is exactly `0x7d400` and `0x7d600`.

The read path retries a failed word read twice before reporting an error. The
reference driver read each word once; retrying keeps one flaky controller
response from failing a boot load, a save's read-back or the cold boot, while
a controller that stops signalling DONE still latches out further commands. CDC exposes no
raw erase/program command: only calibration completion, mirrored Fn-menu
changes and the cold boot can write. Fn+R previews `RESET`; release opens `RESET?`
with full-brightness green Y/red N. After all keys are released, a fresh Y press
invokes the bounded clear operation; N cancels without erasing. Pre-held Y cannot
confirm and simultaneous Y/N cancels. Both pages must be blank or recognizable
HKC1 records before any erase; the older slot is cleared first and each erase is
read back. Unknown contents or controller errors stop clearing. Empty pages are
skipped. Successful clearing removes saved calibration and Fn-menu settings and
restores application defaults on neutral input; it does not erase factory/serial
data or reboot USB. See [RESET](FN_MENU.md#reset-and-flash-boundaries).

The original 1 KiB reservation at image offsets 0x1fc00/0x1fe00 remains FF.
Although readback establishes physical application base 0x8000, we do not
modify its image/checksum bytes for persistence. Bootloader, factory/security,
secondary ASIC and the serial-number pages remain outside write scope.

## Validation

```sh
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
cmake --preset huntsman
cmake --build --preset huntsman
# Optional offline ARM dependencies and original reference required:
python3 -B tools/test_calibration_arm.py \
  build-keyboard-fn-menu/huntsman_firmware.elf --reference /path/to/original.bin
```

Native tests cover simultaneous 61/62/65-key holds at 8 kHz, independent
movement/release, timing, layouts, rollover, noise, cancellation, CRC damage,
unexpected page contents, error handling and all 512 byte-cut points in an
interrupted inactive-page write, plus settings round-trips, A/B rotation,
calibration/settings coexistence in one page, cold boot on a foreign build,
range and damage guards, slot fallback for an unreadable page, clearing
(including recovery of a page that stays unreadable) and the integrity pass:
every one of the 512 single-byte corruptions and every 16-byte-granular partial
program is detected and cleared to a cold boot. ARM tests compare exact register writes
with executed original erase/program instructions, then exercise compiled Fn+C,
CDC commands, complete sequential and parallel simulated 61-key acquisition,
save and reboot loading, the released-key mirror gate, a pre-seeded record
applied on boot, a torn page detected and cleared at boot, and `cfg clean`.
These tests never access the real keyboard. See [calibration operation](CALIBRATION.md)
for physical validation status and limitations.
