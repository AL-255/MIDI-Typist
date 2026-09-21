# Device settings storage

The complete application persists settings and calibration in two reserved
**512-byte tail pages: 0x78000 and 0x78200**. The Razer primary settings and
serial-number region at **0x49000..0x49400** is never a write target.
Bootloader, application image, factory/security/PFR and secondary ASIC storage
are also outside this writer.
M1 has a [read-only factory calibration importer](MONSGEEK_M1.md#read-only-factory-calibration)
and an audited application-tail writer connected to foreground restore/autosave.
Writing requires explicit outer-owner power/quiescence/resume callbacks; without
them, edits remain pending in RAM. The operational instructions below apply to the complete
Huntsman application; [M1's reservation and writer](#m1-application-tail-backend)
have a different update-retention contract.

## What is saved

Each complete snapshot includes keyboard/MIDI mode, Jankó, lower-row mute,
brightness, velocity start, root, scale, octave, output enable, committed
trigger/rapid levels, and every sensor's thresholds, keyboard and MIDI mappings, and completed
calibration bounds. Held keys, sounding notes, wheels, sustain, velocities,
editor previews and incomplete calibration are not saved.

Settings are checked every 20 ms. Saving waits for 250 ms without further
changes, all keys above release thresholds, and no editor, preview or
calibration. Release all keys and wait for **settings saved** in the GUI before
unplugging. A command ACK means applied in RAM, not durable yet. Unplugging
while pending can restore the previous snapshot. No unchanged settings are
rewritten; ordinary notes do not wear flash. Repeated K/L taps coalesce while
Fn remains held.

## Boot and recovery

Loading happens once after layout discovery, before first keyboard/MIDI output.
The newest CRC/range/schema/layout-valid snapshot is restored; generation
ordering handles wraparound. Output then waits for neutral.

First installation or two invalid/incompatible snapshots initializes defaults
and automatically erases/programs/verifies a fresh snapshot in the owned tail
area. One valid snapshot is sufficient for recovery; a bad peer never causes
the good snapshot to be erased. Compatible application updates retain settings.
Only the current MTP2 schema and matching layout are accepted. Unsupported
records are invalid input, not migration sources; current valid saves survive
application updates.

ECC failures invalidate nonblank data. Other controller faults, geometry
errors and timeouts inhibit automatic writes rather than trigger erasure.
Save failures latch for the session, avoiding infinite retries and wear.

## Complete snapshot format

The SDK-free `firmware/services/src/device_store.c` owns the codec and two-slot
journal. Board builds select record size, identity and recoverable read error;
board callbacks alone own physical addresses and controller operations.

| Board | Identity | Record size | CRC offset | Integration |
| --- | --- | --- | --- | --- |
| Huntsman | MTP2 | 512 | 508 | Application and bounded NXP writer |
| M1 | M1P1 | 2048 | 2044 | SDK writer and foreground restore/autosave; outer safety gate required |

The distinct identities bind board-local layout numbers to their physical
namespace. Neither format accepts the other or migrates older records.
Both use the following little-endian fields; page-end offsets below describe
Huntsman. M1 extends the all-ones padding through byte 2043 and stores its CRC
over bytes 0…2043 at byte 2044.

| Offset | Field |
| --- | --- |
| 0 | Four-byte format identity |
| 4 | Board-local layout; sensor count derived from the board description |
| 5 | Calibration present, 0/1 |
| 6 | u32 whole-profile generation |
| 10 | u32 calibration generation |
| 14–18 | Thirteen packed global fields (below); high four bits of byte 18 are 1 |
| 19 | Packed sensor bitstream, in scan order |
| After last sensor through 507 | All unused bits are 1 |
| 508 | CRC-32 of bytes 0…507 |

Globals: performance mode, Jankó, lower mute, brightness (0…19), velocity start
(1…10), root (0…11), scale ID, octave + 10 (0…20), output enable, saved
actuation, saved rapid, rapid enable and lock.
Their respective bit widths are `1,1,1,5,4,4,4,5,1,4,4,1,1`.
Fields are packed least-significant bit first, starting at byte 14.

Each sensor stores two ordered pairs, each in 23 bits: thresholds then
calibration endpoints. For `1 <= a < b <= 4096`, encode the pair losslessly as
`(b-1)*(b-2)/2 + a-1`. Threshold release must be below 4096; calibration span
must be at least 512. Absent calibration uses 23 zero bits.

Mapping fields follow, according to immutable physical role:

- Fn: no mapping bits; keyboard disabled and MIDI unmapped implicitly.
- Six MIDI controls (LCtrl, LGUI, LAlt, RCtrl, RAlt, Space): an eight-bit
  keyboard usage; MIDI remains unmapped.
- Other keys: 15 bits encoding `keyboard_index*129 + note_index`.
  Keyboard index is 0 for disabled, otherwise usage minus 3 (usages 04…E7).
  Note index is 0…127 or 128 for unmapped.

Role selection never uses the user mapping. Including header and CRC, ANSI,
ISO and JIS require 481, 489 and 512 bytes respectively. No endpoint precision
or settings are discarded, and no additional flash pages are reserved.

## Atomic replacement and controller safety

Erase, blank-verify, program and byte-for-byte readback (including CRC) target
only the inactive page. It becomes active only on success. Never overwrite the
sole good snapshot as a fallback after failure. Interrupted writes recover the
old or complete new snapshot, not mixed settings/calibration. An interrupted
first save can leave no valid record and reinitialize defaults.

The Huntsman adapter uses official NXP SDK registers/status codes and the original
working application's CMD4 erase, 32 CMD8 buffer loads and CMD12 program
sequence. Code/stack execute from RAM; IRQ state is preserved, cache flushed,
watchdog serviced and all polls bounded. Slot index, geometry, clock and
record validation precede erase; caller-supplied write addresses are impossible.

**CMD5 verifies erased pages.** Ordinary CMD3 reads of erased LPC55 flash can
produce ECC error 116 because valid parity is absent. CMD5 checks data and
parity with ECC disabled. The storage API returns logical FF only after
confirmed blank-check success; the diagnostic dumper still reports real
per-word errors. See [NXP's explanation](https://community.nxp.com/t5/LPC-Microcontrollers-Knowledge/LPC55xx-Erased-Memory-State-0-or-1/ta-p/1135084).

The application-image 1 KiB reservation stays FF and unused.

## M1 application-tail backend

`m1_storage` reserves **0x08027000 and 0x08027800**, two 2048-byte pages inside
the custom application region. The reference boot erase loop at `0x08000420`
erases 70 pages from `0x08005000`, stopping before stock settings at `0x08028000`.
Factory calibration at `0x08032000/0x08032800`, key types, boot flag, bootloader
and all other stock storage are outside this writer.

**The factory bootloader erases both profiles on every application reflash.**
This backend does not promise update-time retention or implement backup/restore.

The caller supplies only slot 0/1 and must explicitly confirm adequate power,
released/drained host outputs and quiescent acquisition/transports. Additional
guards reject active DMA1/2 channels, ADC1, scan timers or busy LED/radio SPI,
unexpected controller state, invalid records and non-foreground/unprivileged
calls. The read-only size register must report the 256 KiB part, matching the
2 KiB erase geometry; see [Artery's reference manual, §1.3.1](https://www.arterychip.com/download/RM/RM_AT32F402_405_EN_V2.01.pdf)
and [datasheet, table 19](https://www.arterychip.com/download/DS/DS_AT32F405_402_V2.01_EN.pdf).
No actual chip-density measurement is claimed by these checks.

The linker must define the real flash load-image end below the first slot,
including initialized RAM code/data. `linker/storage_ram.ld` places the
transaction and official SDK flash functions in SRAM, with a separate code
load address; startup must copy them before use. During erase/program, IRQs
are masked and a temporary SRAM vector table directs NMI/HardFault to an SRAM
stop handler. Successful and completed-error paths relock and restore VTOR/IRQ
state. Erase is blank-verified and each programmed word is checked; the journal
then compares the whole readback and CRC before accepting the generation.

SDK erase/program polling is bounded by `M1_FLASH_*_WAIT_LOOPS` in `defaults.h`,
applied through a forced-include configuration without editing vendor source.
These are iteration budgets, not measured time guarantees. If flash stays busy
after timeout, returning to flash-resident code is unsafe: stop in SRAM, leave
IRQs masked, disable SysTick and do not retry. There is no option-byte operation,
mass erase, protection change or automatic reset.

`m1_live_init` loads the journal before accepting a real frame. Valid custom
calibration takes precedence; otherwise validated factory bounds are required.
Saved MIDI mode is suppressed for wireless operation. GUI fields report the
actual journal slot, generation, pending state and latched error independently
of the imported-calibration flag.

`device_store_poll` marks pending changes without writing. When a stable neutral
snapshot is due, the foreground additionally checks completed neutral output,
MIDI cleanup, local transport drain, idle lighting and no transport selection.
The supplied `m1_live_storage_ops_t.begin` must prove power/host safety and pause
hardware; false means unchanged/deferred, not a flash fault. After the bounded
write/readback, `end` must discard pre-pause acquisitions, resume hardware and
preserve accurate outer clocks across masked-IRQ time. The deliberate scan gap
invalidates capture/velocity and requires neutral before rearming. A failed
resume is terminal; reinitialization cannot silently clear it. No unchanged
settings are rewritten, and a write failure is not retried in that session.

The scanner provides `m1_hal_pause` / `m1_hal_resume` to stop periodic
acquisition without reinitializing ADC calibration or power rails. It discards
partial/unread frames and battery data. This is only one owner primitive:
it neither qualifies power nor drains lighting/radio/USB or maintains clocks.

The physical pause/drain/power coordinator, live calibration save/reset,
startup RAM copy and complete application image remain unfinished. Foreground
audits script the owner gate and profile I/O; the separate backend audit runs
the actual SDK against modeled controller effects. Neither proves hardware
power qualification or physical persistence.

## Reset, flashing and telemetry

Fn+R previews RESET and opens RESET?; after neutral, Y confirms and N cancels.
Both owned pages are erased and CMD5-verified, older first. Corrupt contents
can be reset within those slots; controller errors stop the operation.
Defaults apply after release and save automatically. Two-page reset is not
atomic: interruption may retain the newest record or leave defaults, but
older-first erasure prevents stale-record resurrection.

Explicit MIDI SysEx `cfg clean` performs the same destructive custom reset and needs
a fresh valid scan. Its ACK confirms erase, not the neutral gate. Neither
path touches Razer data. Recover deleted calibration by recalibrating or using
a private backup.

MTG3 header offsets 46/47 report valid/pending/fault flags and slot; offset 72
reports the full 32-bit profile generation, and 64/68 calibration generation/error.
See [telemetry](TELEMETRY.md). GUI flashing preserves compatible records by default; confirmed Fn+R
explicitly clears them. Stock firmware may
reclaim this custom tail space.

## Validation

Native tests cover all layouts, packed fields, unchanged-state wear,
neutral debounce, settings/calibration preservation, corruption, controller
faults and all 512 byte-cut points in an inactive-page write. The same native
suite tests all 82 M1 sensors and all 2048 byte-cut points, including calibration
and mappings, wrong-format records with valid CRCs, and board-specific error
handling. A separate M1 ARM audit runs the real journal, SRAM transaction and
official SDK with modeled controller effects: exact slot targets, IRQ/vector
restoration, denied contexts/geometry/activity, blank/program verification,
fault latching, reboot fallback and stuck-busy fail-stop. It does not save on a
device or qualify physical power-loss behavior.
Compiled ARM tests drive Fn+Enter/Fn+J, MIDI SysEx thresholds/velocity/keycodes, reboot,
blank-ECC initialization, corrupt-page recovery and unsupported-schema rejection.
The controller model rejects commands outside the tail pages and compares
erase/program transactions against executed original code, separately
checking the added CMD5 verification.

Run `python3 tools/run_tests.py` with the documented offline dependencies and
reference fixture. Modeled interruption tests are not physical endurance or
power-cut qualification; see [validation limits](VALIDATION.md).
