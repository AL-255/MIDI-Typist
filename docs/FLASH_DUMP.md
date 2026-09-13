# Bounded flash read protocol

The application retains a read-only `dump read ID ADDRESS` diagnostic command
for protocol and flash-controller audits. It is not a desktop application or a
GUI backup/restore feature. The GUI is the only supported PC application.

A request returns one HBD1 payload inside a typed MIDI SysEx DUMP message.
Reads use the original controller's READ_SINGLE_WORD sequence, not a ROM API,
and cannot erase or program. Each response contains 64 bytes plus per-16-byte
status fields and CRC. Failed words contain zero placeholders, not verified data.

Addresses must be 64-byte aligned and below both `0x7f400` and the detected
flash size minus 10 KiB. ROM, MMIO, PFR/security and secondary-controller
storage are excluded. A timeout latches a fault and prevents further reads.

The [telemetry reference](TELEMETRY.md#flash-dump-dump-read) defines every field.
The offline `tools/test_flash_dump_arm.py` audit checks bounds, error priority,
CRC, busy ownership and controller transactions. It never opens a device.

Any manually obtained readback can contain firmware, calibration or serial
data. Keep it private and excluded from Git; an error-containing dump is not a
restoration image. Only custom pages `0x78000/0x78200` are writable by
[settings storage](DEVICE_CONFIG_STORAGE.md).
