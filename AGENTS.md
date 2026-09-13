# Repository rules

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

## Firmware and data boundaries

- Define factory settings and tunable thresholds, timings, normalization,
  colors and default note maps in `firmware/app/include/defaults.h`; do not
  duplicate them in C files or host tools. Python tools read that header via
  `tools/firmware_defaults.py`. Keep hardware/protocol constants in their
  owning headers. Add validation for new defaults and preserve saved profiles.
- Keep the original extraction read-only. Do not consult the broken sibling
  implementation or commit original firmware, disassembly, private device
  dumps, serial-number data or credentials.
- Implement only the application. Preserve bootloader, primary settings and
  serial-number storage, factory/security data and secondary-ASIC firmware.
  Calibration and profile RESET may modify only their two documented storage
  pages, 0x78000/0x78200 (verified FF inside the original allocator's free block). Do not execute RESET on a user's saved calibration merely to test it.
- Use the supplied updater, vendored as the `third_party/huntsman_updater`
  submodule, for application flashing; the GUI uses the private
  `tools/device_flash_service.py` worker and selected model adapter.
  Application flashes are authorized for
  this device; tests/builds must not implicitly flash or reset it.
  Manual forced bootloader recovery is not a routine test strategy.
- The GUI is the only supported PC application. Use versioned MIDI SysEx on the
  dedicated control cable; do not add serial interfaces or standalone device CLIs.
- Use the pinned official NXP SDK sources for USB and peripheral integration.
- Preserve build-time Git provenance: refresh it on incremental builds, mark
  dirty/unversioned sources honestly, and never substitute the host checkout's
  current commit for the connected firmware's identity.
- Run relevant native tests and the complete application build before handoff.
  Keep device-dependent checks separate and report what was actually verified.
