#!/usr/bin/env python3
"""Execute the linked USB code offline. This is NOT a PHY/hardware emulator.

Requires unicorn and pyelftools. Board clock/delay/IRQ/PHY recovery is stubbed;
the actual ARM class, DCI, IP3511 and application callbacks are executed.
Only the USB register semantics exercised by these tests are modeled.
"""
import argparse
import struct
from collections import deque

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UcError
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
from unicorn.arm_const import UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
from unicorn.arm_const import UC_CPU_ARM_CORTEX_M33


USB = 0x40094000
RAM = 0x40100000
RETURN = 0x2003F000


class UsbArm:
    def __init__(self, elf_path):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        for address, size in ((0x04000000, 0x8000), (0x20000000, 0x40000),
                              (0x40000000, 0x110000), (0xE0000000, 0x100000)):
            self.cpu.mem_map(address, size)
        with open(elf_path, "rb") as stream:
            elf = ELFFile(stream)
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD":
                    self.cpu.mem_write(segment["p_vaddr"], segment.data())
            entries = list(elf.get_section_by_name(".symtab").iter_symbols())
            self.symbols = {s.name: s["st_value"] for s in entries}
            self.sizes = {s.name: s["st_size"] for s in entries}
        self.registers = {USB + 12: RAM}
        self.reset_requests = 0
        self.trace = deque(maxlen=24)
        self.stubs = {self.symbols[name] & ~1 for name in
                      ("board_usb_clock_init", "usb_errata_init", "board_delay_ms",
                       "board_usb_isr_enable", "usb_errata_bus_reset", "board_enter_bootloader") if name in self.symbols}
        # Only stub entry points need a Python instruction callback. Running
        # one on every arithmetic/load instruction dwarfs actual emulation.
        # MMIO and Device-memory alignment checks remain fully instrumented.
        for address in self.stubs:
            self.cpu.hook_add(UC_HOOK_CODE, self.code, begin=address, end=address)
        self.cpu.hook_add(UC_HOOK_MEM_READ, self.read_register, begin=USB, end=USB + 0xFFF)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.write_register, begin=USB, end=USB + 0xFFF)
        self.cpu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self.check_usb_alignment,
                          begin=RAM, end=RAM + 0x3FFF)

    def check_usb_alignment(self, cpu, access, address, size, value, _):
        # USB SRAM is Device memory in the default ARM memory map. Unicorn
        # does not enforce that attribute, so enforce its alignment here.
        if size > 1 and address % size:
            raise RuntimeError(f"unaligned USB SRAM access at {address:08x}, size={size}, "
                               f"PC={cpu.reg_read(UC_ARM_REG_PC):08x}")

    def u32(self, address):
        return struct.unpack("<I", self.cpu.mem_read(address, 4))[0]

    def put32(self, address, value):
        self.cpu.mem_write(address, struct.pack("<I", value))

    def code(self, cpu, address, size, _):
        self.trace.append(address)
        if address == self.symbols["board_enter_bootloader"] & ~1:
            self.reset_requests += 1  # record intent; never perform a reset here
            # This function is noreturn: its caller has no return epilogue.
            cpu.reg_write(UC_ARM_REG_PC, RETURN | 1)
            return
        if address in self.stubs:
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def read_register(self, cpu, access, address, size, value, _):
        assert size == 4, (hex(address), size)
        self.put32(address, self.registers.get(address, 0))

    def write_register(self, cpu, access, address, size, value, _):
        assert size == 4, (hex(address), size)
        old = self.registers.get(address, 0)
        if address == USB:  # preserve read-only speed/VBUS, implement W1C change/setup
            w1c = 0x0F000100
            readonly = 0x10C00000
            value = ((old & readonly) | (old & w1c & ~value) |
                     (value & ~(readonly | w1c)))
        elif address == USB + 0x20:  # INTSTAT W1C
            value = old & ~value
        elif address == USB + 0x14:  # EPSKIP completes immediately in this model
            for index in range(12):
                if value & (1 << index):
                    odd = (self.registers.get(USB + 0x18, 0) >> index) & 1
                    entry = self.registers[USB + 8] + index * 8 + odd * 4
                    self.put32(entry, self.u32(entry) & ~0x80000000)
            value = 0
        self.registers[address] = value

    def call(self, name, *args):
        self.trace.clear()
        self.cpu.reg_write(UC_ARM_REG_SP, 0x04007F00)
        self.cpu.reg_write(UC_ARM_REG_LR, RETURN | 1)
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.cpu.reg_write(reg, value)
        for index, value in enumerate(args[4:]):
            self.put32(0x04007F00 + index * 4, value)
        try:
            self.cpu.emu_start(self.symbols[name] | 1, RETURN, count=2000000)
        except UcError as error:
            raise RuntimeError(f"{name}: {error}; PC={self.cpu.reg_read(UC_ARM_REG_PC):08x}; "
                               f"trace={[hex(x) for x in self.trace]}") from error
        assert self.cpu.reg_read(UC_ARM_REG_PC) == RETURN, (name, "instruction budget exhausted", list(self.trace))
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def interrupt(self):
        handle = self.u32(self.symbols["s_device"])
        assert handle, "USB class initialization failed"
        self.call("USB1_IRQHandler")

    def reset(self, high_speed=False):
        self.registers[USB] = (self.registers[USB] & ~0x00C00000) | (0x00800000 if high_speed else 0x00400000) | 0x04000000
        self.registers[USB + 0x20] = 0x80000000
        self.interrupt()

    def setup(self, request):
        eplist = self.registers[USB + 8]
        setup_buffer = RAM + ((self.u32(eplist + 4) & 0x7FF) << 6)
        self.cpu.mem_write(setup_buffer, request)
        self.registers[USB] |= 0x100
        self.registers[USB + 0x20] = 1
        self.interrupt()

    def packet(self, index):
        control_in = self.u32(self.packet_entry(index))
        assert control_in & 0x80000000, f"endpoint index {index} was not armed: {control_in:08x}"
        assert not control_in & 0x20000000, f"endpoint index {index} stalled"
        length = (control_in >> 11) & 0x7FFF
        data_buffer = RAM + ((control_in & 0x7FF) << 6)
        return data_buffer, length

    def packet_entry(self, index):
        eplist = self.registers[USB + 8]
        entry = eplist + index * 8
        if index >= 2:
            # Hardware consumes ping-pong buffers in EPINUSE order. Choosing
            # the first active buffer reorders a long double-buffered stream.
            entry += ((self.registers.get(USB + 0x18, 0) >> index) & 1) * 4
        return entry

    def complete(self, index, payload=None):
        address, length = self.packet(index)
        if payload is not None:
            assert len(payload) <= length
            self.cpu.mem_write(address, bytes(payload))
            remaining = length - len(payload)
        else:
            remaining = 0
        entry = self.packet_entry(index)
        self.put32(entry, (self.u32(entry) & ~0x83FFF800) | (remaining << 11))
        if index >= 2:
            self.registers[USB + 0x18] = self.registers.get(USB + 0x18, 0) ^ (1 << index)
        self.registers[USB + 0x20] = 1 << index
        self.interrupt()

    def control_in(self, request):
        self.setup(request)
        result = bytearray()
        wanted = struct.unpack_from("<H", request, 6)[0]
        for _ in range(128):
            address, length = self.packet(1)
            result.extend(self.cpu.mem_read(address, length))
            self.complete(1)
            if length < 64 or len(result) >= wanted:
                break
        else:
            raise AssertionError("control IN exceeded packet budget")
        self.complete(0, b"")  # status OUT
        return bytes(result)

    def control_out(self, request, payload=b""):
        self.setup(request)
        while payload:
            _, length = self.packet(0)
            part, payload = payload[:length], payload[length:]
            assert part, "no OUT data space"
            self.complete(0, part)
        self.complete(1)  # status IN


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf")
    parser.add_argument("--expect-reset-alignment-fault", action="store_true",
                        help="negative control: require the previously flashed bus-reset fault")
    parser.add_argument("--expect-libc-alignment-fault", action="store_true",
                        help="negative control: require prebuilt memcpy's odd-tail halfword fault")
    args = parser.parse_args()
    if args.expect_libc_alignment_fault:
        dev = UsbArm(args.elf)
        try:
            dev.call("memcpy", RAM + 0x3400, 0x04005000, 43)
        except RuntimeError as error:
            assert "unaligned USB SRAM access at 40103429, size=2" in str(error), str(error)
            print(f"PASS libc negative control: {error}")
            return
        raise AssertionError("expected memcpy alignment fault did not occur")
    if args.expect_reset_alignment_fault:
        dev = UsbArm(args.elf)
        dev.call("usb_composite_init")
        try:
            dev.reset()
        except RuntimeError as error:
            assert "unaligned USB SRAM access at 401000a7, size=2" in str(error), str(error)
            print(f"PASS negative control: {error}")
            return
        raise AssertionError("expected reset alignment fault did not occur")
    for high_speed in (False, True):
        dev = UsbArm(args.elf)
        dev.call("usb_composite_init")
        dev.reset(high_speed)
        response = dev.control_in(bytes.fromhex("80 06 00 01 00 00 40 00"))
        assert len(response) == 18 and response[8:12] == bytes.fromhex("32 15 b0 02"), response.hex()
        print(f"PASS {'HS' if high_speed else 'FS'} GET_DESCRIPTOR(device): {response.hex()}")
        dev.control_out(bytes.fromhex("00 05 07 00 00 00 00 00"))
        assert dev.registers[USB] & 0x7F == 7
        config = dev.control_in(bytes.fromhex("80 06 00 02 00 00 ff 00"))
        assert len(config) == 184, len(config)
        print("PASS SET_ADDRESS and full configuration descriptor")
        for index in range(8):
            response = dev.control_in(struct.pack("<BBHHH", 0x80, 6, 0x300 | index,
                                                  0x409 if index else 0, 255))
            assert response[0] == len(response) and response[1] == 3
            # Actual object bounds must agree too; accepting an overlong string
            # merely because its header requests it would hide an over-read.
            assert len(response) == dev.sizes[f"s_string{index}"], (index, len(response))
        print("PASS string descriptor transfers and bounds")
        assert config[15:17] == b"\x00\x00", "NKRO-only interface must not advertise boot protocol"
        for interface, symbol in ((0, "s_keyboard_report_descriptor"),
                                   (3, "s_updater_report_descriptor")):
            report = dev.control_in(struct.pack("<BBHHH", 0x81, 6, 0x2200, interface, 255))
            assert report == bytes(dev.cpu.mem_read(dev.symbols[symbol], dev.sizes[symbol]))
        print("PASS keyboard/updater HID report descriptor requests")
        dev.control_out(bytes.fromhex("00 09 01 00 00 00 00 00"))
        # Enumeration succeeding does not prove that class endpoints opened.
        for index in (4,):  # MIDI OUT must be receiving
            dev.packet(index)
            # NXP DCI ABI: callbacks begin at +20, stride 12 for this build.
            assert dev.u32(dev.u32(dev.symbols["s_device"]) + 20 + index * 12), (index, "missing endpoint callback")
        print("PASS SET_CONFIGURATION")
        assert dev.control_in(bytes.fromhex("80 08 00 00 00 00 01 00")) == b"\x01"
        for interface in range(4):
            assert dev.control_in(struct.pack("<BBHHH", 0x81, 10, 0, interface, 1)) == b"\x00"
            dev.control_out(struct.pack("<BBHHH", 0x01, 11, 0, interface, 0))
        print("PASS GET_CONFIGURATION and all GET_INTERFACE/SET_INTERFACE(0)")
        dev.control_out(bytes.fromhex("21 0a 00 00 00 00 00 00"))  # SET_IDLE(0)
        assert dev.control_in(bytes.fromhex("a1 02 00 00 00 00 01 00")) == b"\x00"
        assert dev.control_in(bytes.fromhex("a1 01 00 02 00 00 01 00")) == b"\x00"
        dev.control_out(bytes.fromhex("21 09 00 02 00 00 01 00"), b"\x02")
        assert dev.call("usb_keyboard_leds") == 2
        assert dev.control_in(bytes.fromhex("a1 01 00 02 00 00 01 00")) == b"\x02"
        dev.control_out(bytes.fromhex("21 09 00 02 00 00 01 00"), b"\x00")
        assert dev.call("usb_keyboard_leds") == 0
        print("PASS keyboard HID Caps Lock LED output report and state")
        dev.complete(4, bytes.fromhex("09 90 3c 7f"))
        dev.packet(4)  # OUT rearmed by the installed callback
        assert dev.call("usb_midi_send", 9, 0x90, 60, 127)
        address, length = dev.packet(5)
        assert length == 4 and bytes(dev.cpu.mem_read(address, 4)) == bytes.fromhex("09 90 3c 7f")
        dev.complete(5)
        assert dev.call("usb_midi_send", 8, 0x80, 60, 0), "MIDI IN busy flag did not clear"
        dev.complete(5)
        print("PASS MIDI IN/OUT and rearm")

        assert 'usb_cdc_write' not in dev.symbols

        dev.cpu.mem_write(0x04005000, bytes.fromhex("02 00 01") + bytes(13))
        assert dev.call("usb_keyboard_send", 0x04005000)
        address, length = dev.packet(3)
        assert length == 30 and bytes(dev.cpu.mem_read(address, 3)) == bytes.fromhex("02 00 01")
        dev.complete(3)
        print("PASS NKRO report send/completion")

        for opcode, expected_length in ((0x81, 2), (0x82, 22), (0x83, 2), (0x84, 1),
                                        (0x86, 2), (0x87, 4), (0x9F, 4), (0xC0, 2)):
            frame = bytearray(90)
            frame[7] = frame[88] = opcode
            dev.control_out(bytes.fromhex("21 09 00 03 03 00 5a 00"), frame)
            response = dev.control_in(bytes.fromhex("a1 01 00 03 03 00 5a 00"))
            assert len(response) == 90 and response[0] == 2 and response[5] == expected_length, response.hex()
            checksum = 0
            for value in response[2:88]:
                checksum ^= value
            assert response[88] == checksum
        print("PASS updater 90-byte SET_REPORT/GET_REPORT queries")
        dev.reset(high_speed)
        response = dev.control_in(bytes.fromhex("80 06 00 01 00 00 40 00"))
        assert len(response) == 18
        print("PASS reset after configured transfers")

        for abort in ("setup", "reset", None):
            boot = UsbArm(args.elf)
            boot.call("usb_composite_init")
            boot.reset(high_speed)
            boot.control_out(bytes.fromhex("00 05 07 00 00 00 00 00"))
            boot.control_out(bytes.fromhex("00 09 01 00 00 00 00 00"))
            frame = bytearray(90)
            frame[5], frame[7], frame[8] = 2, 4, 1  # updater enter_bootloader_report()
            boot.setup(bytes.fromhex("21 09 00 03 03 00 5a 00"))
            boot.complete(0, frame[:64])
            boot.complete(0, frame[64:])
            boot.put32(boot.symbols["s_milliseconds"], 1000)
            boot.call("usb_composite_service")
            assert boot.reset_requests == 0, "reset before status ACK"
            if abort == "setup":
                boot.control_in(bytes.fromhex("80 06 00 01 00 00 40 00"))
            elif abort == "reset":
                boot.reset(high_speed)
            else:
                boot.complete(1)  # status IN completes; start the deferral now
                boot.put32(boot.symbols["s_milliseconds"], 1019)
                boot.call("usb_composite_service")
                assert boot.reset_requests == 0, "deferral shorter than 20 ms"
                # A later request must not restart the already-ACKed deadline.
                boot.control_in(bytes.fromhex("80 08 00 00 00 00 01 00"))
            boot.put32(boot.symbols["s_milliseconds"], 1020)
            boot.call("usb_composite_service")
            assert boot.reset_requests == (1 if abort is None else 0), abort
        print("PASS updater: no reset without status ACK, 20 ms post-ACK deferral, SETUP/reset aborts")

    copies = UsbArm(args.elf)
    for length in (0, 1, 2, 3, 4, 5, 7, 9, 15, 31, 43, 63, 64, 65, 90, 127, 128, 511, 512):
        payload = bytes(i % 251 for i in range(length))
        for source_offset in range(4):
            for destination_offset in range(4):
                # Both sides in Device memory: catches unaligned loads too.
                source = RAM + 0x3000 + source_offset
                destination = RAM + 0x3400 + destination_offset
                copies.cpu.mem_write(source, payload)
                copies.cpu.mem_write(destination - 1, b"\xa5" * (length + 2))
                assert copies.call("__wrap_memcpy", destination, source, length) == destination
                assert bytes(copies.cpu.mem_read(destination - 1, length + 2)) == b"\xa5" + payload + b"\xa5"
    print("PASS Device-memory memcpy: all pointer alignments, odd tails, bounds, return value through 512 bytes")


if __name__ == "__main__":
    main()
