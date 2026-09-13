# Huntsman device settings storage

The complete application persists settings and calibration in two reserved
**512-byte tail pages: 0x78000 and 0x78200**. The Razer primary settings and
serial-number region at **0x49000..0x49400** is never a write target.
Bootloader, application image, factory/security/PFR and secondary ASIC storage
are also outside this writer.

## What is saved

Each complete snapshot includes keyboard/MIDI mode, Jankó, lower-row mute,
brightness, velocity start, root, scale, octave, output enable, committed
trigger/rapid levels, and every sensor's thresholds, MIDI mapping and completed
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
Only the current MTP1 schema and matching layout are accepted. Unsupported
records are invalid input, not migration sources; current valid saves survive
application updates.

ECC failures invalidate nonblank data. Other controller faults, geometry
errors and timeouts inhibit automatic writes rather than trigger erasure.
Save failures latch for the session, avoiding infinite retries and wear.

## Complete snapshot format

One little-endian MTP1 record occupies one page.

| Offset | Field |
| --- | --- |
| 0 | Magic MTP1 |
| 4 | Format 1 |
| 5, 6 | Optical profile, sensor count |
| 7 | Calibration present, 0/1 |
| 8 | u32 whole-profile generation |
| 12 | u32 schema identity 0x3150544d |
| 16–28 | Thirteen global bytes (below) |
| 29 | u32 calibration generation |
| 33 | Up to 65 seven-byte sensor entries |
| After last sensor through 507 | FF padding |
| 508 | CRC-32 of bytes 0…507 |

Globals: performance mode, Jankó, lower mute, brightness (0…19), velocity start
(1…10), root (0…11), scale ID, signed octave (−10…10), output enable, saved
actuation, saved rapid, rapid enable and lock.

Sensor entries: two packed 12-bit thresholds in three bytes, two packed 12-bit
calibration endpoints in three bytes, then a MIDI mapping byte (0…127 or 255).
Samples 1…4096 encode as value minus one; first/second values occupy bits
0…11/12…23 of a little-endian 24-bit pair. Thresholds satisfy press < release
< 4096; calibration lower/upper have at least 512 counts of separation.
Absent calibration uses three zero bytes. Reserved MIDI controls are unmapped.

## Atomic replacement and controller safety

Erase, blank-verify, program and byte-for-byte readback (including CRC) target
only the inactive page. It becomes active only on success. Never overwrite the
sole good snapshot as a fallback after failure. Interrupted writes recover the
old or complete new snapshot, not mixed settings/calibration. An interrupted
first save can leave no valid record and reinitialize defaults.

The adapter uses official NXP SDK registers/status codes and the original
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

GUI offsets 1144…1147 report valid/pending/fault flags, slot and low 16 bits of
whole-profile generation; 1136/1140 retain calibration generation/error.
See [telemetry](TELEMETRY.md). GUI flashing preserves compatible records by default; confirmed Fn+R
explicitly clears them. Stock firmware may
reclaim this custom tail space.

## Validation

Native tests cover all layouts, packed fields, unchanged-state wear,
neutral debounce, settings/calibration preservation, corruption, controller
faults and all 512 byte-cut points in an inactive-page write.
Compiled ARM tests drive Fn+Enter/Fn+J, MIDI SysEx thresholds/velocity, reboot,
blank-ECC initialization, corrupt-page recovery and unsupported-schema rejection.
The controller model rejects commands outside the tail pages and compares
erase/program transactions against executed original code, separately
checking the added CMD5 verification.

Run `python3 tools/run_tests.py` with the documented offline dependencies and
reference fixture. Modeled interruption tests are not physical endurance or
power-cut qualification; see [validation limits](VALIDATION.md).
