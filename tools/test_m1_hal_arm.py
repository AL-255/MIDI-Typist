"""Execute the actual M1 HAL/official SDK offline; no device access.

Only register reset/W1C/output-latch effects are modeled. DMA/SPI completion
flags and time are scripted: this is not proof of electrical output/timing.
The ELF uses a synthetic code address and has no boot header/vector table.
"""
import argparse
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_HOOK_CODE
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_R0,
    UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
    UC_ARM_REG_FAULTMASK)
from firmware_defaults import DEFAULTS as D
from keyboard_boards import m1_records

CODE, RAM, RETURN = 0x10000000, 0x20000000, 0x1003f000
SPI, GPIO, DMA, CRM = 0x40003800, 0x40020000, 0x40026000, 0x40023800
RGB = RAM+0xc000
ADC, TMR3, TMR6 = 0x40012000, 0x40000400, 0x40001000
LATCH, TIMEOUT = D['M1_LED_LATCH_US'], D['M1_LED_TRANSFER_TIMEOUT_US']


class M1Arm:
    def __init__(self, elf_path, scanner=False):
        self.scanner=scanner
        self.calibration_completes=True
        self.cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for address,size in ((CODE,0x40000),(RAM,0x10000),
                             (0x40000000,0x40000),(0xe000e000,0x2000)):
            self.cpu.mem_map(address,size)
        with open(elf_path,'rb') as stream:
            elf=ELFFile(stream)
            for segment in elf.iter_segments():
                if segment['p_type']=='PT_LOAD':
                    self.cpu.mem_write(segment['p_vaddr'],segment.data())
            self.symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        self.writes=[]
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=0x40000000,end=0x4003ffff)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=0xe000e000,end=0xe000ffff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_clear,begin=DMA+4,end=DMA+7)
        if scanner:
            self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_adc,begin=ADC+8,end=ADC+11)
        self.put(CRM+4,(1<<30)|(2<<16)|(72<<6)|1)  # 12 MHz * 72 / 1 / 4
        self.put(CRM+8,(2<<2)|(4<<10))  # PLL, AHB /1, APB1 /2, APB2 /1
        # Unrelated peripherals must survive both success and failure paths.
        self.cpu.mem_write(GPIO+0x400,b'\xa5'*0x800)  # GPIO B/C (power, mux, encoder)
        self.cpu.mem_write(DMA+0x1c,b'\x5a'*(0x100-0x1c))  # other DMA channels
        self.rgb=bytes((i*37+19)&255 for i in range(246))
        self.cpu.mem_write(RGB,self.rgb)

    def u32(self,address):
        return struct.unpack('<I',self.cpu.mem_read(address,4))[0]

    def put(self,address,value):
        self.cpu.mem_write(address,struct.pack('<I',value&0xffffffff))

    def write(self,cpu,access,address,size,value,user):
        self.writes.append((address,size,value))
        allowed=(SPI<=address<SPI+0x24 or GPIO<=address<GPIO+0x40 or
                 DMA<=address<DMA+0x1c or address in (DMA+0x100,DMA+0x104,
                     CRM+0x20,CRM+0x30,CRM+0x40,0xe000e180))
        if self.scanner:
            allowed |= (ADC<=address<ADC+0x400 or
                        GPIO+0x400<=address<GPIO+0x440 or GPIO+0x800<=address<GPIO+0x840 or
                        TMR3<=address<TMR3+0x100 or TMR6<=address<TMR6+0x100 or
                        DMA+0x6c<=address<DMA+0x80 or address in (DMA+0x118,CRM+0x24,CRM+0x44) or
                        0xe000e100<=address<0xe000e200 or 0xe000e280<=address<0xe000e300 or
                        0xe000e400<=address<0xe000e500)
        assert allowed, f'unowned register write {address:08x}'
        if address==DMA+4:
            mask=value
            for channel in range(7):
                if value&(1<<(channel*4)):mask |= 15<<(channel*4)
            self.put(DMA,self.u32(DMA)&~mask)
        elif address==CRM+0x20 and value&(1<<14):
            self.cpu.mem_write(SPI,bytes(0x24))
            self.put(SPI+8,2)  # reset TX-data-empty
        elif address==GPIO+0x28:
            self.put(GPIO+0x14,self.u32(GPIO+0x14)&~value)
        elif self.scanner and address==GPIO+0x428:
            self.put(GPIO+0x414,self.u32(GPIO+0x414)&~value)
        elif self.scanner and address==GPIO+0x418:
            self.put(GPIO+0x414,self.u32(GPIO+0x414)|value)

    def read_adc(self,cpu,access,address,size,value,user):
        if self.calibration_completes:
            self.put(ADC+8,self.u32(ADC+8)&~12)

    def read_clear(self,cpu,access,address,size,value,user):
        # RM 9.5.2: reset-zero RW1C command bits clear flags in STS. They do
        # not retain the last write like RAM; SDK dma_reset uses read/OR/write.
        # https://www.arterychip.com/download/RM/RM_AT32F402_405_EN_V2.01.pdf
        self.put(DMA+4,0)

    def call(self,name,*args,instructions=500000):
        for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):
            self.cpu.reg_write(reg,value&0xffffffff)
        self.cpu.reg_write(UC_ARM_REG_SP,RAM+0xf000)
        self.cpu.reg_write(UC_ARM_REG_LR,RETURN|1)
        self.cpu.emu_start(self.symbols[name]|1,RETURN,count=instructions)
        assert self.cpu.reg_read(UC_ARM_REG_PC)==RETURN, f'{name} did not return'
        self.check_guards()
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def check_guards(self):
        if not self.scanner:
            assert bytes(self.cpu.mem_read(GPIO+0x400,0x800))==b'\xa5'*0x800
            assert bytes(self.cpu.mem_read(DMA+0x1c,0x100-0x1c))==b'\x5a'*(0x100-0x1c)
        else:
            for start,end in ((0x1c,0x6c),(0x80,0x100)):
                assert bytes(self.cpu.mem_read(DMA+start,end-start))==b'\x5a'*(end-start)
            # Sensor/LED supply pins PB6/PB13, PC6/PC14 are startup-owned.
            for port,pins in ((GPIO+0x400,(6,13)),(GPIO+0x800,(6,14))):
                for pin in pins:
                    for offset,width in ((0,2),(4,1),(8,2),(12,2),(20,1)):
                        mask=((1<<width)-1)<<(width*pin)
                        assert self.u32(port+offset)&mask==0xa5a5a5a5&mask

    def ready(self):
        return bool(self.call('m1_lighting_ready'))

    def init(self,now=0):
        assert self.call('m1_lighting_init',now)==1
        assert self.call('m1_lighting_healthy')==1 and not self.ready()
        assert (self.u32(GPIO)>>20)&3==1 and not self.u32(GPIO+0x14)&(1<<10)
        assert not self.u32(DMA+8)&1
        self.call('m1_lighting_service',now+LATCH-1)
        assert not self.ready()
        self.call('m1_lighting_service',now+LATCH)
        assert self.ready()
        return (now+LATCH)&0xffffffff

    def offer(self,now):
        assert self.call('m1_lighting_offer',RGB,246,now)==1
        assert not self.ready()
        assert self.u32(SPI)==0xc35d  # half TX, software CS, master, /16, CPHA2, enabled
        assert self.u32(SPI+4)&6==6  # CS output and TX DMA
        assert self.u32(DMA+8)==0x2091  # high priority, bytes, M->P, MINC, enabled
        assert self.u32(DMA+12)==1968 and self.u32(DMA+16)==SPI+12
        assert self.u32(DMA+0x104)&127==13
        assert (self.u32(GPIO)>>20)&3==2
        assert (self.u32(GPIO+0x24)>>8)&15==5
        assert (self.u32(GPIO+8)>>20)&3==2
        assert (self.u32(GPIO+12)>>20)&3==1
        pointer=self.u32(DMA+20)
        expected=bytes(0xf0 if value&(128>>bit) else 0xc0
            for led in range(82) for channel in (1,0,2)
            for value in (self.rgb[led*3+channel],) for bit in range(8))
        assert bytes(self.cpu.mem_read(pointer,1968))==expected
        return pointer,expected


def lighting(elf):
    dev=M1Arm(elf)
    dev.put(CRM+8,0)  # incompatible HICK clock: no pin/clock/flash writes
    assert dev.call('m1_lighting_init',0)==0 and not dev.writes
    assert dev.call('m1_lighting_healthy')==0
    dev=M1Arm(elf);now=dev.init()
    assert dev.call('m1_lighting_offer',RGB,245,now)==0 and dev.ready()
    assert dev.call('m1_lighting_offer',0,246,now)==0 and dev.ready()
    pointer,expected=dev.offer(now)
    dev.cpu.mem_write(RGB,bytes(246))  # caller storage no longer owned by DMA
    assert dev.call('m1_lighting_offer',RGB,246,now+1)==0
    assert bytes(dev.cpu.mem_read(pointer,1968))==expected
    dev.put(DMA,3);dev.put(SPI+8,0x82)  # DMA complete, shift register still busy
    dev.call('m1_lighting_service',now+3000)
    assert not dev.ready() and not dev.u32(DMA+8)&1
    assert (dev.u32(GPIO)>>20)&3==2  # don't detach mid-byte
    assert dev.call('m1_lighting_offer',RGB,246,now+3001)==0
    dev.put(SPI+8,0)  # not busy but a byte still in the data register
    dev.call('m1_lighting_service',now+3002);assert not dev.ready()
    dev.put(SPI+8,2);dev.call('m1_lighting_service',now+3003)
    assert (dev.u32(GPIO)>>20)&3==1 and not dev.u32(SPI)&64
    dev.call('m1_lighting_service',now+3003+LATCH-1);assert not dev.ready()
    dev.call('m1_lighting_service',now+3003+LATCH);assert dev.ready()
    assert dev.call('m1_lighting_offer',RGB,246,now+3003+LATCH)==1
    assert bytes(dev.cpu.mem_read(pointer,1968))==b'\xc0'*1968
    dev.call('m1_lighting_stop')
    assert not dev.ready() and dev.call('m1_lighting_healthy')==0
    assert not dev.u32(DMA+8)&1 and not dev.u32(SPI)&64
    assert dev.call('m1_lighting_offer',RGB,246,now+9000)==0
    for reason in ('DMA error','SPI error','DMA timeout','drain timeout'):
        dev=M1Arm(elf);now=dev.init(0xfffff000);dev.offer(now)
        if reason=='DMA error':dev.put(DMA,8)
        elif reason=='SPI error':dev.put(SPI+8,0x20)
        elif reason=='drain timeout':
            dev.put(DMA,3);dev.put(SPI+8,0x82)
            dev.call('m1_lighting_service',now+1)
        dev.call('m1_lighting_service',now+(TIMEOUT if 'timeout' in reason else 2))
        assert dev.call('m1_lighting_healthy')==0,reason
        assert dev.call('m1_lighting_errors')==1,reason
        assert not dev.u32(DMA+8)&1 and not dev.u32(SPI)&64
        assert (dev.u32(GPIO)>>20)&3==1 and not dev.u32(GPIO+0x14)&(1<<10)
        assert dev.call('m1_lighting_offer',RGB,246,now+TIMEOUT+1)==0
        dev.init(now+TIMEOUT+2)  # explicit recovery, no automatic retry
    print('PASS M1 linked HAL/SDK: encoding, PA10/SPI2/DMA1 ownership, drain, latch, stop, faults and wraparound')


def scanner(elf):
    dev=M1Arm(elf,scanner=True)
    assert dev.call('m1_hal_init')==1
    assert dev.call('m1_hal_healthy')==1
    assert not dev.u32(TMR3)&1 and not dev.u32(TMR6)&1
    assert not dev.u32(DMA+0x6c)&1
    assert dev.u32(DMA+0x118)&127==5  # ADC1 DMAMUX request
    # Sequence positions and sample-time selectors, read from actual SDK output.
    packed=[dev.u32(ADC+0x34),dev.u32(ADC+0x30),dev.u32(ADC+0x2c)]
    ranks=[(packed[i//6]>>(5*(i%6)))&31 for i in range(15)]
    assert ranks==[10,11,12,13,0,1,2,3,4,5,6,7,14,15,8],ranks
    assert dev.call('m1_hal_start')==1
    assert dev.call('m1_hal_start')==0
    def begin():
        dev.put(TMR6+0x10,1);dev.call('m1_hal_timer_irq')
    def complete(bank):
        assert dev.u32(DMA+0x70)&65535==15
        assert dev.u32(DMA+0x74)==ADC+0x4c
        assert dev.u32(DMA+0x6c)&1 and dev.u32(TMR3)&1
        assert (dev.u32(GPIO+0x414)>>7)&7==[0,6,2,4,3,1][bank]  # B7..B9
        dev.cpu.mem_write(dev.u32(DMA+0x78),struct.pack('<15H',*(1000+bank*15+i for i in range(15))))
        dev.put(DMA,3<<20);dev.call('m1_hal_dma_irq')
    begin()
    for bank in range(6):
        assert dev.call('m1_hal_frame',RGB,RGB+200)==0
        complete(bank)
    assert dev.call('m1_hal_frame',RGB,RGB+200)==1
    actual=struct.unpack('<82H',dev.cpu.mem_read(RGB,164))
    assert actual==tuple(1001+r[1]*15+r[2] for r in m1_records())
    assert dev.u32(RGB+200)==1
    assert dev.call('m1_hal_battery',RGB+204,RGB+208)==1
    assert struct.unpack('<H',dev.cpu.mem_read(RGB+204,2))[0]==1079
    assert dev.u32(RGB+208)==1
    assert dev.call('m1_hal_frame',RGB,RGB+200)==0
    begin();begin()  # cadence overtook an incomplete frame: fail-stop
    assert dev.call('m1_hal_healthy')==0 and dev.call('m1_hal_errors')==1
    assert dev.call('m1_hal_battery',RGB+204,RGB+208)==0
    assert dev.call('m1_hal_start')==0
    assert not dev.u32(TMR3)&1 and not dev.u32(TMR6)&1
    assert dev.call('m1_hal_init')==1 and dev.call('m1_hal_start')==1
    begin();dev.put(DMA,8<<20);dev.call('m1_hal_dma_irq')
    assert dev.call('m1_hal_healthy')==0 and dev.call('m1_hal_frame',RGB,RGB+200)==0
    # Bounded calibration failure runs the real polling loop, not a stub.
    dev=M1Arm(elf,scanner=True);dev.calibration_completes=False
    assert dev.call('m1_hal_init',instructions=20000000)==0
    assert dev.call('m1_hal_healthy')==0
    # Both HALs share DMA1/GPIOA/CRM but own different channels and pins.
    dev=M1Arm(elf,scanner=True)
    assert dev.call('m1_hal_init')==1 and dev.call('m1_hal_start')==1
    dev.put(TMR6+0x10,1);dev.call('m1_hal_timer_irq')
    regions=((ADC,0x400),(TMR3,0x100),(TMR6,0x100),(DMA+0x6c,0x14))
    before=[bytes(dev.cpu.mem_read(a,n)) for a,n in regions]
    dev.put(DMA,3<<20)  # a pending sensor completion must survive LED setup
    now=dev.init();pointer,encoded=dev.offer(now)
    assert [bytes(dev.cpu.mem_read(a,n)) for a,n in regions]==before
    assert dev.u32(DMA)&(3<<20)==3<<20
    dev.call('m1_hal_dma_irq')
    assert dev.u32(DMA+8)&1  # LED channel still running
    assert bytes(dev.cpu.mem_read(pointer,1968))==encoded
    dev.call('m1_lighting_stop')
    assert dev.call('m1_hal_healthy')==1 and dev.u32(DMA+0x6c)&1
    print('PASS M1 linked scanner/SDK: ADC ranks, six DMA rows, bank pins, complete-frame ownership, cadence/DMA/calibration faults')


def scanner_capture(elf):
    timeout=D['M1_WAKE_SCAN_TIMEOUT_US']
    dev=M1Arm(elf,scanner=True)
    assert not dev.call('m1_hal_capture_start',0)
    assert dev.call('m1_hal_init')==1
    for start in (0xfffffff0,10000,20000):
        assert dev.call('m1_hal_capture_start',start)==1
        assert dev.call('m1_hal_capture_busy')==1
        assert not dev.u32(TMR6)&1 and dev.u32(TMR3)&1 and dev.u32(ADC+8)&1
        assert not dev.call('m1_hal_start') and not dev.call('m1_hal_capture_start',start+1)
        # A stale/spurious periodic IRQ cannot restart/overrun a one-shot.
        dev.put(TMR6+0x10,1);dev.call('m1_hal_timer_irq')
        for bank in range(6):
            assert not dev.call('m1_hal_frame',RGB,RGB+200)
            assert dev.u32(DMA+0x70)==15 and dev.u32(DMA+0x6c)&1
            assert (dev.u32(GPIO+0x414)>>7)&7==[0,6,2,4,3,1][bank]
            dev.cpu.mem_write(dev.u32(DMA+0x78),struct.pack('<15H',*(2000+bank*15+i for i in range(15))))
            dev.put(DMA,3<<20);dev.call('m1_hal_dma_irq')
            assert dev.call('m1_hal_capture_busy')==(bank!=5)
        assert dev.call('m1_hal_healthy')==1
        assert not dev.u32(TMR3)&1 and not dev.u32(TMR6)&1 and not dev.u32(ADC+8)&1
        assert not dev.u32(DMA+0x6c)&1
        assert not dev.call('m1_hal_start') and not dev.call('m1_hal_capture_start',start+20)
        dev.call('m1_hal_service',start+timeout+1)  # completed frame cannot timeout
        assert dev.call('m1_hal_frame',RGB,RGB+200)==1
        actual=struct.unpack('<82H',dev.cpu.mem_read(RGB,164))
        assert actual==tuple(2001+r[1]*15+r[2] for r in m1_records())
        assert dev.u32(RGB+200)==(1 if start==0xfffffff0 else start//10000+1)
    assert dev.call('m1_hal_start')==1 and dev.u32(ADC+8)&1 and dev.u32(TMR6)&1
    assert not dev.call('m1_hal_capture_start',30000)
    dev.call('m1_hal_stop');assert not dev.call('m1_hal_capture_start',30001)
    for reason in ('deadline','dma error','bad sample','stop'):
        dev=M1Arm(elf,scanner=True);assert dev.call('m1_hal_init')==1
        assert dev.call('m1_hal_capture_start',0xfffffff0)==1
        dev.call('m1_hal_service',0xfffffff0+timeout-1)
        assert dev.call('m1_hal_capture_busy')==1
        if reason=='deadline':dev.call('m1_hal_service',0xfffffff0+timeout)
        elif reason=='dma error':dev.put(DMA,8<<20);dev.call('m1_hal_dma_irq')
        elif reason=='bad sample':
            dev.cpu.mem_write(dev.u32(DMA+0x78),struct.pack('<15H',*([4096]*15)))
            dev.put(DMA,3<<20);dev.call('m1_hal_dma_irq')
        else:dev.call('m1_hal_stop')
        assert not dev.call('m1_hal_healthy') and not dev.call('m1_hal_capture_busy'),reason
        assert not dev.call('m1_hal_frame',RGB,RGB+200)
        assert not dev.u32(DMA+0x6c)&1 and not dev.u32(TMR3)&1 and not dev.u32(ADC+8)&1
        assert dev.call('m1_hal_init')==1 and dev.call('m1_hal_capture_start',1)==1
    print('PASS M1 one-shot capture: six-bank ownership, repeat/periodic transitions, deadline/wrap and faults')


class StartupArm(M1Arm):
    """Script oscillator readiness/source status; execute real SDK clock code."""
    def __init__(self,elf,failure=None):
        super().__init__(elf,scanner=True)
        self.failure=failure
        self.put(CRM,0x03030003)  # HICK, HEXT, PLL enabled and stable
        self.put(CRM+8,self.u32(CRM+8)|2)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.clock_read,begin=CRM,end=CRM+0xff)

    def clock_read(self,cpu,access,address,size,value,user):
        if address==CRM:
            control=self.u32(CRM)
            for name,enable,stable in (('hick',0,1),('hext',16,17),('pll',24,25)):
                ready=bool(control&(1<<enable))
                if self.failure==name:ready=False
                if self.failure==name+'_stop':ready=True
                control=(control&~(1<<stable))|(int(ready)<<stable)
            self.put(CRM,control)
        elif address==CRM+8:
            config=self.u32(CRM+8)
            source=config&3
            if self.failure!=('hick_switch' if source==0 else 'pll_switch'):
                self.put(CRM+8,(config&~12)|(source<<2))

    def write(self,cpu,access,address,size,value,user):
        if address in (CRM,CRM+4,CRM+8,CRM+0xa0,CRM+0xa4,0x40007010,0x40023c00):
            if address==0x40007010 and (value^self.u32(address))&3:
                assert (self.u32(CRM+8)>>2)&3 in (0,1), 'voltage changed on PLL'
            self.writes.append((address,size,value))
            return
        if address==GPIO+0x818:self.put(GPIO+0x814,self.u32(GPIO+0x814)|value)
        if address==GPIO+0x828:self.put(GPIO+0x814,self.u32(GPIO+0x814)&~value)
        super().write(cpu,access,address,size,value,user)

    def check_guards(self):
        for start,end in ((0x1c,0x6c),(0x80,0x100)):
            assert bytes(self.cpu.mem_read(DMA+start,end-start))==b'\x5a'*(end-start)
        # Radio SPI3 pins and encoder inputs remain outside startup ownership.
        for port,pins in ((GPIO+0x400,(3,4,5,10)),(GPIO+0x800,(10,11,12))):
            for pin in pins:
                for offset,width in ((0,2),(4,1),(8,2),(12,2),(20,1)):
                    mask=((1<<width)-1)<<(width*pin)
                    assert self.u32(port+offset)&mask==0xa5a5a5a5&mask


def startup(elf):
    dev=StartupArm(elf)
    assert dev.call('m1_clock_init')==1 and not dev.writes  # interrupts must be masked
    dev.cpu.reg_write(UC_ARM_REG_PRIMASK,1)
    assert dev.call('m1_clock_init',instructions=3000000)==0
    assert dev.u32(dev.symbols['system_core_clock'])==216000000
    assert dev.u32(0x40023c00)==0x156  # latency only, never FLASH CTRL/erase/key
    assert dev.u32(CRM+4)&(1<<29)==0  # USB PLL output remains disabled
    assert dev.u32(CRM+8)&15==10
    # Every wait must have a finite failure path. No PLL changes before a
    # proven HICK source switch, no HEXT bypass changes while HEXT is live.
    for failure,code in (('hick',2),('hick_switch',3),('pll_stop',4),
                         ('hext_stop',9),('hext',5),('pll',6),('pll_switch',7)):
        dev=StartupArm(elf,failure)
        dev.cpu.reg_write(UC_ARM_REG_PRIMASK,1)
        assert dev.call('m1_clock_init',instructions=3000000)==code,failure
        if failure in ('hick','hick_switch'):
            assert not any(a==CRM+4 for a,_,_ in dev.writes)
            assert dev.u32(CRM)&(1<<24)
    # The present rail sequencer is ONLY the verified PC13-low startup path.
    # Wireless cold-start/sleep requires a separate RTC/scan/radio integration.
    for start in (0,0xfffffff0):
        dev=StartupArm(elf)
        dev.put(GPIO+0x810,0)
        assert dev.call('m1_startup_begin',start)==1
        assert dev.u32(GPIO+0x414)&(1<<6) and not dev.u32(GPIO+0x814)&(1<<14)
        assert dev.call('m1_startup_begin',start)==0
        stage=D['M1_POWER_STAGE_MS']
        for delta in (stage-1,stage,2*stage-1):
            dev.call('m1_startup_service',start+delta)
            assert not dev.u32(GPIO+0x814)&(1<<14)
        dev.call('m1_startup_service',start+2*stage)
        assert dev.u32(GPIO+0x814)&(1<<14)
        dev.call('m1_startup_service',start+3*stage)
        assert dev.u32(GPIO+0x414)&(1<<13) and dev.u32(GPIO+0x814)&(1<<6)
        assert dev.call('m1_startup_ready')==0
        dev.call('m1_startup_service',start+3*stage+D['M1_SENSOR_SETTLE_MS'])
        assert dev.call('m1_startup_ready')==1
        assert dev.u32(TMR6)&1
        dev.put(GPIO+0x810,1<<13)  # do not run wired rail sequencing on battery
        dev.call('m1_startup_service',start+4*stage)
        assert dev.call('m1_startup_fault')==1 and dev.call('m1_startup_ready')==0
        assert not dev.u32(GPIO+0x414)&((1<<6)|(1<<13))
        assert not dev.u32(GPIO+0x814)&((1<<6)|(1<<14))
        assert not dev.u32(TMR6)&1
        dev.call('m1_startup_stop')
        dev.put(GPIO+0x810,0)
        assert dev.call('m1_startup_begin',start+5*stage)==1
    print('PASS M1 clock/wired startup: SDK sequence, bounded readiness faults, rail ordering, sensor settle, cable loss and wraparound')


def battery(elf):
    dev=M1Arm(elf,scanner=True)
    dev.call('m1_battery_hal_service',0)
    assert dev.call('m1_test_battery_status')==0
    dev.call('m1_battery_hal_init')
    for port,pin in ((GPIO+0x400,10),(GPIO+0x800,13)):
        assert not (dev.u32(port)>>(pin*2))&3
        assert (dev.u32(port+12)>>(pin*2))&3==1
    assert dev.call('m1_hal_init')==1 and dev.call('m1_hal_start')==1
    now=0xfffffff0
    def sample(adc,external=False,charger_high=False):
        nonlocal now
        dev.put(GPIO+0x810,0 if external else 1<<13)
        dev.put(GPIO+0x410,(1<<10) if charger_high else 0)
        dev.put(TMR6+0x10,1);dev.call('m1_hal_timer_irq')
        for bank in range(6):
            row=[3900]*15
            if bank==5:row[4]=adc
            dev.cpu.mem_write(dev.u32(DMA+0x78),struct.pack('<15H',*row))
            dev.put(DMA,3<<20);dev.call('m1_hal_dma_irq')
        dev.call('m1_battery_hal_service',now)
        now=(now+D['M1_BATTERY_SAMPLE_MS'])&0xffffffff
    sample(1280)
    for i in range(20):dev.call('m1_battery_hal_service',now)  # no new DMA frame
    assert dev.call('m1_test_battery_status')==0
    for i in range(D['M1_BATTERY_FILTER_SAMPLES']-1):sample(1280)
    assert dev.call('m1_test_battery_status')&0xffff==0x100|20
    for i in range(D['M1_CHARGER_CONFIRM_SAMPLES']):sample(1705,True,True)
    assert dev.call('m1_test_battery_status')==0x30163  # known,99%, PB10 high
    dev.call('m1_battery_hal_service',now+D['SCAN_STALE_MS'])
    assert dev.call('m1_test_battery_status')==0  # stale is unknown, never low/full
    # Scanner faults invalidate the charge indication, not just key reports.
    sample(1280)
    dev.put(DMA,8<<20);dev.call('m1_hal_dma_irq')
    dev.call('m1_battery_hal_service',now)
    assert dev.call('m1_test_battery_status')==0
    print('PASS M1 battery HAL: PB10/PC13 input-only, complete DMA sample, no replay, cable changes, stale/fault invalidation')


RTC, EXINT, PWC, SYSTICK, SCR = 0x40002800,0x40013c00,0x40007000,0xe000e010,0xe000ed10


class SleepArm(StartupArm):
    def __init__(self,elf,failure=None):
        super().__init__(elf,failure)
        self.cpu.mem_map(0xe0042000,0x1000)
        self.rtc_status=0
        self.unlocked=False
        self.unlock_first=False
        self.wakes=0
        self.rtc_wake=True
        self.resume_failure=None
        self.put(PWC+16,3)
        self.put(GPIO+0x414,self.u32(GPIO+0x414)&~((1<<6)|(1<<13)))
        self.put(GPIO+0x814,self.u32(GPIO+0x814)&~((1<<6)|(1<<14)))
        self.cpu.mem_write(RTC+0x50,b'\x79'*80)
        self.put(RTC,0x00112233);self.put(RTC+4,0x00245678)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.rtc_read,begin=RTC+12,end=RTC+15)
        address=self.symbols['pwc_deep_sleep_mode_enter']&~1
        self.cpu.hook_add(UC_HOOK_CODE,self.sleep,begin=address,end=address+0x28)

    def clock_read(self,cpu,access,address,size,value,user):
        super().clock_read(cpu,access,address,size,value,user)
        if address==CRM+0x74:
            ready=bool(self.u32(address)&1) and self.failure!='lick'
            self.put(address,(self.u32(address)&~2)|(int(ready)<<1))

    def rtc_read(self,cpu,access,address,size,value,user):
        status=self.rtc_status
        for bit,ready in ((2,not self.u32(RTC+8)&(1<<10) and self.failure!='wat'),
                          (6,bool(status&128) and self.failure!='rtc_init'),
                          (5,not status&128 and self.failure!='rtc_sync')):
            status=(status&~(1<<bit))|(int(ready)<<bit)
        self.rtc_status=status
        self.put(RTC+12,status)

    def write(self,cpu,access,address,size,value,user):
        if RTC<=address<RTC+0x28:
            self.writes.append((address,size,value))
            assert address not in (RTC,RTC+4), 'sleep must not replace the calendar/date'
            if address==RTC+0x24:
                if value==0xca:self.unlock_first=True
                elif value==0x53:
                    assert self.unlock_first
                    self.unlocked=True;self.unlock_first=False
                elif value==0xff:self.unlocked=False
                else:raise AssertionError('invalid RTC protection key')
            elif address==RTC+12:
                # W0C event/update flags; IMEN is writable, readiness is modeled
                # on read. Ones do not manufacture pending hardware events.
                self.rtc_status=(self.rtc_status&~((~value)&0x7f20)&~128)|(value&128)
            else:assert self.unlocked, f'protected RTC write {address:08x}'
            return
        if (EXINT<=address<EXINT+0x18 or address in
                (CRM+0x70,CRM+0x74,PWC,SYSTICK,SCR)):
            assert address!=CRM+0x70 or not value&(1<<16), 'backup reset forbidden'
            self.writes.append((address,size,value));return
        super().write(cpu,access,address,size,value,user)

    def check_guards(self):
        super().check_guards()
        assert bytes(self.cpu.mem_read(RTC+0x50,80))==b'\x79'*80
        assert self.u32(RTC)==0x00112233 and self.u32(RTC+4)==0x00245678

    def sleep(self,cpu,address,size,user):
        if bytes(cpu.mem_read(address,2))!=b'\x30\xbf':return
        self.wakes+=1
        assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        assert not self.u32(SYSTICK)&1 and self.u32(SCR)&4
        assert self.u32(CRM+8)&12==0  # HICK proven before lowering voltage
        assert self.u32(PWC+16)&3==0
        assert self.u32(PWC)&1
        assert self.u32(RTC+8)&0x4400==0x4400
        assert not self.unlocked
        # Simulate the wake boundary, not RTC progression or architectural IRQ
        # entry. The PRIMASK-protected handler runs after clock restoration.
        self.put(CRM,self.u32(CRM)&~0x03030000)
        if self.rtc_wake:self.rtc_status|=1<<10
        self.failure=self.resume_failure
        cpu.reg_write(UC_ARM_REG_PC,(address+2)|1)


def sleep_hal(elf):
    dev=SleepArm(elf)
    assert dev.call('m1_sleep_wait',30,1)==2 and not dev.writes
    dev.put(CRM+0x70,1<<8)  # existing LEXT domain must not be reset/reassigned
    assert dev.call('m1_sleep_init')==0 and not dev.writes
    dev=SleepArm(elf,'lick')
    assert dev.call('m1_sleep_init',instructions=3000000)==0
    assert dev.call('m1_sleep_ready')==0
    for mask,ticks,tickctrl,wake in ((0,1,7,True),(1,30,6,True),(0,65536,5,False)):
        dev=SleepArm(elf)
        dev.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
        assert dev.call('m1_sleep_init')==1
        assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert dev.u32(RTC+16)==0x70007 and dev.u32(RTC+8)&7==4
        assert dev.u32(EXINT)&(1<<22) and dev.u32(EXINT+8)&(1<<22)
        assert not dev.u32(EXINT+4)&(1<<22) and not dev.u32(EXINT+12)&(1<<22)
        assert not dev.u32(RTC+8)&0x4400 and not dev.unlocked
        dev.put(SYSTICK,tickctrl);dev.rtc_wake=wake
        writes=len(dev.writes)
        assert dev.call('m1_sleep_wait',ticks,0)==4 and len(dev.writes)==writes
        assert dev.call('m1_sleep_wait',0,1)==2 and len(dev.writes)==writes
        assert dev.call('m1_sleep_wait',65537,1)==2 and len(dev.writes)==writes
        assert dev.call('m1_sleep_wait',ticks,1,instructions=3000000)==(0 if wake else 1)
        assert dev.wakes==1 and dev.u32(RTC+20)&65535==ticks-1
        assert dev.u32(SYSTICK)==tickctrl and not dev.u32(SCR)&4
        assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert dev.u32(dev.symbols['system_core_clock'])==216000000
        assert not dev.u32(RTC+8)&0x4400 and not dev.unlocked
        # A second cycle must not inherit the previous timer-wake flag.
        dev.rtc_wake=False
        assert dev.call('m1_sleep_wait',8,1)==1 and dev.wakes==2
    for reg in (UC_ARM_REG_BASEPRI,UC_ARM_REG_FAULTMASK):
        dev=SleepArm(elf);assert dev.call('m1_sleep_init')==1
        dev.cpu.reg_write(reg,0x20 if reg==UC_ARM_REG_BASEPRI else 1)
        writes=len(dev.writes)
        assert dev.call('m1_sleep_wait',30,1)==3 and len(dev.writes)==writes
    for address,bit in ((DMA+8,1),(DMA+0x1c,1),(DMA+0x30,1),(DMA+0x6c,1),
            (TMR3,1),(TMR6,1),(PWC,2),(0xe0042004,2),(ADC+8,1),
            (SPI+8,128),(0x40003c08,128),(GPIO+0x414,1<<6),
            (GPIO+0x414,1<<13),(GPIO+0x814,1<<6),(GPIO+0x814,1<<14)):
        dev=SleepArm(elf);assert dev.call('m1_sleep_init')==1
        dev.put(address,dev.u32(address)|bit)
        writes=len(dev.writes)
        # Calling without generic RAM guards here allows the deliberately
        # changed radio DMA control word; no firmware write is permitted.
        dev.check_guards=lambda:None
        assert dev.call('m1_sleep_wait',30,1)==4 and len(dev.writes)==writes
    dev=SleepArm(elf);assert dev.call('m1_sleep_init')==1
    dev.put(SYSTICK,7);dev.resume_failure='hext'
    assert dev.call('m1_sleep_wait',30,1,instructions=3000000)==7
    assert dev.wakes==1 and dev.call('m1_sleep_ready')==0
    assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==1 and not dev.u32(SYSTICK)&1
    assert not dev.u32(RTC+8)&0x4400
    # Run the real SDK timeout loops, without changing their constants or
    # intercepting return values. These are bounded failure tests, not sleeps.
    for failure in ('wat','rtc_init','rtc_sync'):
        dev=SleepArm(elf,failure)
        assert dev.call('m1_sleep_init',instructions=100000000)==0,failure
        assert dev.call('m1_sleep_ready')==0 and not dev.wakes and not dev.unlocked
        assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==0
    dev=SleepArm(elf);assert dev.call('m1_sleep_init')==1
    dev.resume_failure='wat';dev.put(SYSTICK,7)
    assert dev.call('m1_sleep_wait',30,1,instructions=100000000)==5
    assert dev.wakes==1 and dev.call('m1_sleep_ready')==0
    assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==0 and dev.u32(SYSTICK)==7
    print('PASS M1 RTC sleep HAL: SDK initialization, backup preservation, masked wake/clock restoration, early wake, quiescence gates and fatal resume')


RADIO_SPI = 0x40003c00
RADIO_PULSE = D['M1_RADIO_START_PULSE_US']
RADIO_TIMEOUT = D['M1_RADIO_TRANSFER_TIMEOUT_US']
PACKET, RADIO_OUT, RADIO_LENGTH = RAM+0xd000, RAM+0xd100, RAM+0xd200


class RadioArm(M1Arm):
    def __init__(self,elf,coexist=False):
        super().__init__(elf,scanner=coexist)
        # Seed the remaining ports too, to detect modifications to unrelated pins.
        self.cpu.mem_write(GPIO+0xc00,b'\xa5'*0x40)
        self.put(GPIO+0xc10,4)  # PD2 inactive

    def write(self,cpu,access,address,size,value,user):
        extra=(RADIO_SPI<=address<RADIO_SPI+0x24 or
            GPIO+0x400<=address<GPIO+0x440 or GPIO+0xc00<=address<GPIO+0xc40 or
            DMA+0x1c<=address<DMA+0x44 or address in (DMA+0x108,DMA+0x10c))
        if extra:self.writes.append((address,size,value))
        else:super().write(cpu,access,address,size,value,user)
        if address==CRM+0x20 and value&(1<<15):
            self.cpu.mem_write(RADIO_SPI,bytes(0x24));self.put(RADIO_SPI+8,2)
        for port in (GPIO,GPIO+0x400,GPIO+0xc00):
            if address==port+0x18:self.put(port+0x14,self.u32(port+0x14)|value)
            if address==port+0x28:self.put(port+0x14,self.u32(port+0x14)&~value)

    def check_guards(self):
        # Pins outside these explicit ownership sets must retain their values.
        ports=((GPIO,{15,10}|(set(range(8)) if self.scanner else set()),0),
            (GPIO+0x400,{3,4,5}|({0,7,8,9} if self.scanner else set()),0xa5a5a5a5),
            (GPIO+0x800,set(range(6)) if self.scanner else set(),0xa5a5a5a5),
            (GPIO+0xc00,{2},0xa5a5a5a5))
        for port,owned,initial in ports:
            for pin in set(range(16))-owned:
                for offset,width in ((0,2),(4,1),(8,2),(12,2),(20,1)):
                    mask=((1<<width)-1)<<(width*pin)
                    assert self.u32(port+offset)&mask==initial&mask,(hex(port),pin,offset)
                offset=0x20+4*(pin//8);mask=15<<(4*(pin%8))
                assert self.u32(port+offset)&mask==initial&mask
        ranges=[(0x44,0x6c),(0x80,0x100)]
        if not self.scanner:ranges.append((0x6c,0x80))
        for start,end in ranges:
            assert bytes(self.cpu.mem_read(DMA+start,end-start))==b'\x5a'*(end-start)

    def radio_ready(self):return bool(self.call('m1_radio_ready'))

    def radio_init(self,now=0):
        assert self.call('m1_radio_init',now)==1
        assert self.call('m1_radio_healthy')==1 and not self.radio_ready()
        assert not self.u32(GPIO+0x14)&(1<<15)
        assert (self.u32(GPIO)>>30)&3==1 and (self.u32(GPIO+12)>>30)&3==1
        assert (self.u32(GPIO+0xc00)>>4)&3==0 and (self.u32(GPIO+0xc0c)>>4)&3==1
        for pin in (3,4,5):
            assert (self.u32(GPIO+0x400)>>(pin*2))&3==2
            assert (self.u32(GPIO+0x420)>>(pin*4))&15==6
            assert (self.u32(GPIO+0x40c)>>(pin*2))&3==0
        assert self.u32(RADIO_SPI)==0x35d  # full duplex, mode 1, /16, software CS
        assert not self.u32(RADIO_SPI+4)&3
        assert not self.u32(DMA+0x1c)&1 and not self.u32(DMA+0x30)&1
        assert self.call('m1_radio_data_pending')==0
        writes=len(self.writes)
        assert self.call('m1_radio_init',now+1)==0 and len(self.writes)==writes
        self.call('m1_radio_service',now+RADIO_PULSE-1);assert not self.radio_ready()
        self.call('m1_radio_service',now+RADIO_PULSE);assert self.radio_ready()
        assert self.u32(GPIO+0x14)&(1<<15)
        return (now+RADIO_PULSE)&0xffffffff

    def exchange(self,now,size=68):
        payload=bytes((i*37+9)&255 for i in range(size))
        self.cpu.mem_write(PACKET,payload+bytes(80-size)+bytes([size]))
        start=len(self.writes)
        assert self.call('m1_radio_exchange',PACKET,now)==1 and not self.radio_ready()
        assert self.u32(DMA+0x1c)==0x2091 and self.u32(DMA+0x30)==0x2081
        assert self.u32(DMA+0x20)==size and self.u32(DMA+0x34)==size
        assert self.u32(DMA+0x24)==RADIO_SPI+12 and self.u32(DMA+0x38)==RADIO_SPI+12
        assert self.u32(DMA+0x108)&127==15 and self.u32(DMA+0x10c)&127==14
        assert self.u32(RADIO_SPI+4)&3==3 and not self.u32(GPIO+0x14)&(1<<15)
        tx,rx=self.u32(DMA+0x28),self.u32(DMA+0x3c)
        assert bytes(self.cpu.mem_read(tx,size))==payload
        assert bytes(self.cpu.mem_read(rx,80))==bytes(80)
        events=self.writes[start:]
        rx_on=next(i for i,(a,_,v) in enumerate(events) if a==DMA+0x30 and v&1)
        cs_low=next(i for i,(a,_,v) in enumerate(events) if a==GPIO+0x28 and v&(1<<15))
        tx_on=next(i for i,(a,_,v) in enumerate(events) if a==DMA+0x1c and v&1)
        assert rx_on<cs_low<tx_on
        self.cpu.mem_write(PACKET,bytes(80))
        assert bytes(self.cpu.mem_read(tx,size))==payload
        assert not self.call('m1_radio_exchange',PACKET,now+1)
        return tx,rx,payload


def radio(elf):
    dev=RadioArm(elf);dev.put(CRM+8,0)
    assert dev.call('m1_radio_init',0)==0 and not dev.writes
    assert dev.call('m1_radio_errors')==1
    dev=RadioArm(elf);now=dev.radio_init(0xfffff000)
    assert not dev.call('m1_radio_data_pending')
    dev.put(GPIO+0xc10,0);assert dev.call('m1_radio_data_pending')==1
    for size in (0,1,3,5,79,81,255):
        dev.cpu.mem_write(PACKET,bytes(80)+bytes([size]));writes=len(dev.writes)
        assert not dev.call('m1_radio_exchange',PACKET,now) and len(dev.writes)==writes
    assert not dev.call('m1_radio_exchange',0,now)
    for size in (4,68,80):
        tx,rx,payload=dev.exchange(now,size)
        assert not dev.call('m1_radio_quiesce',1)
        dev.cpu.mem_write(rx,payload[::-1])
        dev.put(DMA,(3<<4)|(8<<20))  # TX complete, unrelated scanner error survives
        dev.call('m1_radio_service',now+1)
        assert dev.u32(DMA+0x30)&1 and not dev.u32(GPIO+0x14)&(1<<15)
        dev.put(DMA,(3<<8)|(8<<20))  # RX alone is insufficient too
        dev.call('m1_radio_service',now+2);assert dev.u32(DMA+0x1c)&1
        dev.put(DMA,(3<<4)|(3<<8)|(8<<20));dev.put(RADIO_SPI+8,0x82)
        dev.call('m1_radio_service',now+3)
        assert not dev.u32(DMA+0x1c)&1 and not dev.u32(DMA+0x30)&1
        assert dev.u32(DMA)==8<<20 and not dev.u32(GPIO+0x14)&(1<<15)
        dev.put(RADIO_SPI+8,0);dev.call('m1_radio_service',now+4)
        assert not dev.u32(GPIO+0x14)&(1<<15)
        dev.put(RADIO_SPI+8,2);dev.call('m1_radio_service',now+5)
        assert dev.u32(GPIO+0x14)&(1<<15) and not dev.radio_ready()
        assert not dev.call('m1_radio_quiesce',1)
        assert not dev.call('m1_radio_exchange',PACKET,now+6)
        for out,capacity,length in ((0,80,RADIO_LENGTH),(RADIO_OUT,size-1,RADIO_LENGTH),(RADIO_OUT,80,0)):
            assert not dev.call('m1_radio_take',out,capacity,length)
        assert dev.call('m1_radio_take',RADIO_OUT,80,RADIO_LENGTH)==1
        assert dev.u32(RADIO_LENGTH)==size and bytes(dev.cpu.mem_read(RADIO_OUT,size))==payload[::-1]
        assert dev.radio_ready() and not dev.call('m1_radio_take',RADIO_OUT,80,RADIO_LENGTH)
        now+=10
    writes=len(dev.writes)
    assert not dev.call('m1_radio_quiesce',0) and len(dev.writes)==writes
    assert dev.call('m1_radio_quiesce',1)==1 and not dev.call('m1_radio_healthy')
    assert not dev.u32(RADIO_SPI)&64 and not dev.u32(RADIO_SPI+4)&3
    for port,pins in ((GPIO+0x400,(3,4,5)),(GPIO+0xc00,(2,))):
        for pin in pins:
            assert (dev.u32(port)>>(pin*2))&3==1
            assert (dev.u32(port+12)>>(pin*2))&3==0
            assert not dev.u32(port+20)&(1<<pin)
    dev.radio_init(now)
    for address,bit in ((RADIO_SPI+8,0x80),(RADIO_SPI+8,0x20),(RADIO_SPI+8,0x40),
                        (DMA+0x1c,1),(DMA+0x30,1)):
        dev=RadioArm(elf);now=dev.radio_init()
        dev.put(address,dev.u32(address)|bit)
        dev.cpu.mem_write(PACKET,bytes(80)+bytes([4]))
        if bit in (0x80,1):
            writes=len(dev.writes)
            assert not dev.call('m1_radio_quiesce',1) and len(dev.writes)==writes
        assert not dev.call('m1_radio_exchange',PACKET,now)
        assert not dev.call('m1_radio_healthy') and dev.call('m1_radio_errors')==1
    for reason in ('tx error','rx error','master error','overrun','transfer timeout','drain timeout'):
        dev=RadioArm(elf);now=dev.radio_init(0xffffd000);dev.exchange(now)
        if reason=='tx error':dev.put(DMA,8<<4)
        elif reason=='rx error':dev.put(DMA,8<<8)
        elif reason=='master error':dev.put(RADIO_SPI+8,0x20)
        elif reason=='overrun':dev.put(RADIO_SPI+8,0x40)
        elif reason=='drain timeout':
            dev.put(DMA,0x330);dev.put(RADIO_SPI+8,0x82)
            dev.call('m1_radio_service',now+1)
        dev.call('m1_radio_service',now+(RADIO_TIMEOUT if 'timeout' in reason else 2))
        assert not dev.call('m1_radio_healthy') and dev.call('m1_radio_errors')==1,reason
        assert not dev.u32(DMA+0x1c)&1 and not dev.u32(DMA+0x30)&1
        assert not dev.u32(RADIO_SPI)&64 and dev.u32(GPIO+0x14)&(1<<15)
        assert (dev.u32(GPIO+0xc00)>>4)&3==0  # don't drive an awake peer on errors
        dev.radio_init(now+RADIO_TIMEOUT+1)
    # Radio stop/init/transfer must not disturb concurrently active LED/scan DMA.
    dev=RadioArm(elf,coexist=True)
    assert dev.call('m1_hal_init')==1 and dev.call('m1_hal_start')==1
    now=dev.init();dev.offer(now)
    dev.put(TMR6+0x10,1);dev.call('m1_hal_timer_irq')
    led=bytes(dev.cpu.mem_read(DMA+8,20));scan=bytes(dev.cpu.mem_read(DMA+0x6c,20))
    now=dev.radio_init(now);dev.exchange(now);dev.call('m1_radio_stop')
    assert bytes(dev.cpu.mem_read(DMA+8,20))==led
    assert bytes(dev.cpu.mem_read(DMA+0x6c,20))==scan
    assert dev.u32(TMR3)&1 and dev.u32(TMR6)&1
    print('PASS M1 SPI3 radio: transfer bounds, pin/DMA ownership, RX/TX ordering, drain/backpressure, faults, wraparound and quiescence')


USB_HS = 0x40040000


class UsbPowerArm(M1Arm):
    def __init__(self,elf,ready_after=1,cable_at=0):
        super().__init__(elf)
        self.cpu.mem_map(USB_HS,0x10000)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=USB_HS,end=USB_HS+0xffff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_usb,begin=USB_HS,end=USB_HS+0xffff)
        self.ready_after,self.cable_at=ready_after,cable_at
        self.status_reads=0
        self.put(CRM,1<<17)  # HEXT stable, the only admitted SDK branch
        self.put(CRM+0x30,4)  # PC13 GPIO clock on, OTGHS core clock off

    def write(self,cpu,access,address,size,value,user):
        if address in (CRM+0x78,USB_HS+0x38,USB_HS+0xc,USB_HS+0x804,USB_HS+0xe00):
            self.writes.append((address,size,value))
            assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        else:
            # No broader peripheral writes are authorized for this helper.
            assert address==CRM+0x30,hex(address)
            super().write(cpu,access,address,size,value,user)

    def read_usb(self,cpu,access,address,size,value,user):
        assert self.u32(CRM+0x30)&(1<<29),'read from an unclocked USB core'
        if address==USB_HS+0x808:
            self.status_reads+=1
            if self.ready_after and self.status_reads>=self.ready_after:self.put(address,1)
            if self.cable_at and self.status_reads>=self.cable_at:
                self.put(GPIO+0x810,self.u32(GPIO+0x810)&~(1<<13))


def usb_power(elf):
    for initial_mask,ready_after in ((0,1),(1,4),(0,216000)):
        dev=UsbPowerArm(elf,ready_after)
        dev.cpu.reg_write(UC_ARM_REG_PRIMASK,initial_mask)
        assert not dev.call('m1_usb_power_ready')
        assert dev.call('m1_usb_power_down',1,instructions=5000000)==0
        assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==initial_mask
        assert dev.call('m1_usb_power_ready')==1
        assert dev.status_reads==ready_after+1  # SDK poll plus checked result
        assert dev.u32(USB_HS+0x38)==0x600000 and dev.u32(USB_HS+0xe00)&1
        assert dev.u32(USB_HS+0xc)&(1<<30) and not dev.u32(USB_HS+0x804)&2
        assert dev.u32(CRM+0x78)==0 and dev.u32(CRM+0x30)==(1<<29)|4
        writes=len(dev.writes)
        assert dev.call('m1_usb_power_down',1)==0 and len(dev.writes)==writes
        dev.call('m1_usb_power_invalidate');assert not dev.call('m1_usb_power_ready')
        assert len(dev.writes)==writes
    dev=UsbPowerArm(elf,ready_after=0)
    assert dev.call('m1_usb_power_down',1,instructions=5000000)==5
    assert dev.status_reads==216001 and not dev.call('m1_usb_power_ready')
    assert not dev.cpu.reg_read(UC_ARM_REG_PRIMASK)
    # Failure checks must not shut down a live stack or touch GPIO/flash/clocks.
    cases=[(CRM,1<<25,3),(CRM+8,0,3),(CRM+0x30,0,4),
           (GPIO+0x810,0,4),(TMR3,1,2),(TMR6,1,2),(ADC+8,1,2),
           (SPI+8,128,2),(RADIO_SPI+8,128,2)]
    cases.extend((DMA+offset,1,2) for offset in (8,0x1c,0x30,0x6c))
    cases.extend((0xe000e108,1<<irq,2) for irq in (10,11,12,13))
    cases.extend((USB_HS+offset,1,2) for offset in (8,0x14)) # global IRQ / host mode
    cases.extend((USB_HS+base+ep*0x20,1<<31,2) for base in (0x900,0xb00) for ep in range(8))
    for address,value,expected in cases:
        dev=UsbPowerArm(elf)
        if USB_HS<=address<USB_HS+0x10000:dev.put(CRM+0x30,(1<<29)|4)
        dev.put(address,value)
        # Deliberate changes to unrelated initial register sentinels are
        # allowed as input; every firmware peripheral write is still rejected.
        dev.check_guards=lambda:None
        assert dev.call('m1_usb_power_down',1)==expected,(hex(address),expected)
        assert not dev.writes and not dev.cpu.reg_read(UC_ARM_REG_PRIMASK)
    for register in (UC_ARM_REG_BASEPRI,UC_ARM_REG_FAULTMASK):
        dev=UsbPowerArm(elf);dev.cpu.reg_write(register,1)
        assert dev.call('m1_usb_power_down',1)==1 and not dev.writes
    dev=UsbPowerArm(elf)
    assert dev.call('m1_usb_power_down',0)==2 and not dev.writes
    dev=UsbPowerArm(elf,ready_after=4,cable_at=2)
    # Cable change is external stimulus; do not compare its input to the old guard.
    dev.check_guards=lambda:None
    assert dev.call('m1_usb_power_down',1)==4 and not dev.call('m1_usb_power_ready')
    dev=UsbPowerArm(elf);assert dev.call('m1_usb_power_down',1)==0
    dev.put(USB_HS+0x38,dev.u32(USB_HS+0x38)|(1<<16))
    assert not dev.call('m1_usb_power_ready')  # external PHY reinitialization
    print('PASS M1 USB power: official SDK HEXT path, live-stack/DMA guards, bounded timeout, cable changes and ownership')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    args=parser.parse_args()
    lighting(args.elf)
    scanner(args.elf)
    scanner_capture(args.elf)
    startup(args.elf)
    battery(args.elf)
    radio(args.elf)
    usb_power(args.elf)
    sleep_hal(args.elf)


if __name__=='__main__':main()
