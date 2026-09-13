"""Read-only execution of hash-pinned production routines for differential tests.

Only ordinary memory and explicitly supplied call boundaries are modeled.
This is not a board emulator and never opens a device or writes the reference.
"""
import hashlib
import struct
from collections import deque
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M33, UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
                               UC_ARM_REG_LR, UC_ARM_REG_PC)

REFERENCE_SHA256 = "d8c0268529e34a9f17ce6e806f062b5d4e41a21f631d3ba6690faa06d6df3d27"
RETURN = 0x2003f000


class ProductionArm:
    def __init__(self, reference):
        image = Path(reference).read_bytes()
        assert hashlib.sha256(image).hexdigest() == REFERENCE_SHA256
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        self.cpu.mem_map(0x20000000, 0x40000)
        self.cpu.mem_map(0x04000000, 0x8000)
        self.cpu.mem_write(0x20000000, image)
        self.stubs = {}
        self.trace = deque(maxlen=16)
        self.hooked_stubs = set()
        # Execute the actual scatter-load table/decompressor, not inferred
        # layouts or tables from a previous reconstruction.
        for offset in range(0x1e584, 0x1e5c4, 16):
            source, destination, length, entry = struct.unpack_from("<4I", image, offset)
            self.call(entry, source, destination, length)

    def read(self, address, length=1):
        return bytes(self.cpu.mem_read(address, length))

    def byte(self, address):
        return self.read(address)[0]

    def write(self, address, data):
        self.cpu.mem_write(address, bytes(data))

    def code(self, cpu, address, size, _):
        self.trace.append(address)
        if address in self.stubs:
            args = tuple(cpu.reg_read(reg) for reg in
                         (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
            result = self.stubs[address](*args)
            cpu.reg_write(UC_ARM_REG_R0, result or 0)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def call(self, entry, *args):
        # Callers install boundary stubs after construction. Hook those
        # addresses only; ordinary production instructions execute natively.
        for address in self.stubs.keys() - self.hooked_stubs:
            self.cpu.hook_add(UC_HOOK_CODE, self.code, begin=address, end=address)
            self.hooked_stubs.add(address)
        self.trace.clear()
        self.cpu.reg_write(UC_ARM_REG_SP, 0x04007f00)
        self.cpu.reg_write(UC_ARM_REG_LR, RETURN | 1)
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.cpu.reg_write(reg, value)
        for i, value in enumerate(args[4:]):
            self.write(0x04007f00 + i * 4, struct.pack('<I', value))
        self.cpu.emu_start(entry | 1, RETURN, count=2000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == RETURN, list(map(hex, self.trace))
        return self.cpu.reg_read(UC_ARM_REG_R0)
