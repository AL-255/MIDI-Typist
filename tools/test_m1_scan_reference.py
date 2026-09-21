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
from unicorn.arm_const import UC_ARM_REG_R4, UC_ARM_REG_R6, UC_ARM_REG_R7
from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R5, UC_ARM_REG_R10
from firmware_defaults import DEFAULTS as D


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
    # Execute the original startup validity/fallback block, not a reconstructed
    # C implementation. Registers provide one rank/bank and scratch RAM only;
    # stop before later filtering. This is neither a whole-boot nor flash test.
    base=0x20000000;offset=3*12+2*2
    cpu.reg_write(UC_ARM_REG_R4,base);cpu.reg_write(UC_ARM_REG_R6,base+0x1000)
    cpu.reg_write(UC_ARM_REG_R7,2);cpu.mem_write(base+0x1980,bytes((3,)))
    upper=base+0x845c+offset;lower=base+0x8558+offset;sample=base+0x7afe+offset
    for hi in (0,999,1000,4000,4001,21000):
        for lo in (0,1000):
            cpu.mem_write(upper,struct.pack('<H',hi));cpu.mem_write(lower,struct.pack('<H',lo))
            cpu.mem_write(sample,struct.pack('<H',2600))
            cpu.emu_start(0x08005d69,0x08005e86,count=200)
            assert cpu.reg_read(UC_ARM_REG_PC)==0x08005e86
            observed=(struct.unpack('<H',cpu.mem_read(upper,2))[0],struct.unpack('<H',cpu.mem_read(lower,2))[0])
            fallback=hi<D['M1_FACTORY_RELEASE_MIN_RAW'] or hi>D['M1_FACTORY_RELEASE_MAX_RAW'] or lo==0
            assert observed==((2600,2600-D['M1_STARTUP_TRAVEL_RAW']) if fallback else (hi,lo))
    print('PASS private reference replaces out-of-range startup calibration in RAM with sample and sample-minus-700')
    # Original mode-packet branch, ending before its DMA submission. In
    # particular USB selection really carries mode 6 through opcode 0x93.
    for mode in (0,1,2,5,6):
        packet=base+0x10000;state=base+0x11000;runtime=base+0x12000
        cpu.mem_write(packet,bytes(84));cpu.mem_write(state,bytes(32));cpu.mem_write(runtime,bytes(64))
        cpu.mem_write(state+8,bytes((2,0,1)));cpu.mem_write(runtime+30,bytes((mode,)))
        for reg,value in ((UC_ARM_REG_R1,state),(UC_ARM_REG_R4,packet),
                          (UC_ARM_REG_R5,0),(UC_ARM_REG_R10,runtime)):
            cpu.reg_write(reg,value)
        cpu.emu_start(0x080180af,0x080186a2,count=100)
        assert cpu.reg_read(UC_ARM_REG_PC)==0x080186a2
        assert bytes(cpu.mem_read(packet,4))==bytes((0x93,1,mode,mode))
        assert bytes(cpu.mem_read(state+9,2))==bytes((1,0))
    print('PASS private reference mode-packet instructions: BT slots, RF and USB select via 0x93 before status query')
    # Execute the stock quadrature sampler and its actual SDK GPIO reader.
    # Legal complete cycles establish pin order/direction queue identity only;
    # our debounce/overflow policy is intentionally independent.
    for phases,expected in (((1,3,2,0),(1,0)),((2,3,1,0),(0,1))):
        cpu.mem_write(base+0xc29f,bytes(12))
        for phase in phases:
            cpu.mem_write(0x40020810,struct.pack('<I',((phase&1)<<10)|((phase>>1)<<12)|0x800))
            for _ in range(2):
                cpu.reg_write(UC_ARM_REG_SP,base+0x17000);cpu.reg_write(UC_ARM_REG_LR,0x0803f001)
                cpu.emu_start(0x0801a8bd,0x0803f000,count=300)
                assert cpu.reg_read(UC_ARM_REG_PC)==0x0803f000
        observed=(cpu.mem_read(base+0xc2a2,1)[0],cpu.mem_read(base+0xc2a4,1)[0])
        assert observed==expected,(phases,observed)
    print('PASS private reference encoder GPIO pin order and both complete electrical-cycle directions')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf'); parser.add_argument('--reference', required=True)
    args = parser.parse_args(); check(args.elf, args.reference)
