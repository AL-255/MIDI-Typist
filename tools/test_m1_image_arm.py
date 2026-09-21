"""Development ELF boundaries and actual Cortex-M reset/memory initialization.

Never creates a flash binary or opens hardware. Main-loop ordering tests stub
component calls; composed peripherals are tested by the separate boot audit.
"""
import argparse
import io
from pathlib import Path
import struct
import subprocess
import tempfile
from elftools.elf.elffile import ELFFile
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE,
                     UC_HOOK_MEM_WRITE, UC_PROT_READ, UC_PROT_EXEC)
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_MSP, UC_ARM_REG_PSP, UC_ARM_REG_PC,
    UC_ARM_REG_LR, UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI, UC_ARM_REG_FAULTMASK,
    UC_ARM_REG_CONTROL)
from firmware_defaults import DEFAULTS as D

FLASH,RAM,APP,VECTOR,LIMIT,RAM_END = 0x08000000,0x20000000,0x08005000,0x08005200,0x08027000,0x20018000
VTOR,GPIOC,SIZE = 0xe000ed08,0x40020800,0x1ffff7e0


class Image:
    def __init__(self,data):
        self.elf=ELFFile(io.BytesIO(data))
        self.sections={s.name:s for s in self.elf.iter_sections()}
        self.symbols={s.name:s['st_value'] for s in self.sections['.symtab'].iter_symbols()}
        self.loads=[s for s in self.elf.iter_segments() if s['p_type']=='PT_LOAD']
        self.vectors=struct.unpack('<128I',self.sections['.m1_vectors'].data())

    def validate(self):
        e,s=self.elf,self.symbols
        assert e.elfclass==32 and e.little_endian and e['e_machine']=='EM_ARM' and e['e_type']=='ET_EXEC'
        assert self.sections['.m1_identity']['sh_addr']==APP
        assert self.sections['.m1_identity'].data()==b'AT32F405 8KMKB'
        assert self.sections['.m1_vectors']['sh_addr']==VECTOR and VECTOR%512==0
        assert e['e_entry']==self.vectors[1]==s['M1_Reset_Handler']
        assert self.vectors[0]==s['__m1_stack_top__'] and self.vectors[0]%8==0
        required={1:'M1_Reset_Handler',19:'m1_sleep_irq',32:'m1_hal_dma_irq',
                  70:'m1_hal_timer_irq',93:'m1_usb_hw_irq'}
        for index,target in enumerate(self.vectors[1:],1):
            assert target==s[required.get(index,'M1_Unhandled_Handler')]
            assert target&1 and VECTOR+512<=target<s['__m1_application_flash_end__']
        physical=[];virtual=[]
        for seg in self.loads:
            a,b,n=seg['p_paddr'],seg['p_vaddr'],seg['p_filesz'];m=seg['p_memsz']
            assert m>=n
            if n:
                assert APP<=a<a+n<=LIMIT,('protected flash load',hex(a),n)
                physical.append((a,a+n))
            else:assert RAM<=a==b<RAM_END
            assert (APP<=b<b+m<=LIMIT or RAM<=b<b+m<=RAM_END)
            virtual.append((b,b+m))
        for ranges in (physical,virtual):
            ordered=sorted(ranges)
            assert all(a[1]<=b[0] for a,b in zip(ordered,ordered[1:])),ordered
        assert max(b for _,b in physical)==s['__m1_application_flash_end__']
        allowed={'.m1_identity','.m1_vectors','.m1_storage_ram','.text','.data','.bss','.m1_stack'}
        assert {x.name for x in self.sections.values() if x['sh_flags']&2}==allowed
        for name in allowed:
            section=self.sections[name]
            assert sum(seg.section_in_segment(section) for seg in self.loads)==1,name
        for name in ('data','storage_ram'):
            start,end,load=(s[f'__m1_{name}_{part}__'] for part in ('start','end','load'))
            assert RAM<=start<end<=RAM_END and start%4==end%4==load%4==0
            assert APP<=load<load+end-start<=LIMIT
            owner=next(seg for seg in self.loads if seg['p_vaddr']==start)
            assert owner['p_paddr']==load and owner['p_filesz']==end-start
        assert RAM<=s['__m1_bss_start__']<s['__m1_bss_end__']<=s['__m1_stack_bottom__']
        assert s['__m1_stack_top__']-s['__m1_stack_bottom__']==D['M1_MAIN_STACK_BYTES']
        assert s['__m1_stack_top__']<=RAM_END
        for symbol in self.sections['.symtab'].iter_symbols():
            if symbol['st_info']['type']=='STT_FUNC' and symbol.name.startswith('flash_'):
                assert s['__m1_storage_ram_start__']<=symbol['st_value']<s['__m1_storage_ram_end__']
        for name in ('transaction','fail_stop','flash_unlock','flash_lock','flash_sector_erase','flash_word_program'):
            assert s['__m1_storage_ram_start__']<=s[name]<s['__m1_storage_ram_end__']
        assert not any(name in s for name in ('SystemInit','flash_internal_all_erase',
                                             'flash_user_system_data_erase','flash_fap_enable'))


class Reset:
    def __init__(self,image):
        self.image=image;self.s=image.symbols;self.writes=[];self.reset_phase=True
        self.cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for address,size in ((FLASH,0x40000),(RAM,RAM_END-RAM),(0xe000e000,0x2000),
                             (0x40000000,0x50000),(0x1ffff000,0x1000)):
            self.cpu.mem_map(address,size)
        self.cpu.mem_write(FLASH,b'\xff'*0x40000)
        self.cpu.mem_write(RAM,b'\xa5'*(RAM_END-RAM))
        for seg in image.loads:
            if seg['p_filesz']:self.cpu.mem_write(seg['p_paddr'],seg.data())
        self.cpu.mem_protect(FLASH,0x40000,UC_PROT_READ|UC_PROT_EXEC)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write)

    def u32(self,address):return struct.unpack('<I',self.cpu.mem_read(address,4))[0]
    def put(self,address,value):self.cpu.mem_write(address,struct.pack('<I',value))
    def write(self,cpu,access,address,size,value,user):
        if self.reset_phase:assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        self.writes.append((address,size,value))

    def reset(self,psp=False):
        self.cpu.reg_write(UC_ARM_REG_MSP,RAM_END-16)
        self.cpu.reg_write(UC_ARM_REG_PSP,RAM_END-32)
        self.cpu.reg_write(UC_ARM_REG_CONTROL,2 if psp else 0)
        self.cpu.reg_write(UC_ARM_REG_BASEPRI,0x40)
        self.cpu.reg_write(UC_ARM_REG_FAULTMASK,1)
        self.cpu.reg_write(UC_ARM_REG_PRIMASK,0)
        main=self.s['m1_main']&~1
        hook=self.cpu.hook_add(UC_HOOK_CODE,lambda cpu,*args:cpu.emu_stop(),begin=main,end=main)
        self.cpu.emu_start(self.image.vectors[1],FLASH+0x40000,count=200000)
        self.cpu.hook_del(hook)
        assert self.cpu.reg_read(UC_ARM_REG_PC)==main
        assert self.cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        assert all(self.cpu.reg_read(r)==0 for r in (UC_ARM_REG_CONTROL,UC_ARM_REG_BASEPRI,UC_ARM_REG_FAULTMASK))
        assert self.u32(VTOR)==VECTOR and self.u32(0xe000ed14)&512
        assert (self.u32(0xe000ed0c)>>8)&7==3
        for name in ('data','storage_ram'):
            start,end,load=(self.s[f'__m1_{name}_{part}__'] for part in ('start','end','load'))
            assert self.cpu.mem_read(start,end-start)==self.cpu.mem_read(load,end-start)
        lo,hi=self.s['__m1_bss_start__'],self.s['__m1_bss_end__']
        assert self.cpu.mem_read(lo,hi-lo)==bytes(hi-lo)
        ranges=[(self.s[f'__m1_{n}_start__'],self.s[f'__m1_{n}_end__']) for n in ('data','storage_ram','bss')]
        ranges.append((self.s['__m1_stack_bottom__'],self.s['__m1_stack_top__']))
        for address,size,_ in self.writes:
            if RAM<=address<RAM_END:
                assert any(lo<=address<address+size<=hi for lo,hi in ranges),hex(address)
            else:
                assert address in (0xe000e010,0xe000e014,0xe000e018,0xe000ed04,VTOR,0xe000ed0c,0xe000ed14) or (
                    0xe000e180<=address<0xe000e190 or 0xe000e280<=address<0xe000e290),hex(address)
                assert self.cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        assert self.s['__m1_stack_bottom__']<self.cpu.reg_read(UC_ARM_REG_SP)<=self.s['__m1_stack_top__']
        for lo,hi in zip([RAM]+[b for a,b in sorted(ranges)], [a for a,b in sorted(ranges)]+[RAM_END]):
            assert self.cpu.mem_read(lo,hi-lo)==b'\xa5'*(hi-lo)
        self.reset_phase=False


def linker_bounds():
    root=Path(__file__).resolve().parents[1]
    script=root/'firmware/boards/monsgeek_m1_v5_tmr/linker/application.ld'
    with tempfile.TemporaryDirectory(dir=root/'build-m1-hal') as directory:
        tmp=Path(directory)
        for identity,vectors,flash,ram,valid in ((14,512,16,16,True),
                (15,512,16,16,False),(14,508,16,16,False),
                (14,512,0x22000,16,False),(14,512,16,0x18000,False)):
            source=(f'.syntax unified\n.thumb\n.section .m1_identity,"a"\n.space {identity}\n'
                    f'.section .m1_vectors,"a"\n.balign 512\n.space {vectors}\n'
                    '.text\n.global M1_Reset_Handler\n.thumb_func\nM1_Reset_Handler: bx lr\n'
                    f'.section .rodata.guard,"a"\n.space {flash}\n'
                    f'.section .bss.guard,"aw",%nobits\n.space {ram}\n'
                    '.section .m1_stack,"aw",%nobits\n.balign 8\n.space 8192\n')
            subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-m4','-mthumb','-x','assembler',
                '-c','-o',str(tmp/'fixture.o'),'-'],input=source,text=True,check=True)
            result=subprocess.run(['arm-none-eabi-ld','-L',str(root),'-T',str(script),
                str(tmp/'fixture.o'),'-o',str(tmp/'fixture.elf')],capture_output=True,text=True)
            assert (result.returncode==0)==valid,result.stderr
            if not valid:assert 'M1 ' in result.stderr,result.stderr
    print('PASS M1 application linker: identity/vector mismatch and flash/RAM overflow rejected')


def malformed(data):
    original=Image(data);original.validate()
    vectors=original.sections['.m1_vectors']['sh_offset']
    identity=original.sections['.m1_identity']['sh_offset']
    changes=[(identity,b'X'),(vectors+4,struct.pack('<I',VECTOR)),
             (vectors,struct.pack('<I',RAM_END+8)),(vectors+93*4,struct.pack('<I',original.symbols['M1_Unhandled_Handler']))]
    e=original.elf
    for index,seg in enumerate(e.iter_segments()):
        if seg['p_type']=='PT_LOAD' and seg['p_filesz']:
            # Independently reject a bootloader prefix or a config-page load.
            for address in (FLASH,LIMIT):
                changes.append((e['e_phoff']+index*e['e_phentsize']+12,struct.pack('<I',address)))
    for at,replacement in changes:
        broken=bytearray(data);broken[at:at+len(replacement)]=replacement
        try:Image(broken).validate()
        except (AssertionError,StopIteration):pass
        else:raise AssertionError(f'accepted corrupt image at {at:x}')
    print('PASS M1 ELF: exact identity/vectors, all load ranges, RAM SDK writer, stack and malformed-image rejection')


def main_loop(image):
    # Test actual foreground control flow, not its already-audited callees.
    for external,failure,expected in ((True,None,3),(False,None,3),(True,'geometry',4),
        (True,'clock',5),(True,'time_start',6),(True,'boot_begin',7),(True,'boot_service',7),
        (True,'time_now',6),(True,'source',8),(False,'source',8),(True,'device',9),
        (True,'lighting',9),(True,'transport',9),(True,'storage',9),(False,'radio',9),
        (True,'recovery',11),(True,'boot_diagnostics',7)):
        d=Reset(image);d.reset();s=d.s;trace=[];live=0;times=[];diagnostic_calls=0
        d.cpu.mem_write(SIZE,struct.pack('<H',128 if failure=='geometry' else 256))
        d.put(GPIOC+0x10,0 if external else 1<<13)
        d.put(GPIOC,0) # input source pin, real SDK GPIO setup is allowed
        results={'m1_storage_arm_recovery':0x3100c if failure=='recovery' else 0,
            'm1_live_update_requested':0,
            'm1_usb_hw_running':external and failure!='boot_service','m1_diagnostics_service':0,
            'm1_clock_init':6 if failure=='clock' else 0,
            'm1_time_start':failure!='time_start','m1_boot_begin':failure!='boot_begin',
            'm1_boot_state':6 if failure in ('boot_service','boot_diagnostics') else 5,'m1_boot_error':5,
            'm1_hal_healthy':failure!='device','m1_lighting_healthy':failure!='lighting',
            'm1_live_transport_fault':failure=='transport','m1_live_storage_fault':failure=='storage',
            'm1_live_transport':6 if external else D['M1_DEFAULT_WIRELESS_TRANSPORT'],
            'm1_wireless_healthy':failure!='radio'}
        voids=('m1_boot_service','m1_live_stop','m1_usb_hw_stop','m1_hal_stop',
               'm1_lighting_stop','m1_wireless_stop','m1_radio_stop','m1_diagnostics_runtime_fault')
        names=set(results)|set(voids)|{'m1_time_now','m1_live_service'}
        by_address={s[name]&~1:name for name in names}
        def intercept(cpu,address,size,user):
            nonlocal live,diagnostic_calls
            if bytes(cpu.mem_read(address,2))==b'\x30\xbf':cpu.emu_stop();return
            name=by_address.get(address)
            if not name:return
            args=tuple(cpu.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
            trace.append((name,args,cpu.reg_read(UC_ARM_REG_PRIMASK)))
            result=results.get(name,0)
            if name=='m1_diagnostics_service':
                diagnostic_calls+=1
                if diagnostic_calls==2:cpu.emu_stop();return
            if name=='m1_time_now':
                # Independent wraps: deriving ms from us must fail this check.
                point=(125,0xfffffffc);cpu.mem_write(args[0],struct.pack('<II',*point))
                result=failure!='time_now'
                if failure=='source':d.put(GPIOC+0x10,(1<<13) if external else 0)
            if name=='m1_live_service':
                times.append(args[:2]);live+=1
                if live==2:cpu.emu_stop();return
            cpu.reg_write(UC_ARM_REG_R0,int(result));cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        d.cpu.hook_add(UC_HOOK_CODE,intercept)
        d.cpu.emu_start(s['m1_main'],FLASH+0x40000,count=200000)
        assert d.u32(s['m1_main_state'])==expected,(failure,trace)
        if expected==4:assert d.u32(s['m1_main_detail'])==128
        if expected==5:assert d.u32(s['m1_main_detail'])==6
        if expected==7:assert d.u32(s['m1_main_detail'])==5
        if expected==8:assert d.u32(s['m1_main_detail'])==external
        if expected==9:
            assert d.u32(s['m1_main_detail'])=={'device':1,'lighting':2,'transport':4,'storage':8,'radio':16}[failure]
        labels=[x[0] for x in trace]
        if expected==3:
            assert labels.index('m1_storage_arm_recovery')<labels.index('m1_clock_init')
            assert live==2 and times==[(0xfffffffc,125)]*2
            assert labels.index('m1_boot_service')<labels.index('m1_live_service')
            begin=next(x for x in trace if x[0]=='m1_boot_begin')
            assert begin[1]==(6 if external else D['M1_DEFAULT_WIRELESS_TRANSPORT'],0,1)
            assert begin[2]==1
            assert next(x for x in trace if x[0]=='m1_boot_service')[2]==0
        else:
            diagnostic=failure=='boot_diagnostics' or (external and expected in (8,9))
            assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==int(not diagnostic),(failure,trace)
            if failure=='boot_diagnostics':assert diagnostic_calls==2 and not live
            if expected in (8,9):
                at=labels.index('m1_live_stop')
                assert labels[at:at+5]==['m1_live_stop','m1_hal_stop',
                                      'm1_lighting_stop','m1_wireless_stop','m1_radio_stop']
                assert 'm1_usb_hw_stop' not in labels
                assert ('m1_diagnostics_runtime_fault' in labels)==external
                if external:assert diagnostic_calls==2
            else:assert 'm1_live_stop' not in labels
    print('PASS M1 development main: ordered startup, source-selected transport, independent timestamps and terminal failures (component calls stubbed)')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path)
    args=parser.parse_args();data=args.elf.read_bytes();image=Image(data)
    malformed(data);linker_bounds()
    for psp in (False,True):Reset(image).reset(psp)
    print('PASS M1 reset: real entry, MSP/PSP normalization, masked vectors, data/RAM-code copy, BSS and memory guards')
    main_loop(image)


if __name__=='__main__':main()
