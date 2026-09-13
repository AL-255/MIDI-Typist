# Documentation guide

Start with the illustrated [user manual](../USER_MANUAL.md) for everyday operation,
the [project README](../README.md) for a project overview, and the
[build guide](BUILDING.md) for a clean checkout. The current complete preset is
`huntsman` (alias `keyboard-fn-menu`), emitting GUI telemetry.

## Choose a reading path

| Your task | Read |
| --- | --- |
| Use the supported Huntsman keyboard | [User manual](../USER_MANUAL.md) |
| Build firmware or run the desktop simulator | [Building](BUILDING.md) |
| Add a different keyboard or MCU | [Porting guide](PORTING.md), then [architecture](ARCHITECTURE.md) |
| Choose an owner loop or RTOS task | [Scheduling](SCHEDULING.md) |
| Integrate host tools | [Protocol](MIDI_PROTOCOL.md) and [GUI limits](KEYBOARD_GUI.md) |

Physical layout, flash addresses, USB identities and HKG/HKS/HKL/HBD formats
in feature guides describe Huntsman unless explicitly stated otherwise.
Shared application contracts are documented separately from those wire formats.

## Current behavior and protocols

- [Fn menu](FN_MENU.md): supported hints, trigger-point editor and brightness.
- [Keyboard GUI](KEYBOARD_GUI.md): layout, Schmitt thresholds, MIDI mappings,
  profiles, calibration controls, application flashing and connection handling.
- [Per-key velocity](KEY_VELOCITY.md), [normalization](NORMALIZED_VELOCITY.md)
  and [interval pop filter](MIDI_FILTER.md): independent capture and encoding.
- [MIDI design](MIDI_DESIGN.md) and [USB/GUI protocol](MIDI_PROTOCOL.md).
- [Device telemetry](TELEMETRY.md): every stream, field, rate and text reply the
  application reports, and how hosts consume them.
- [Root/scale selection](MIDI_SCALES.md): portable interval tables, selectors,
  shared note/LED filtering, modal safety and RAM-only selection state.
- [Parallel calibration](CALIBRATION.md): operation, state machine, telemetry fields
  and validation limits.
- [Device storage](DEVICE_CONFIG_STORAGE.md): two-page ownership, calibration
  record layout and write bounds, cold boot, validation, recovery limits and
  serial-number protection.
- [Flash acquisition](FLASH_DUMP.md): read-only dump commands and private backups.
- [Whole scan display](SCAN_STREAM.md), [20-sample capture](LAST_KEY_STREAM.md)
  and [travel lighting](TRAVEL_LIGHTING.md): operation and protocol details.
- [USB integration](USB_DESIGN.md) and [optical/Fn design](KEYBOARD_RECOVERY.md).
- [SDK provenance and licenses](../third_party/ORIGINS.md).

## Application and board ports

- [Architecture](ARCHITECTURE.md): shared application, board contracts and compatibility.
- [Porting guide](PORTING.md): build-manifest and lifecycle examples, sample/key
  contracts, LED/USB/storage ownership, RTOS integration and acceptance checks.
- [Scheduling](SCHEDULING.md): cooperative ownership and the FreeRTOS tradeoff.

Project-authored documentation describes only the latest build. Update or
remove obsolete claims in place; do not append development snapshots. Keep
unverified behavior explicit. This is enforced as a contributor rule in
[AGENTS.md](../AGENTS.md). Vendored SDK documentation retains its upstream content.

## Current evidence boundary

The latest build is validated by native tests and compiled ARM/register models,
including comparison with original editor instructions and number-row colors.
The application passes computer-initiated flashing, matching full-image
readback, live CDC/scan health and unchanged calibration-page comparison.
See [validation limits](CALIBRATION.md#validation-status).
Tests do not establish physical LED appearance, 8 kHz acquisition, calibrated
force/distance or comprehensive USB/DAW compliance, and do not flash hardware.
