"""Actual shared journal + SRAM writer + official SDK, synthetic flash only.

Models unlock/W1C/erase/program effects, not physical cells, elapsed timing,
power qualification or the foreground quiescence coordinator. Never opens USB.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_PC, UC_ARM_REG_PRIMASK,
    UC_ARM_REG_BASEPRI, UC_ARM_REG_FAULTMASK, UC_ARM_REG_CONTROL, UC_ARM_REG_IPSR)
from test_m1_hal_arm import M1Arm, CODE, RAM, RGB, DMA, ADC, TMR2, TMR3, TMR6, SPI

FLASH, SIZE = 0x40023c00, 0x1ffff7e0
BASE, A, B, END = 0x08000000, 0x08027000, 0x08027800, 0x08028000
PAGE, VTOR = 2048, 0xe000ed08
ARG, CONTEXT, UNSAFE, GEOMETRY, LINK, BUSY, CONTROLLER, RECORD, UNLOCK, ERASE, PROGRAM, VERIFY = range(0x31001,0x3100d)


class Store(M1Arm):
    def call(self,name,*args,instructions=3000000):
        return super().call(name,*args,instructions=instructions)

    def __init__(self, elf, recovery=False):
        self.recovery=recovery
        super().__init__(elf)
        self.cpu.mem_map(BASE,0x40000)
        self.cpu.mem_write(BASE,b'\xa5'*0x40000)
        self.cpu.mem_write(A,b'\xff'*(2*PAGE))
        if recovery:self.cpu.mem_write(BASE+0x4800,b'\xff'*PAGE)
        self.cpu.mem_map(0x1ffff000,0x1000)
        self.cpu.mem_write(SIZE,struct.pack('<H',256))
        self.put(FLASH+16,0x80)
        self.put(VTOR,0x08005200)
        self.pending={}
        self.unlock_key=False
        self.unlock_fails=False
        self.lock_fails=False
        self.erase_fails=False
        self.program_fails=None
        self.drop_erase=False
        self.drop_program=None
        self.stuck=None
        self.remaining=0
        self.operation=None
        self.commands=[]
        self.program_count=0
        self.stop_seen=False
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_register,begin=FLASH,end=FLASH+0x23)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_flash,begin=BASE,end=BASE+0x3ffff)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.program,begin=BASE,end=BASE+0x3ffff)
        self.cpu.hook_add(UC_HOOK_CODE,self.code,begin=CODE,end=CODE+0x3ffff)
        stop=self.symbols['fail_stop']&~1
        self.cpu.hook_add(UC_HOOK_CODE,self.stop,begin=stop,end=stop+24)
        for name in ('transaction','fail_stop','flash_unlock','flash_lock',
                     'flash_flag_clear','flash_sector_erase','flash_word_program'):
            assert RAM<=self.symbols[name]<RAM+0x10000,name
        assert not any(name in self.symbols for name in ('flash_internal_all_erase',
            'flash_user_system_data_erase','flash_fap_enable','flash_epp_set'))

    def flush(self):
        for address,value in self.pending.items():self.put(address,value)
        self.pending.clear()

    def guarded(self):
        assert self.cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        assert RAM<=self.cpu.reg_read(UC_ARM_REG_PC)<RAM+0x10000
        vectors=self.u32(VTOR)
        assert RAM<=vectors<RAM+0x10000 and vectors%512==0
        for offset in (8,12):assert self.u32(vectors+offset)==self.symbols['fail_stop']

    def begin_operation(self, kind, target):
        self.guarded()
        assert self.operation is None and not self.u32(FLASH+16)&0x80
        self.commands.append((kind,target))
        self.operation=kind
        self.remaining=None if self.stuck==kind else 2
        self.put(FLASH+12,1)

    def write(self,cpu,access,address,size,value,user):
        self.flush()
        self.writes.append((address,size,value))
        assert size==4
        if address==VTOR:
            assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
            return
        if address==0xe000e010:
            assert value==0
            return
        assert address in (FLASH+4,FLASH+12,FLASH+16,FLASH+20),hex(address)
        self.guarded()
        if address==FLASH+4:
            if value==0x45670123:self.unlock_key=True
            else:
                assert value==0xcdef89ab and self.unlock_key
                self.unlock_key=False
                if not self.unlock_fails:self.put(FLASH+16,self.u32(FLASH+16)&~0x80)
        elif address==FLASH+12:
            assert value==32  # Only clear completion, never erase an error.
            self.pending[address]=self.u32(address)&~value
        elif address==FLASH+20:
            assert value in (A,B)
        elif address==FLASH+16:
            assert not value&~0xc3,hex(value)  # No options, mass erase, IRQs.
            if value&0x40:
                assert value&2 and not value&1
                target=self.u32(FLASH+20)
                assert target in (A,B)
                self.begin_operation('erase',target)
                if not self.erase_fails and not self.drop_erase:
                    self.cpu.mem_write(target,b'\xff'*PAGE)
                self.pending[address]=value&~0x40  # self-clearing start
            elif value&0x80 and self.lock_fails:self.pending[address]=value&~0x80

    def read_register(self,cpu,access,address,size,value,user):
        self.flush()
        if address==FLASH+12 and self.operation:
            self.guarded()
            if self.remaining is None:self.put(address,1)
            elif self.remaining:
                self.remaining-=1;self.put(address,1)
            else:
                error=16 if self.operation=='erase' and self.erase_fails else 0
                if self.operation=='program' and self.program_count==self.program_fails:error=4
                self.put(address,error|32)
                self.operation=None

    def read_flash(self,cpu,access,address,size,value,user):
        assert (A<=address and address+size<=END or
                self.recovery and BASE+0x4800<=address<address+size<=BASE+0x5000),hex(address)
        assert not self.u32(FLASH+12)&1,'flash read while busy'
        assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1

    def program(self,cpu,access,address,size,value,user):
        self.flush();self.guarded()
        assert size==4 and address%4==0 and (A<=address<END or
            self.recovery and address==BASE+0x4800 and value==0x55aa55aa)
        assert self.u32(FLASH+16)&3==1
        assert self.u32(address)==0xffffffff
        self.program_count+=1
        self.begin_operation('program',address)
        if self.program_count==self.drop_program:
            # Applied on the next status read, after Unicorn's ordinary store.
            self.pending[address]=0xffffffff

    def code(self,cpu,address,size,user):
        assert not self.operation or not self.u32(FLASH+12)&1,'execution returned to flash-side code while busy'

    def check_guards(self):
        # This fixture intentionally sets DMA busy bits in rejection tests;
        # all CPU peripheral stores are independently whitelisted by write().
        if self.recovery:
            assert self.cpu.mem_read(BASE,0x4800)==b'\xa5'*0x4800
            assert self.cpu.mem_read(BASE+0x5000,A-BASE-0x5000)==b'\xa5'*(A-BASE-0x5000)
        else:assert self.cpu.mem_read(BASE,A-BASE)==b'\xa5'*(A-BASE)
        assert self.cpu.mem_read(END,BASE+0x40000-END)==b'\xa5'*(BASE+0x40000-END)

    def stop(self,cpu,address,size,user):
        if bytes(cpu.mem_read(address,2))==b'\x30\xbf':
            self.stop_seen=True;self.guarded();cpu.emu_stop()

    def preserved(self,mask=0):
        self.flush()
        assert self.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert self.u32(VTOR)==0x08005200
        assert self.u32(FLASH+16)==0x80
        assert self.cpu.mem_read(BASE,A-BASE)==b'\xa5'*(A-BASE)
        assert self.cpu.mem_read(END,BASE+0x40000-END)==b'\xa5'*(BASE+0x40000-END)


def linker_guards():
    root=Path(__file__).resolve().parents[1]
    template=(root/'tests/m1_storage_audit.ld').read_text()
    with tempfile.TemporaryDirectory(dir=root/'build-m1-hal') as directory:
        tmp=Path(directory)
        assembly=('.syntax unified\n.thumb\n.global m1_storage_read\n.thumb_func\n'
                  'm1_storage_read: bx lr\n.section .ramfunc.m1_storage,"ax"\n.thumb_func\n'
                  'ram_stub: bx lr\n')
        subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m4','-mthumb','-x','assembler',
                        '-c','-o',str(tmp/'stub.o'),'-'],input=assembly,text=True,check=True)
        for end,valid in ((0x08005200,False),(0x08005204,True),(A,True),(A+1,False),(END,False)):
            (tmp/'layout.ld').write_text(template.replace('0x08026000',hex(end)))
            result=subprocess.run(['arm-none-eabi-ld','-L',str(root),'-T',str(tmp/'layout.ld'),
                str(tmp/'stub.o'),'-o',str(tmp/'audit.elf')],capture_output=True,text=True)
            assert (result.returncode==0)==valid,(hex(end),result.stderr)
            if not valid:assert 'overlaps custom profile slots' in result.stderr


def run(elf):
    linker_guards()
    d=Store(elf);d.call('m1_test_store_boot')
    assert d.call('m1_test_store_save',7)==1
    assert d.commands[0]==('erase',A) and len(d.commands)==513
    assert [address for kind,address in d.commands[1:]]==list(range(A,A+PAGE,4))
    record=bytes(d.cpu.mem_read(A,PAGE));assert record[:4]==b'M1P1'
    d.call('m1_test_store_boot');assert d.call('m1_test_store_status')==1|(7<<16)
    assert d.call('m1_test_store_save',12)==1
    d.call('m1_test_store_boot');assert d.call('m1_test_store_status')==2|(12<<16)
    assert d.call('m1_test_store_clear')==1
    assert d.commands[-2:]==[('erase',A),('erase',B)]
    assert d.cpu.mem_read(A,PAGE*2)==b'\xff'*(PAGE*2);d.preserved()
    for mask in (0,1):
        d=Store(elf);d.cpu.mem_write(RGB,record);d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
        # The no-IRQ timebase must be allowed to run through a save. Unlike
        # scan timers it owns no DMA or flash accesses. Register preservation,
        # not modeled physical timer progression, is established here.
        d.put(TMR2,0x401);d.put(TMR2+0x28,215);d.put(TMR2+0x2c,0xffffffff)
        timer=bytes(d.cpu.mem_read(TMR2,0x100))
        assert d.call('m1_storage_write',1,RGB,1)==0;d.preserved(mask)
        assert bytes(d.cpu.mem_read(TMR2,0x100))==timer
        d.cpu.mem_write(RGB,b'\x5a'*PAGE)
        assert d.call('m1_storage_read',1,RGB)==0
        assert d.cpu.mem_read(RGB,PAGE)==record;d.preserved(mask)
    for operation,args,expected in (
            ('m1_storage_write',(2,RGB,1),ARG),('m1_storage_write',(0,0,1),ARG),
            ('m1_storage_write',(0,A,1),ARG),('m1_storage_write',(0,0x20017fff,1),ARG),
            ('m1_storage_write',(0,RGB,0),UNSAFE),('m1_storage_erase',(0,0),UNSAFE),
            ('m1_storage_erase',(2,1),ARG),('m1_storage_read',(2,RGB),ARG),
            ('m1_storage_read',(0,0),ARG),('m1_storage_write',(0,RGB,1),RECORD)):
        d=Store(elf)
        assert d.call(operation,*args)==expected and not d.writes,(operation,args)
    for reg,value in ((UC_ARM_REG_BASEPRI,1),(UC_ARM_REG_FAULTMASK,1),
                      (UC_ARM_REG_CONTROL,1),(UC_ARM_REG_IPSR,3)):
        d=Store(elf);d.cpu.reg_write(reg,value)
        assert d.call('m1_storage_erase',0,1)==CONTEXT and not d.writes
        assert d.call('m1_storage_read',0,RGB)==CONTEXT and not d.writes
    for size in (0,128,255,512,0xffff):
        d=Store(elf);d.cpu.mem_write(SIZE,struct.pack('<H',size))
        assert d.call('m1_storage_erase',0,1)==GEOMETRY and not d.writes
    for status,expected in ((1,BUSY),(4,CONTROLLER),(16,CONTROLLER)):
        d=Store(elf);d.put(FLASH+12,status)
        d.cpu.mem_write(RGB,b'\x5a'*PAGE)
        assert d.call('m1_storage_read',0,RGB)==expected and not d.writes
        assert d.cpu.mem_read(RGB,PAGE)==b'\x5a'*PAGE
    for controller in (0,1,2,0x180,0x280,0x480):
        d=Store(elf);d.put(FLASH+16,controller)
        assert d.call('m1_storage_erase',0,1)==CONTROLLER and not d.writes
    busy_registers=[DMA+8+20*i for i in range(7)]+[0x40026408+20*i for i in range(7)]
    busy_registers += [ADC+8,TMR3,TMR6]
    for register in busy_registers:
        d=Store(elf);d.put(register,d.u32(register)|1)
        assert d.call('m1_storage_erase',0,1)==UNSAFE and not d.writes,hex(register)
    for register in (SPI+8,0x40003c08):
        d=Store(elf);d.put(register,0x80)
        assert d.call('m1_storage_erase',0,1)==UNSAFE and not d.writes
    for failure,expected in (('unlock_fails',UNLOCK),('erase_fails',ERASE),('drop_erase',VERIFY)):
        d=Store(elf);setattr(d,failure,True);d.cpu.mem_write(A,b'\0'*PAGE)
        assert d.call('m1_storage_erase',0,1)==expected;d.preserved()
        assert not any(kind=='program' for kind,_ in d.commands)
    for failure,expected in (('program_fails',PROGRAM),('drop_program',VERIFY)):
        for index in (1,128,512):
            d=Store(elf);d.cpu.mem_write(RGB,record);setattr(d,failure,index)
            assert d.call('m1_storage_write',0,RGB,1)==expected
            assert d.program_count==index;d.preserved()
    d=Store(elf);d.lock_fails=True
    assert d.call('m1_storage_erase',0,1)==CONTROLLER
    assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==0 and d.u32(VTOR)==0x08005200
    assert d.u32(FLASH+16)==0
    # A failed replacement retains the previous committed slot; no fallback
    # erase, retries or silent success. Reboot can use the surviving record.
    d=Store(elf);d.cpu.mem_write(A,record);d.call('m1_test_store_boot')
    d.program_fails=128
    assert d.call('m1_test_store_save',9)==0 and d.cpu.mem_read(A,PAGE)==record
    commands=len(d.commands);assert d.call('m1_test_store_save',9)==0 and len(d.commands)==commands
    d.put(FLASH+12,0);d.call('m1_test_store_boot')
    assert d.call('m1_test_store_status')==1|(7<<16);d.preserved()
    for operation in ('erase','program'):
        d=Store(elf);d.cpu.mem_write(RGB,record);d.stuck=operation
        try:d.call('m1_storage_write',0,RGB,1,instructions=12000000)
        except AssertionError as error:assert 'did not return' in str(error)
        else:raise AssertionError('stuck controller returned')
        assert d.stop_seen and d.cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        assert d.u32(0xe000e010)==0 and d.cpu.mem_read(d.symbols['fatal'],1)==b'\1'
        assert d.u32(FLASH+12)&1 and d.u32(VTOR)!=0x08005200
        assert len(d.commands)==(1 if operation=='erase' else 2)
    print('PASS M1 journal/SDK storage: owned slots, SRAM execution/vectors, guards, full readback, failures, reboot fallback and bounded fail-stop')


def recovery_check(elf):
    for failure,expected in ((None,0),('unlock_fails',UNLOCK),
                              ('program_fails',PROGRAM),('drop_program',VERIFY)):
        d=Store(elf,recovery=True)
        before=bytes(d.cpu.mem_read(BASE,0x40000))
        if failure:setattr(d,failure,1)
        assert d.call('m1_storage_arm_recovery',1)==expected
        after=bytes(d.cpu.mem_read(BASE,0x40000))
        assert before[:0x4800]==after[:0x4800] and before[0x4804:]==after[0x4804:]
        assert not any(kind=='erase' for kind,_ in d.commands)
        assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==0 and d.u32(VTOR)==0x08005200
        assert d.u32(FLASH+16)==0x80
        if not failure:assert d.u32(BASE+0x4800)==0x55aa55aa
    d=Store(elf,recovery=True);d.put(BASE+0x4804,0)
    assert d.call('m1_storage_arm_recovery',1)==VERIFY and not d.commands
    d=Store(elf,recovery=True)
    assert d.call('m1_storage_arm_recovery',0)==UNSAFE and not d.commands
    print('PASS M1 trial recovery: exact boot-flag word, no erase, SRAM SDK/vectors, readback and failure guards')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf')
    parser.add_argument('--recovery-only',action='store_true');args=parser.parse_args()
    recovery_check(args.elf)
    if not args.recovery_only:run(args.elf)
