# MIDI-Typist: official SDK provenance

## Artery AT32F405

`third_party/artery` is the unmodified official
[AT32F402/405 Firmware Library](https://github.com/ArteryTek/AT32F402_405_Firmware_Library)
submodule, pinned to SDK v2.1.5 commit
`5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3`. M1 CMake checks this revision and
compiles its ADC, CRM, DMA, GPIO, SPI, timer, ERTC, EXINT, power and USB modules,
plus the official USB device core, standard requests and interrupt driver with CMSIS headers. Original
license notices remain in those upstream files. No code or tables from the
original MonsGeek executable are vendored. Board-owned USB descriptors/class
and request-validation linker wrappers sit above the unchanged library.
Board-owned startup uses CMSIS cycle-counter delay hooks. Additional wrappers
provide the PHY power-up delay and defer attachment until SDK initialization
postconditions pass; the SDK files remain unmodified. Complete runtime binding
and physical validation are pending; the M1 library/audit builds are not a
complete firmware.

## NXP LPC55

The Huntsman board build is self-contained. Its board manifest selects the
NXP components; shared application and synthetic-port sources do not depend
on this SDK. See [architecture](../docs/ARCHITECTURE.md) and
[adding another vendor platform](../docs/PORTING.md).
The files under `third_party/nxp` are
unmodified source snapshots from the official MCUXpresso SDK repositories.
They were selected from the MCUXpresso Installer 26.06.123 catalog and pinned
to the `release/26.06.00-lts` manifest revisions below.

| Directory | Official repository | Revision |
| --- | --- | --- |
| `core/drivers` | `nxp-mcuxpresso/mcuxsdk-core` | `a910e7645d2d809a3431e1d5f42fca1cdeee69c9` |
| `devices` | `nxp-mcuxpresso/mcux-devices-lpc` | `a38f1d6b6daa9014b3486d8e106af9c35e623e09` |
| `usb` | `nxp-mcuxpresso/mcuxsdk-middleware-usb` | `2289f6c8ce0d07e57421ed9b50e2a82d0c38568b` |
| `cmsis` | `nxp-mcuxpresso/mcu-sdk-cmsis` | `e07cca54712c65a938f41a3e72fbfcb20e2f864a` |
| `component` | `nxp-mcuxpresso/mcux-component` | `c4fba0f97e0c889b9235b53c686d2d2dcc5defa4` |

The corresponding upstream license texts are in `third_party/licenses`.
The Huntsman optical transport uses `core/drivers/lpc_dma/fsl_dma.{c,h}` from the
same pinned core revision (Git blobs `1368c32a5ee68ed70e8a0739d4cb82ae4c219dd8`
and `379afb1f1b867b023ef5b3867a784a3ee3809390`). This is the LPC descriptor DMA
driver; the older unused `core/drivers/dma` directory is not selected.
Application code is covered by the repository's GPL-2.0 license; vendored
files retain their original SPDX notices and licenses.

The flash integration includes `core/drivers/iap1/fsl_iap.c` and its
four headers from the same pinned core revision. Their Git blob hashes match
upstream exactly. The ROM wrapper source is **not compiled**.
The controller-based dumper uses the SDK's flash
register definitions and status constants, with a board-owned bounded
read-command adapter checked against the original register transactions.
Settings and calibration share an independent two-tail-page erase/program
adapter, compared with executed original instructions and supplemented with
CMD5 erased-page verification. No SDK ROM-call erase/program
or FFR-write routine is linked. Upstream changelogs and license Markdown remain
unchanged snapshots of these pinned revisions, not project status documents.
