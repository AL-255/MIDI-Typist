# Repository rules

## Incremental commits and pushes

- Commit and push meaningful, validated checkpoints as work progresses; do not
  accumulate an entire implementation in an uncommitted working tree.
- Use the active feature branch. Do not merge into or push changes to `main`
  without an explicit request. Never force-push as part of this workflow.
- Run checks appropriate to each checkpoint and describe unfinished work
  honestly. Keep generated artifacts, private data and firmware dumps out of
  commits. Preserve unrelated user changes.
- Verify the push succeeded and report its branch and commit. If it fails,
  retain the local commit and report the actual failure.
- Prioritize an installable firmware and a verified flashing/recovery path.
  Use focused checks for changed behavior; do not repeatedly expand or run the
  entire offline audit suite instead of progressing to hardware validation.
  Flash boundaries, device identity, transfer integrity and recovery are release
  gates. Unfinished application features must be documented, not hidden behind
  claims of a complete port or used to postpone all testable firmware delivery.

## Latest implementation only

- Keep one current custom firmware implementation and its matching GUI. Remove
  superseded source, build variants, entry points, protocol fallbacks, save-format
  migrations, and repository-local backups when replacing them.
- Do not support older custom firmware or host-profile formats. Reject unsupported
  inputs explicitly; do not silently reinterpret them. Unrecognized device saves
  follow the current bounded cold-start policy.
- Retain tests, build tooling, the portable reference board, and pinned upstream
  dependencies needed by the current implementation. Preserve upstream licenses.
- Factory Razer conversion/restoration through the GUI is supported; it is not
  backward compatibility with old custom firmware. Never bundle factory images.
- This rule applies to the checkout and release artifacts, not rewriting Git history.

## Documentation describes the latest build only

- Keep all project-authored Markdown aligned with the latest complete firmware
  build and matching GUI. Update or remove obsolete descriptions in
  place; do not retain historical status, development snapshots, old artifact
  hashes, flash-session narratives or superseded validation results.
- State the complete build preset, current behavior and actual verification
  limits consistently. Never present modeled tests as physical validation.
- Keep useful design rationale, original-firmware reference facts and supported
  factory behavior only where they explain the current implementation.
- Check relative documentation links after moving or deleting a document.
- Build documentation with `python tools/build_docs.py` using the pinned
  `docs/requirements.txt` environment. Sphinx warnings are errors; add every
  public page to `docs/index.rst`. Keep generated HTML and private data out of Git.
- Use one authoritative guide per topic: manual for operation, BUILDING for
  commands, TELEMETRY for wire fields, DEVICE_CONFIG_STORAGE for persistence,
  and VALIDATION for evidence limits. Prefer links over duplicated tables.
- Keep [`docs/TELEMETRY.md`](docs/TELEMETRY.md) complete: update it whenever a
  stream, magic, field, rate, checksum or text reply changes, and keep the
  per-stream documents it links to in step.
- Preserve upstream SDK documentation, license notices and provenance; do not
  rewrite vendored material to satisfy the project-documentation rule.
- Use `huntsman` for complete physical-board build examples and `simulator`
  for the SDK-free reference port. Do not add historical preset aliases;
  artifact names and USB identities are board contracts, not project branding.
- Distinguish portable application behavior from Huntsman-specific addresses,
  timing, sensor counts and host protocols. Update the porting guide when a
  public board contract changes, and check its examples against current headers.

## Physical-key mapping contract

- Every platform must provide a repository-owned default keyboard mapping
  configuration, indexed by its physical key/sensor identity and layout. Do
  not scatter default transmitted keycodes through scan, USB or GUI code.
- Keep a distinct mapping step between physical key detection and outgoing
  keyboard reports. Scan geometry, calibration, thresholds, physical labels
  and MIDI note assignments must not change when a keyboard key is remapped.
- The GUI must offer a keycode drop-down for each remappable key and verify
  device ACK/readback. Device-local flash persistence, not a host background
  process, makes the mapping survive unplug/replug. ACK alone is not proof
  that a setting was saved.
- Fn itself and all Fn-layer combinations/system controls remain immutable.
  Resolve them from physical key identity before considering a user mapping.
  Remapping a base-layer key must not move its Fn action or alter its label.
- Support duplicate destinations without releasing a transmitted key while
  another physical source still holds it. Mapping edits must release prior
  outputs and require neutral before rearming; do not leave stale keycodes held.
- Store mappings with the complete board/layout-bound configuration using
  checked, atomic snapshots and verified storage ownership. Do not allocate
  extra flash pages, truncate settings or overwrite factory data to make a new
  mapping format fit. Validate capacity for every supported layout.

## Firmware and data boundaries

- Define factory settings and tunable thresholds, timings, normalization,
  colors and default note maps in `firmware/app/include/defaults.h`; do not
  duplicate them in C files or host tools. Python tools read that header via
  `tools/firmware_defaults.py`. Keep hardware/protocol constants in their
  owning headers. Add validation for new defaults and preserve saved profiles.
- Keep the original extraction read-only. Do not consult the broken sibling
  implementation or commit original firmware, disassembly, private device
  dumps, serial-number data or credentials.
- Keep the external MonsGeek M1 firmware-recovery repository read-only too.
  M1 installation requires a verified application-only updater and recovery
  path. The current `m1_development.elf` alone is not a flashing procedure.
  Incomplete power/transport features do not by themselves forbid an explicitly
  labelled experimental build once flashing and recovery are established.
  Require vendor ID2949 for a factory application, not a shared USB PID.
  For custom M1 firmware, require the current SysEx build target on a control
  port bound through ALSA/sysfs to the selected physical USB device. Never
  substitute a friendly MIDI name or another connection's cached build identity.
  Never probe its destructive boot-entry
  command. Do not enable M1 flashing or allocate profile pages without proving
  its application/update and factory-calibration boundaries. M1's custom profile
  reservation is now 0x08027000/0x08027800, the last two application pages; its
  writer, save gate and foreground autosave flow have offline audits; validate
  their power/quiescence/resume behavior on hardware before device use. Keep the entire
  load image below 0x08027000 and execute its flash transaction/SDK code from
  SRAM. Stock settings at/above 0x08028000 and calibration at 0x08032000/0x08032800
  remain protected. Factory-bootloader reflashing erases both custom slots.
- Implement only the application. Preserve bootloader, primary settings and
  serial-number storage, factory/security data and secondary-ASIC firmware.
  On Huntsman, calibration and profile RESET may modify only 0x78000/0x78200
  (verified FF inside the original allocator's free block). On M1, only the
  application-tail reservation above is writable for profiles. The M1 experimental
  startup may program only the factory IAP magic at 0x08004800 in an already
  erased boot-flag page, from SRAM; it must not erase bootloader metadata or code.
  The authorized stock-to-custom entry command resets stock user settings, but
  must leave factory sensor-calibration pages untouched. Do not execute RESET on a
  user's saved calibration merely to test it.
- Use the supplied updater, vendored as the `third_party/huntsman_updater`
  submodule, for application flashing; the GUI uses the private
  `tools/device_flash_service.py` worker and selected model adapter.
  Application flashes are authorized for
  this device; tests/builds must not implicitly flash or reset it.
  Manual forced bootloader recovery is not a routine test strategy.
- The GUI is the only supported PC application. Use versioned MIDI SysEx on the
  dedicated control cable; do not add serial interfaces or standalone device CLIs.
- Use pinned official vendor SDKs for USB and peripheral integration: NXP for
  LPC55, Artery for AT32F405. Keep SDK selection out of shared application code.
- Preserve build-time Git provenance: refresh it on incremental builds, mark
  dirty/unversioned sources honestly, and never substitute the host checkout's
  current commit for the connected firmware's identity.
- Run relevant native tests and the complete application build before handoff.
  Keep device-dependent checks separate and report what was actually verified.
