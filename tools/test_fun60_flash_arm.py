"""Execute FUN60's slot writer and official Artery flash driver offline.

Models busy/status/erase/program registers, not physical flash timing or power.
Asserts that ALL code executed while busy is in RAM and all writes/erases stay
inside the two reserved application sectors. Never touches a USB device.
"""
from pathlib import Path
import struct
import sys
from elftools.elf.elffile import ELFFile
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                              UC_ARM_REG_PRIMASK, UC_CPU_ARM_CORTEX_M4)

FLASH=0x40023c00
BASE=0x08027000
PAGE=2048
SCRATCH=0x20010000
RETURN=0x2001f000


class Flash:
    def __init__(self,path):
        self.cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for base,size in ((0x08000000,0x40000),(0x20000000,0x20000),(0x40023000,0x1000)):
            self.cpu.mem_map(base,size)
        self.cpu.mem_write(0x08000000,b'\xa5'*0x40000)
        with Path(path).open('rb') as stream:
            elf=ELFFile(stream)
            for segment in elf.iter_segments():
                if segment['p_type']=='PT_LOAD' and segment['p_filesz']:
                    self.cpu.mem_write(segment['p_vaddr'],segment.data())
            self.symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        for name in ('mutate','wait_safe_return','flash_sector_erase','flash_word_program',
                     'flash_operation_wait_for','flash_operation_status_get',
                     'flash_unlock','flash_lock','flash_flag_clear'):
            assert 0x20000000<=self.symbols[name]<0x2000e000,name
        assert 'flash_internal_all_erase' not in self.symbols
        self.before=bytes(self.cpu.mem_read(0x08000000,0x40000))
        self.status=0;self.ctrl=128;self.address=0;self.unlocks=[]
        self.erases=[];self.programs=[];self.busy=0;self.pending=None
        self.fail_program=-1;self.bad_erase=False
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_reg,begin=FLASH,end=FLASH+0x17)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write_reg,begin=FLASH,end=FLASH+0x17)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write_flash,begin=0x08000000,end=0x0803ffff)
        self.cpu.hook_add(UC_HOOK_CODE,self.code,begin=0x08000000,end=0x0803ffff)

    def code(self,cpu,address,size,_):assert not self.busy,hex(address)
    def store32(self,address,value):self.cpu.mem_write(address,struct.pack('<I',value))
    def read_reg(self,cpu,access,address,size,value,_):
        assert size==4
        if address==FLASH+12:
            if self.busy:
                self.busy-=1
                if not self.busy:
                    operation,target=self.pending
                    if operation=='erase':
                        if not self.bad_erase:cpu.mem_write(target,b'\xff'*PAGE)
                        self.status=32
                    else:self.status=4 if len(self.programs)-1==self.fail_program else 32
            value=1 if self.busy else self.status
        elif address==FLASH+16:value=self.ctrl
        elif address==FLASH+20:value=self.address
        else:value=0
        self.store32(address,value)

    def write_reg(self,cpu,access,address,size,value,_):
        assert size==4
        if address==FLASH+4:
            self.unlocks.append(value)
            if self.unlocks[-2:]==[0x45670123,0xcdef89ab]:self.ctrl&=~128
        elif address==FLASH+12:self.status&=~value
        elif address==FLASH+20:
            assert value in (BASE,BASE+PAGE),hex(value)
            self.address=value
        elif address==FLASH+16:
            assert not value&(4|16|32),hex(value)  # no mass or option-byte operations
            self.ctrl=value
            if value&64:
                assert not value&128 and value&2 and not self.busy
                assert self.address in (BASE,BASE+PAGE)
                assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
                self.erases.append(self.address);self.busy=4
                self.pending=('erase',self.address);self.ctrl&=~64

    def write_flash(self,cpu,access,address,size,value,_):
        assert size==4 and not address%4 and BASE<=address<BASE+2*PAGE
        assert self.ctrl&1 and not self.ctrl&128 and not self.busy
        assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        old=struct.unpack('<I',cpu.mem_read(address,4))[0]
        assert old&value==value
        self.programs.append(address);self.busy=4;self.pending=('program',address)

    def call(self,name,*args):
        self.cpu.reg_write(UC_ARM_REG_SP,0x2001e000)
        self.cpu.reg_write(UC_ARM_REG_LR,RETURN|1)
        for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args):self.cpu.reg_write(reg,value)
        self.cpu.emu_start(self.symbols[name]|1,RETURN,count=2000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC)==RETURN,name
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def preserve(self):
        after=bytes(self.cpu.mem_read(0x08000000,0x40000))
        assert after[:0x27000]==self.before[:0x27000]
        assert after[0x28000:]==self.before[0x28000:]


def main(path):
    data=bytes(range(256))*2
    for slot in (0,1):
        flash=Flash(path);flash.cpu.mem_write(SCRATCH,data)
        # Reject every unsafe public input without touching the controller.
        for args in ((2,SCRATCH,512),(0xffffffff,SCRATCH,512),(slot,0,512),
                     (slot,SCRATCH,0),(slot,SCRATCH,511),(slot,SCRATCH,2052)):
            assert flash.call('fun60_flash_write',*args)==0x30001
        assert not flash.unlocks and not flash.erases and not flash.programs
        flash.cpu.reg_write(UC_ARM_REG_PRIMASK,slot)
        assert flash.call('fun60_flash_write',slot,SCRATCH,len(data))==0
        assert flash.cpu.reg_read(UC_ARM_REG_PRIMASK)==slot
        assert flash.erases==[BASE+PAGE*slot]
        assert flash.programs==list(range(BASE+PAGE*slot,BASE+PAGE*slot+512,4))
        assert flash.ctrl&128
        assert bytes(flash.cpu.mem_read(BASE+PAGE*slot,PAGE))==data+b'\xff'*(PAGE-512)
        assert flash.call('fun60_flash_read',slot,SCRATCH+2048,512)==0
        assert bytes(flash.cpu.mem_read(SCRATCH+2048,512))==data
        assert flash.call('fun60_flash_erase',slot)==0
        assert bytes(flash.cpu.mem_read(BASE+PAGE*slot,PAGE))==b'\xff'*PAGE
        flash.preserve()
    for failed_word in (0,1,63,127):
        flash=Flash(path);flash.cpu.mem_write(SCRATCH,data);flash.fail_program=failed_word
        assert flash.call('fun60_flash_write',1,SCRATCH,512)!=0
        assert len(flash.programs)==failed_word+1
        erases=list(flash.erases)
        assert flash.call('fun60_flash_write',0,SCRATCH,512)==0x30003
        assert flash.call('fun60_flash_erase',0)==0x30003
        assert flash.erases==erases and flash.ctrl&128
        assert bytes(flash.cpu.mem_read(BASE,PAGE))==b'\xa5'*PAGE
        flash.preserve()
    flash=Flash(path);flash.bad_erase=True
    assert flash.call('fun60_flash_erase',0)==0x30002
    assert not flash.programs and flash.ctrl&128;flash.preserve()
    print('PASS: FUN60 official ARM flash driver, RAM execution, owned-sector bounds, verification, IRQ restoration, failure latch')


if __name__=='__main__':main(sys.argv[1])
