#!/usr/bin/env python3
"""Validate the updater-facing invariants of a linked application image."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

APP_BASE = 0x20000000
APP_SIZE = 0x20000
CONFIG_OFFSET = 0x1FC00
CONFIG_PAGE_SIZE = 0x200
STACK_LIMIT = 0x04006000
STACK_TOP = 0x04008000


def elf_symbols_and_data(path: Path, values: dict[str, int] | None = None) -> dict[str, bytes]:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit("validator expects a little-endian ELF32 image")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    section_offset = header[5]
    section_entry_size = header[10]
    section_count = header[11]
    sections = [
        struct.unpack_from("<IIIIIIIIII", data, section_offset + i * section_entry_size)
        for i in range(section_count)
    ]
    result: dict[str, bytes] = {}
    for section in sections:
        section_type, offset, size, link, entry_size = section[1], section[4], section[5], section[6], section[9]
        if section_type != 2 or not entry_size:
            continue
        string_section = sections[link]
        strings = data[string_section[4] : string_section[4] + string_section[5]]
        for pos in range(offset, offset + size, entry_size):
            name_offset, value, symbol_size, _info, _other, symbol_section = struct.unpack_from("<IIIBBH", data, pos)
            if not name_offset:
                continue
            end = strings.find(b"\0", name_offset)
            name = strings[name_offset:end].decode("ascii", errors="replace")
            if values is not None and symbol_section != 0:
                values[name] = value
            if not symbol_size or symbol_section >= len(sections):
                continue
            owner = sections[symbol_section]
            if owner[1] == 8:  # SHT_NOBITS has no initialized bytes in the file.
                continue
            file_offset = owner[4] + value - owner[3]
            result[name] = data[file_offset : file_offset + symbol_size]
    return result


def validate_config_reservation(image: bytes, values: dict[str, int]) -> None:
    expected = {
        "__app_config_start__": APP_BASE + CONFIG_OFFSET,
        "__app_config_slot_a__": APP_BASE + CONFIG_OFFSET,
        "__app_config_slot_b__": APP_BASE + CONFIG_OFFSET + CONFIG_PAGE_SIZE,
        "__app_config_end__": APP_BASE + APP_SIZE,
    }
    if any(values.get(name) != address for name, address in expected.items()):
        raise SystemExit("missing or invalid application configuration reservation")
    load_end = values.get("__app_load_end__", 0)
    if not APP_BASE < load_end <= APP_BASE + CONFIG_OFFSET:
        raise SystemExit("application load image overlaps configuration reservation")
    if len(image) != APP_SIZE or image[CONFIG_OFFSET:] != b"\xff" * (2 * CONFIG_PAGE_SIZE):
        raise SystemExit("fresh updater image must initialize both configuration slots to FF")


def validate_usb_descriptors(symbols: dict[str, bytes]) -> None:
    device = symbols.get("s_device_descriptor", b"")
    config = symbols.get("s_configuration_descriptor", b"")
    keyboard_report = symbols.get("s_keyboard_report_descriptor", b"")
    updater_report = symbols.get("s_updater_report_descriptor", b"")
    if len(device) != 18 or struct.unpack_from("<HH", device, 8) != (0x1532, 0x02B0):
        raise SystemExit("USB device descriptor does not expose 1532:02b0")
    if len(config) < 9 or struct.unpack_from("<H", config, 2)[0] != len(config) or config[4] != 4:
        raise SystemExit("USB configuration descriptor length/interface count is invalid")

    interfaces: dict[int, tuple[int, int]] = {}
    endpoints: dict[int, list[int]] = {}
    current_interface = -1
    offset = 0
    while offset < len(config):
        length = config[offset]
        if length < 2 or offset + length > len(config):
            raise SystemExit(f"malformed USB descriptor at offset {offset}")
        descriptor_type = config[offset + 1]
        if descriptor_type == 4:
            current_interface = config[offset + 2]
            interfaces[current_interface] = (config[offset + 5], config[offset + 4])
            endpoints[current_interface] = []
        elif descriptor_type == 5:
            endpoints.setdefault(current_interface, []).append(config[offset + 2])
        offset += length
    if sorted(interfaces) != list(range(4)):
        raise SystemExit(f"USB interfaces are {sorted(interfaces)}, expected 0..3")
    expected = {
        0: (0x03, [0x81]),
        1: (0x01, []),
        2: (0x01, [0x02, 0x82]),
        3: (0x03, []),
    }
    for interface, (class_code, addresses) in expected.items():
        if interfaces[interface][0] != class_code or endpoints[interface] != addresses:
            raise SystemExit(f"USB interface {interface} does not match composite contract")
    if b"\x95\x70\x81\x02" not in keyboard_report:
        raise SystemExit("NKRO HID report does not contain its 112-bit bitmap")
    if b"\x95\x5a\x09\x01\xb1\x02" not in updater_report:
        raise SystemExit("updater HID report is not a 90-byte feature report")
    for index in range(8):
        descriptor = symbols.get(f"s_string{index}", b"")
        if len(descriptor) < 2 or descriptor[0] != len(descriptor) or descriptor[1] != 3 or len(descriptor) % 2:
            raise SystemExit(f"USB string {index} length/type does not match its ELF object")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    args = parser.parse_args()

    image = args.bin.read_bytes()
    if len(image) != APP_SIZE:
        raise SystemExit(f"binary is {len(image):#x} bytes; expected {APP_SIZE:#x}")

    initial_sp, reset_vector = struct.unpack_from("<II", image)
    if initial_sp != STACK_TOP:
        raise SystemExit(f"initial MSP is {initial_sp:#010x}; expected {STACK_TOP:#010x}")
    if not (APP_BASE <= (reset_vector & ~1) < APP_BASE + CONFIG_OFFSET) or not (reset_vector & 1):
        raise SystemExit(f"reset vector {reset_vector:#010x} is outside the application or not Thumb")
    if STACK_LIMIT >= STACK_TOP:
        raise SystemExit("invalid stack bounds")
    if not args.elf.is_file():
        raise SystemExit("ELF is missing")
    values: dict[str, int] = {}
    validate_usb_descriptors(elf_symbols_and_data(args.elf, values))
    validate_config_reservation(image, values)
    print(f"validated 128 KiB application: MSP={initial_sp:#010x}, reset={reset_vector:#010x}")


if __name__ == "__main__":
    main()
