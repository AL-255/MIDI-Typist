"""Compare compiled bank wiring with private reference instructions; no USB I/O.

Requires the user's external ID2949/v410 image. No vendor bytes are bundled.
The reference selector and GPIO helpers execute unchanged; only output-latch
effects are modeled. This is not an electrical scan or sensor timing test.
"""
import argparse
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC


def check(elf_path, reference):
    with open(elf_path, 'rb') as stream:
        elf = ELFFile(stream)
        symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('m1_bank_bits')[0]
        section = elf.get_section(symbol['st_shndx'])
        offset = symbol['st_value'] - section['sh_addr']
        wiring = section.data()[offset:offset + symbol['st_size']]
    assert len(wiring) == 6
    data = Path(reference).read_bytes()
    assert 0x16100 <= len(data) <= 0x40000, 'Expected boot-prefixed private reference'
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    cpu.mem_map(0x08000000, 0x40000); cpu.mem_write(0x08000000, data)
    cpu.mem_map(0x20000000, 0x18000); cpu.mem_map(0x40020000, 0x1000)
    writes = []
    def write(cpu, access, address, size, value, user):
        assert size == 4 and address in (0x40020418, 0x40020428)
        writes.append((address, value))
        latch = struct.unpack('<I', cpu.mem_read(0x40020414, 4))[0]
        latch = latch | value if address == 0x40020418 else latch & ~value
        cpu.mem_write(0x40020414, struct.pack('<I', latch))
    cpu.hook_add(UC_HOOK_MEM_WRITE, write, begin=0x40020000, end=0x40020fff)
    for bank in range(7):
        before = 0xa5a55a5a
        cpu.mem_write(0x40020414, struct.pack('<I', before)); writes.clear()
        cpu.reg_write(UC_ARM_REG_R0, bank); cpu.reg_write(UC_ARM_REG_SP, 0x20017000)
        cpu.reg_write(UC_ARM_REG_LR, 0x0803f001)
        cpu.emu_start(0x080132b5, 0x0803f000, count=200)
        assert cpu.reg_read(UC_ARM_REG_PC) == 0x0803f000
        after = struct.unpack('<I', cpu.mem_read(0x40020414, 4))[0]
        assert after & ~0x380 == before & ~0x380
        if bank < 6:
            assert [value for _, value in writes] == [512, 256, 128]
            assert after >> 7 & 7 == wiring[bank], (bank, after >> 7 & 7, wiring[bank])
        else:
            assert not writes and after == before
    print('PASS compiled M1 bank wiring matches executed private reference GPIO writes for all banks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf'); parser.add_argument('--reference', required=True)
    args = parser.parse_args(); check(args.elf, args.reference)
