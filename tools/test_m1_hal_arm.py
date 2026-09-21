"""Execute the actual M1 HAL/official SDK offline; no device access.

Only register reset/W1C/output-latch effects are modeled. DMA/SPI completion
flags and time are scripted: this is not proof of electrical output/timing.
The ELF uses a synthetic code address and has no boot header/vector table.
"""
import argparse
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_HOOK_CODE, UC_PROT_READ
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_R0,
    UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
    UC_ARM_REG_FAULTMASK, UC_ARM_REG_CONTROL, UC_ARM_REG_IPSR)
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
    assert dev.call('m1_hal_periodic_active')==1
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
    assert dev.call('m1_hal_periodic_active')==1
    dev.call('m1_hal_stop');assert not dev.call('m1_hal_capture_start',30001)
    assert dev.call('m1_hal_periodic_active')==0
    for reason in ('deadline','dma error','bad sample','stop'):
        dev=M1Arm(elf,scanner=True);assert dev.call('m1_hal_init')==1
        assert dev.call('m1_hal_capture_start',0xfffffff0)==1
        assert dev.call('m1_hal_periodic_active')==0
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
        # Scanner, LED, rails and observed wake GPIO own only these pins.
        # Preserve every other pin, including radio SPI3 and debug pins.
        for port,owned,initial in ((GPIO,set(range(8))|{10,11},0),
                (GPIO+0x400,{0,6,7,8,9,10,12,13},0xa5a5a5a5),
                (GPIO+0x800,set(range(7))|{10,11,12,13,14},0xa5a5a5a5)):
            for pin in set(range(16))-owned:
                for offset,width in ((0,2),(4,1),(8,2),(12,2),(20,1)):
                    mask=((1<<width)-1)<<(width*pin)
                    assert self.u32(port+offset)&mask==initial&mask


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
    # Wired branch, including cold GPIO restore and independently wrapped time.
    for start in (0,0xfffffff0):
        dev=StartupArm(elf)
        dev.put(GPIO+0x810,0)
        assert dev.call('m1_startup_begin',start,0)==0 and not dev.writes
        assert dev.call('m1_startup_begin',start,1)==1
        assert dev.u32(GPIO+0x414)&(1<<6) and not dev.u32(GPIO+0x814)&(1<<14)
        assert dev.call('m1_startup_begin',start,1)==0
        stage=D['M1_POWER_STAGE_MS']
        for delta in (stage-1,stage,2*stage-1):
            dev.call('m1_startup_service',start+delta,123+delta*1000)
            assert not dev.u32(GPIO+0x814)&(1<<14)
        dev.call('m1_startup_service',start+2*stage,123+2*stage*1000)
        assert dev.u32(GPIO+0x814)&(1<<14)
        dev.call('m1_startup_service',start+3*stage,123+3*stage*1000)
        assert dev.u32(GPIO+0x414)&(1<<13) and dev.u32(GPIO+0x814)&(1<<6)
        assert dev.call('m1_startup_ready')==0
        dev.call('m1_startup_service',start+4*stage,123+4*stage*1000)
        assert dev.call('m1_startup_ready')==0  # fresh timestamp after ADC calibration
        dev.call('m1_startup_service',start+4*stage+D['M1_SENSOR_SETTLE_MS'],123+5*stage*1000)
        assert dev.call('m1_startup_ready')==1
        assert dev.u32(TMR6)&1
        dev.put(GPIO+0x810,1<<13)  # do not run wired rail sequencing on battery
        assert dev.call('m1_startup_encoder_phase',RGB)==1
        dev.call('m1_startup_service',start+5*stage,123+6*stage*1000)
        assert dev.call('m1_startup_fault')==1 and dev.call('m1_startup_ready')==0
        assert not dev.u32(GPIO+0x414)&((1<<6)|(1<<13))
        assert not dev.u32(GPIO+0x814)&((1<<6)|(1<<14))
        assert not dev.u32(TMR6)&1
        dev.call('m1_startup_stop')
        dev.put(GPIO+0x810,0)
        assert dev.call('m1_startup_begin',start+6*stage,1)==1
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


class WirelessArm(RadioArm):
    def __init__(self,elf,mode=0,start=0):
        super().__init__(elf)
        self.mode=mode;self.now=self.radio_init(start)
        assert not self.call('m1_wireless_init',mode,0,self.now)
        for invalid in (3,4,6,7,255,0x100):
            assert not self.call('m1_wireless_init',invalid,1,self.now)
        assert self.call('m1_wireless_init',mode,1,self.now)
        assert not self.call('m1_wireless_init',mode,1,self.now)
        self.put(GPIO+0xc10,0)
        self.step()
        assert self.packet()==bytes((9,))+bytes(67)
        self.put(GPIO+0xc10,4)
        self.finish(bytes((0,4,0x10,0,3,mode,0x13+mode)))
        assert not self.call('m1_wireless_ready') # no mode command sent before this poll
        assert self.packet()[:4]==bytes((0x93,1,mode,mode))
        self.finish() # mode completion alone is not confirmation
        assert not self.call('m1_wireless_ready')
        assert self.packet()[:4]==bytes((0x92,1,0,0))
        self.finish()
    def step(self,us=125):
        self.now=(self.now+us)&0xffffffff
        self.call('m1_wireless_service',self.now)
    def packet(self):
        assert self.u32(DMA+0x1c)&1
        return bytes(self.cpu.mem_read(self.u32(DMA+0x28),self.u32(DMA+0x20)))
    def finish(self,reply=b''):
        size=self.u32(DMA+0x34)
        assert self.u32(DMA+0x30)&1 and len(reply)<=size
        self.cpu.mem_write(self.u32(DMA+0x3c),reply+bytes(size-len(reply)))
        self.put(DMA,0x330);self.put(RADIO_SPI+8,2);self.step()
    def poll(self,state=3,mode=None,flags=0,valid=True):
        self.put(GPIO+0xc10,0);self.step(D['M1_RADIO_POLL_US'])
        assert self.packet()==bytes((9,))+bytes(67)
        self.put(GPIO+0xc10,4)
        payload=bytes((0x10,flags,state,self.mode if mode is None else mode))
        reply=bytes((0,len(payload)))+payload+bytes(((sum(payload)+(not valid))&255,))
        self.finish(reply)
    def baseline(self):
        self.poll(flags=0x25)
        assert self.call('m1_wireless_ready') and not self.call('m1_wireless_local_idle')
        assert self.packet()[:10]==bytes((0x81,8,1))+bytes(7)
        self.finish();assert self.packet()[:18]==bytes((0x81,16,2))+bytes(15)
        self.finish();assert self.call('m1_wireless_reports_sent')==1
        assert self.call('m1_wireless_local_idle')
    def offer(self,usages=(),modifiers=0):
        report=bytearray(30);report[0]=modifiers
        for usage in usages:report[2+(usage-4)//8]|=1<<((usage-4)%8)
        self.cpu.mem_write(RADIO_OUT,bytes(report))
        return self.call('m1_wireless_offer',RADIO_OUT)


def wireless(elf):
    for mode in (0,1,2,5):
        d=WirelessArm(elf,mode,start=0xffffd000)
        assert not d.offer((4,))
        # Matching mode with not-yet-eligible states must wait, not fake a link.
        d.poll(state=0);assert not d.call('m1_wireless_ready')
        d.poll(state=1);assert d.call('m1_wireless_healthy')
        assert not d.call('m1_wireless_ready')
        d.baseline()
        assert d.call('m1_wireless_status',RADIO_OUT)
        assert bytes(d.cpu.mem_read(RADIO_OUT,3))==bytes((0x25,3,mode))
        assert not d.call('m1_wireless_status',0)
        assert d.offer(range(4,12),0xa5)
        assert not d.offer((100,)) # full pair is immutable, even before its first DMA
        gap=D['M1_RADIO_RF_REPORT_US'] if mode==5 else D['M1_RADIO_BT_REPORT_US']
        d.step(gap);first=d.packet()
        assert first[:10]==bytes((0x81,8,1,0xa5,4,5,6,7,8,9))
        assert not d.call('m1_wireless_local_idle')
        d.finish();second=d.packet()
        expected=bytearray(15)
        for usage in (10,11):expected[usage//8]|=1<<(usage%8)
        assert second[:18]==bytes((0x81,16,2))+expected
        assert d.call('m1_wireless_reports_sent')==1 # one subtype is not a pair
        assert not d.offer(())
        d.finish();assert d.call('m1_wireless_reports_sent')==2
        assert d.call('m1_wireless_local_idle')
        assert d.offer(())
        d.step(gap);assert d.packet()[:10]==bytes((0x81,8,1))+bytes(7)
        d.finish();assert d.packet()[:18]==bytes((0x81,16,2))+bytes(15)
        d.finish();assert d.call('m1_wireless_reports_sent')==3
        d.poll(mode=(mode+1)%3)
        assert not d.call('m1_wireless_healthy') and not d.offer((4,))
        assert d.call('m1_wireless_errors')==1
        writes=len(d.writes);d.step(1000);assert len(d.writes)==writes
        assert not d.call('m1_wireless_init',mode,1,d.now)
        d.call('m1_wireless_stop');d.radio_init(d.now)
        assert d.call('m1_wireless_init',mode,1,d.now+RADIO_PULSE)
    # A poll before sending the requested mode cannot confirm it. Ordinary
    # full-duplex command bytes must not be accepted as unsolicited status.
    d=WirelessArm(elf);d.poll(valid=False)
    assert d.call('m1_wireless_errors')==1 and not d.call('m1_wireless_ready')
    d.step(D['M1_RADIO_MODE_TIMEOUT_US'])
    assert not d.call('m1_wireless_healthy')
    d=WirelessArm(elf);d.baseline();assert d.offer((4,))
    d.step(D['M1_RADIO_BT_REPORT_US'])
    assert d.packet()[0]==0x81
    d.finish(bytes((0,4,0x10,0,3,0,0x13))) # plausible status, ignored outside a poll
    d.finish()
    d.step(D['M1_RADIO_STATUS_TIMEOUT_US'])
    assert not d.call('m1_wireless_ready') and not d.call('m1_wireless_healthy')
    d=WirelessArm(elf);d.baseline();assert d.offer((4,))
    d.step(D['M1_RADIO_BT_REPORT_US']);d.put(DMA,8<<8);d.step()
    assert not d.call('m1_wireless_healthy') and d.call('m1_wireless_reports_sent')==1
    assert not d.call('m1_wireless_local_idle')
    print('PASS M1 wireless: mode/status gating, neutral baseline, paired remapped reports, backpressure, stale/invalid status, DMA faults, wrap and explicit restart; no host-delivery claim')


def wireless_power(elf):
    d=WirelessArm(elf);d.baseline()
    # Real battery filter -> scheduler -> SDK DMA buffer. Incomplete/invalid
    # sampling must never become a fictitious full battery packet.
    samples=D['M1_BATTERY_FILTER_SAMPLES']
    assert not d.call('m1_test_wireless_battery',1705,samples-1,1)
    for adc,percent,pins in ((1145,1,1),(1280,20,1),(1705,100,1),(1705,99,2)):
        assert d.call('m1_test_wireless_battery',adc,samples,pins)==percent*256+1
        assert not d.call('m1_wireless_local_idle')
        d.step();assert d.packet()==bytes((0x90,1,percent,percent))
        d.finish();assert d.call('m1_wireless_battery_sent',RADIO_OUT)
        assert d.cpu.mem_read(RADIO_OUT,1)[0]==percent
        assert d.call('m1_wireless_local_idle')
    for percent in range(1,101):
        assert d.call('m1_test_wireless_battery_raw',percent,3)
        d.step();assert d.packet()==bytes((0x90,1,percent,percent));d.finish()
    assert d.call('m1_test_wireless_battery_raw',100,3)
    d.step();assert d.call('m1_wireless_local_idle') # unchanged value does not spam
    for percent,flags in ((0,3),(101,3),(255,3),(50,0),(50,1),(50,2)):
        assert not d.call('m1_test_wireless_battery_raw',percent,flags)
        assert not d.call('m1_wireless_battery_sent',RADIO_OUT)
    assert not d.call('m1_wireless_battery',0)
    assert not d.call('m1_wireless_battery_sent',0)
    # Latest-only while queued, copy-on-accept while in flight. Invalidating
    # acquisition cannot overwrite DMA or revive a stale valid percentage.
    assert d.call('m1_test_wireless_battery_raw',20,3)
    assert d.call('m1_test_wireless_battery_raw',21,3)
    d.step();assert d.packet()==bytes((0x90,1,21,21))
    assert d.call('m1_test_wireless_battery_raw',22,3)
    assert d.packet()==bytes((0x90,1,21,21))
    d.finish();assert d.packet()==bytes((0x90,1,22,22))
    assert d.call('m1_wireless_battery_sent',RADIO_OUT) and d.cpu.mem_read(RADIO_OUT,1)[0]==21
    assert not d.call('m1_test_wireless_battery',4096,samples,1)
    d.finish();assert not d.call('m1_wireless_battery_sent',RADIO_OUT)
    assert d.call('m1_wireless_local_idle')
    # Metadata cannot split an accepted key pair or starve its release.
    assert d.offer((4,)) and d.call('m1_test_wireless_battery_raw',50,3)
    d.step(D['M1_RADIO_BT_REPORT_US']);assert d.packet()[2]==1
    d.finish();assert d.packet()[2]==2
    d.finish();assert d.packet()==bytes((0x90,1,50,50))
    assert d.offer(())
    d.finish();d.step(D['M1_RADIO_BT_REPORT_US'])
    assert d.packet()[:10]==bytes((0x81,8,1))+bytes(7)
    d.finish();d.finish();assert d.call('m1_wireless_local_idle')
    for mode in (0,1,2,5):
        for critical in (False,True):
            d=WirelessArm(elf,mode,start=0xffffc000);d.baseline()
            command=3 if critical or mode==5 else 5
            for invalid in (0,1,2,4,6,255):
                assert not d.call('m1_wireless_request_sleep',invalid,1)
            assert not d.call('m1_test_wireless_power_request',mode,critical,0)
            assert d.offer((4,))
            assert not d.call('m1_test_wireless_power_request',mode,critical,1)
            d.step(D['M1_RADIO_BT_REPORT_US']);d.finish();d.finish()
            assert not d.call('m1_test_wireless_power_request',mode,critical,1) # held
            assert d.offer(())
            d.step(D['M1_RADIO_BT_REPORT_US']);d.finish()
            assert not d.call('m1_test_wireless_power_request',mode,critical,1) # half release
            d.finish()
            if mode==5:assert not d.call('m1_wireless_request_sleep',5,1)
            assert d.call('m1_test_wireless_power_request',mode,critical,1)
            assert not d.offer((4,)) and not d.call('m1_test_wireless_battery_raw',50,3)
            assert d.call('m1_test_wireless_power_status',31)==command
            assert d.call('m1_wireless_cancel_sleep')
            assert d.call('m1_wireless_ready')
            assert d.call('m1_test_wireless_power_request',mode,critical,1)
            d.step();assert d.packet()==bytes((0x94,1,command,command))
            assert not d.call('m1_wireless_cancel_sleep')
            assert not d.call('m1_wireless_sleep_sent')
            assert not d.call('m1_wireless_local_idle')
            # Neither TX completion alone nor full DMA while SPI is busy is a commit.
            d.put(DMA,0x30);d.step();assert not d.call('m1_wireless_sleep_sent')
            d.put(DMA,0x330);d.put(RADIO_SPI+8,0x82);d.step()
            assert d.call('m1_test_wireless_power_status',31)==command
            d.put(RADIO_SPI+8,2);d.step()
            assert d.call('m1_wireless_sleep_sent')==command
            assert d.call('m1_wireless_local_idle') and not d.call('m1_wireless_ready')
            for guards in range(32):
                assert d.call('m1_test_wireless_power_status',guards)==command+256+(512 if guards==31 else 0)
            # Completed handoff remains quiet beyond normal status expiry; it
            # must not restart status queries into the possibly sleeping peer.
            d.put(GPIO+0xc10,0);writes=len(d.writes)
            d.step(D['M1_RADIO_STATUS_TIMEOUT_US']*2)
            assert len(d.writes)==writes and d.call('m1_wireless_sleep_sent')==command
            assert not d.call('m1_wireless_cancel_sleep')
            if command==5:
                d.call('m1_test_wireless_power_critical')
                assert d.call('m1_test_wireless_power_status',31)==3
                assert not d.call('m1_wireless_request_sleep',3,0)
                assert d.call('m1_wireless_request_sleep',3,1)
                assert not d.call('m1_wireless_sleep_sent')
                assert d.call('m1_wireless_cancel_sleep')
                assert d.call('m1_wireless_sleep_sent')==5 and not d.call('m1_wireless_ready')
                assert d.call('m1_test_wireless_power_status',31)==3 # old completion is insufficient
                assert d.call('m1_wireless_request_sleep',3,1)
                d.step();assert d.packet()==bytes((0x94,1,3,3))
                d.finish();assert d.call('m1_test_wireless_power_status',31)==3+256+512
            d.call('m1_wireless_stop');assert not d.call('m1_wireless_sleep_sent')
    # No connected host / unsent neutral baseline: critical shutdown can still
    # send control 3. Cancelling before submission restores the baseline gate.
    d=WirelessArm(elf)
    assert not d.call('m1_wireless_request_sleep',5,1)
    assert d.call('m1_test_wireless_power_request',0,1,1)
    assert d.call('m1_wireless_cancel_sleep') and not d.call('m1_wireless_local_idle')
    assert d.call('m1_test_wireless_power_request',0,1,1)
    d.step();assert d.packet()==bytes((0x94,1,3,3));d.finish()
    assert d.call('m1_test_wireless_power_status',31)==3+256+512
    # An in-flight timeout, DMA error or overdue unsent request cannot commit.
    for failure in ('timeout','dma','unsent','late-completion'):
        d=WirelessArm(elf);d.baseline()
        assert d.call('m1_test_wireless_power_request',0,1,1)
        if failure=='unsent':d.step(D['M1_RADIO_SLEEP_TIMEOUT_US'])
        elif failure=='late-completion':
            d.step(D['M1_RADIO_SLEEP_TIMEOUT_US']-125)
            assert d.packet()[0]==0x94
            d.put(DMA,0x330);d.put(RADIO_SPI+8,2);d.step(250)
        else:
            d.step();assert d.packet()[0]==0x94
            if failure=='dma':d.put(DMA,8<<8);d.step()
            else:d.step(RADIO_TIMEOUT)
        assert not d.call('m1_wireless_healthy') and not d.call('m1_wireless_sleep_sent')
        assert d.call('m1_test_wireless_power_status',31)==3
        assert not d.call('m1_wireless_cancel_sleep')
    print('PASS M1 wireless power: filtered battery metadata, latest/copy ownership, neutral-gated sleep controls, actual idle/critical policy commit, cancellation, DMA drain, timeouts and quiet handoff; no peer-sleep claim')


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


class BatteryStartupArm(SleepArm):
    """Compose real startup/sleep/USB/scanner code with scripted hardware flags."""
    def __init__(self,elf,failure=None):
        super().__init__(elf,failure)
        self.cpu.mem_map(USB_HS,0x10000)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=USB_HS,end=USB_HS+0xffff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_usb,begin=USB_HS,end=USB_HS+0xffff)
        self.ready_after,self.cable_at,self.status_reads=1,0,0
        self.put(GPIO+0x810,(1<<13)|(1<<10))

    read_usb=UsbPowerArm.read_usb

    def write(self,cpu,access,address,size,value,user):
        if address in (CRM+0x78,USB_HS+0x38,USB_HS+0xc,USB_HS+0x804,USB_HS+0xe00):
            assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
            self.writes.append((address,size,value))
        else:super().write(cpu,access,address,size,value,user)

    def service(self,ms,us):
        self.call('m1_startup_service',ms,us,instructions=3000000)

    def begin_capture(self,start=0,us=0xfffffff0):
        assert self.call('m1_startup_begin',start,1)==1
        assert self.call('m1_power_gpio_prepared')
        self.service(start+100,us)  # stamp AFTER blocking initialization
        self.service(start+100+D['M1_POWER_STAGE_MS']-1,us)
        assert self.wakes==0
        self.service(start+100+D['M1_POWER_STAGE_MS'],us)
        assert self.wakes==1 and not self.u32(GPIO+0x414)&(1<<6)
        assert self.u32(RTC+20)==D['M1_COLD_SLEEP_TICKS']-1
        self.service(start+200,us)  # raise rails and calibrate ADC after wake
        assert self.u32(GPIO+0x414)&(1<<6) and self.u32(GPIO+0x814)&(1<<14)
        assert self.u32(GPIO+0x814)&(1<<6) and not self.u32(GPIO+0x414)&(1<<13)
        assert not self.call('m1_hal_capture_busy')
        self.service(start+201,us)  # stamp AFTER ADC calibration
        self.service(start+201,us+D['M1_COLD_SCAN_SETTLE_US']-1)
        assert not self.call('m1_hal_capture_busy')
        us+=D['M1_COLD_SCAN_SETTLE_US']
        self.service(start+201,us)
        assert self.call('m1_hal_capture_busy') and not self.u32(TMR6)&1
        return start+201,us

    def complete_capture(self):
        for bank in range(6):
            self.cpu.mem_write(self.u32(DMA+0x78),struct.pack('<15H',*([3000]*15)))
            self.put(DMA,3<<20);self.call('m1_hal_dma_irq')
        assert not self.call('m1_hal_capture_busy')

    def stopped(self):
        assert not self.call('m1_startup_ready')
        assert not self.u32(GPIO+0x414)&((1<<6)|(1<<13))
        assert not self.u32(GPIO+0x814)&((1<<6)|(1<<14))
        assert not self.u32(ADC+8)&1 and not self.u32(TMR6)&1
        assert not self.u32(DMA+0x6c)&1


def battery_startup(elf):
    for start,us,rtc_wake in ((0,0xfffffff0,True),(0xffffff80,1700,False)):
        d=BatteryStartupArm(elf);d.rtc_wake=rtc_wake
        ms,us=d.begin_capture(start,us)
        d.service(ms,us+1);assert d.call('m1_hal_capture_busy')
        d.complete_capture();d.service(ms+1,us+20)
        assert not d.call('m1_hal_frame',RGB,RGB+200)  # warmup never reaches application
        assert d.u32(GPIO+0x414)&(1<<6) and not d.u32(GPIO+0x814)&((1<<6)|(1<<14))
        stage=D['M1_POWER_STAGE_MS'];ms+=1
        d.service(ms+stage-1,us+30);assert d.call('m1_power_gpio_prepared')
        d.service(ms+stage,us+40);assert not d.call('m1_power_gpio_prepared')
        assert not (d.u32(GPIO+0x400)>>20)&3  # charge pin restored to input
        d.service(ms+2*stage-1,us+50);assert not d.u32(GPIO+0x814)&(1<<14)
        d.service(ms+2*stage,us+60);assert d.u32(GPIO+0x814)&(1<<14)
        d.service(ms+3*stage,us+70)
        assert d.u32(GPIO+0x414)&(1<<13) and not d.u32(TMR6)&1
        d.service(ms+4*stage,us+1000);assert not d.call('m1_startup_ready')
        d.service(ms+4*stage+D['M1_SENSOR_SETTLE_MS'],us+2000)
        assert d.call('m1_startup_ready') and d.u32(TMR6)&1
        assert d.call('m1_startup_encoder_phase',RGB) and d.cpu.mem_read(RGB,1)[0]==1
        assert not d.call('m1_startup_encoder_phase',0)
        d.call('m1_startup_stop');d.stopped()
    for failure in ('timeout','dma','cable','cancel'):
        d=BatteryStartupArm(elf);ms,us=d.begin_capture()
        if failure=='dma':d.put(DMA,8<<20);d.call('m1_hal_dma_irq')
        elif failure=='cable':d.put(GPIO+0x810,0)
        elif failure=='cancel':d.call('m1_startup_stop')
        d.service(ms+1,us+D['M1_WAKE_SCAN_TIMEOUT_US'])
        d.stopped();assert not d.call('m1_power_gpio_prepared')
        assert bool(d.call('m1_startup_fault'))==(failure!='cancel')
        writes=len(d.writes);d.service(ms+1000,us+1000000)
        assert len(d.writes)==writes  # no automatic restart after failure
    for failure in ('lick','phy','cable'):
        d=BatteryStartupArm(elf,'lick' if failure=='lick' else None)
        if failure=='phy':d.ready_after=0
        if failure=='cable':d.cable_at=1
        assert not d.call('m1_startup_begin',0,1,instructions=5000000)
        assert d.call('m1_startup_fault');d.stopped()
        assert not d.call('m1_power_gpio_prepared')
    # A failed clock restore must not touch GPIO/ADC afterward or unmask IRQs.
    d=BatteryStartupArm(elf);d.resume_failure='hext';d.put(SYSTICK,7)
    assert d.call('m1_startup_begin',0,1)
    d.service(100,0);d.service(100+D['M1_POWER_STAGE_MS'],0)
    assert d.call('m1_startup_clock_fatal') and d.call('m1_startup_fault')
    assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==1 and not d.u32(SYSTICK)&1
    writes=len(d.writes);d.call('m1_startup_stop');d.service(1000,1000000)
    assert not d.call('m1_startup_begin',1000,1) and len(d.writes)==writes
    print('PASS M1 battery cold startup: actual SDK sleep/PHY/GPIO/scan composition, fresh settling clocks, six-bank warmup discard, rail order, wrap, cancellation and fail-stop')


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


class PowerGpioArm(UsbPowerArm):
    def __init__(self,elf):
        super().__init__(elf)
        self.put(GPIO+0x800,self.u32(GPIO+0x800)&~(3<<26)) # PC13 input, battery power
        self.put(GPIO+0x804,self.u32(GPIO+0x804)&~(1<<13)) # SDK default push-pull
        self.cpu.mem_write(GPIO,b'\x5a'*0x40)
        self.pin_guard={p:bytes(self.cpu.mem_read(p,0x40)) for p in (GPIO,GPIO+0x400,GPIO+0x800)}
        self.input_script=[]
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_switch,begin=GPIO+0x810,end=GPIO+0x813)
        self.battery_reads=0
        address=self.symbols['m1_hal_battery']&~1
        self.cpu.hook_add(UC_HOOK_CODE,self.read_battery,begin=address,end=address)
        self.cpu.reg_write(UC_ARM_REG_PRIMASK,1);self.call('m1_battery_hal_init')
        self.cpu.reg_write(UC_ARM_REG_PRIMASK,0);self.writes.clear()
    def read_battery(self,cpu,address,size,user):
        self.battery_reads+=1
    def read_switch(self,cpu,access,address,size,value,user):
        if self.input_script:self.put(address,(self.u32(address)&~0x1c00)|self.input_script.pop(0))
    def write(self,cpu,access,address,size,value,user):
        for port in (GPIO,GPIO+0x400,GPIO+0x800):
            if port<=address<port+0x40:
                assert address-port in (0,4,8,12,0x28),hex(address)
                assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
                self.writes.append((address,size,value))
                if address==port+0x28:self.put(port+0x14,self.u32(port+0x14)&~value)
                return
        super().write(cpu,access,address,size,value,user)
    def check_guards(self):
        for port,owned in ((GPIO,{11}),(GPIO+0x400,{10,12}),(GPIO+0x800,{10,11,12})):
            before=self.pin_guard[port]
            for offset,width in ((0,2),(4,1),(8,2),(12,2),(20,1)):
                original=struct.unpack_from('<I',before,offset)[0]
                mask=sum(((1<<width)-1)<<(width*pin) for pin in set(range(16))-owned)
                assert self.u32(port+offset)&mask==original&mask,(hex(port),offset)
            assert bytes(self.cpu.mem_read(port+0x20,8))==before[0x20:0x28]
    def pin(self,port,pin,mode,pull):
        assert (self.u32(port)>>(pin*2))&3==mode
        assert (self.u32(port+12)>>(pin*2))&3==pull
        assert not self.u32(port+4)&(1<<pin) # push-pull, including default input config
        assert (self.u32(port+8)>>(pin*2))&3==1 # SDK stronger-drive default
    def order(self,start,initial,expected,reset):
        writes=self.writes[start:]
        configs=[(i,a,v) for i,(a,_,v) in enumerate(writes) if a in initial]
        assert len(configs)==len(expected)*2
        for index,(port,pin,mode) in enumerate(expected):
            _,address,clear=configs[index*2]
            _,again,value=configs[index*2+1]
            assert address==again==port
            assert clear==initial[port]&~(3<<(pin*2))
            assert value==clear|(mode<<(pin*2))
            initial[port]=value
        address,value,after=reset
        reset_at=next(i for i,(a,_,v) in enumerate(writes) if a==address and v==value)
        assert configs[after*2+1][0]<reset_at<configs[(after+1)*2][0]
    def restore(self):
        start=len(self.writes);initial={p:self.u32(p) for p in self.pin_guard}
        assert self.call('m1_power_gpio_restore',1,RADIO_OUT)
        self.order(start,initial,((GPIO+0x400,12,1),(GPIO+0x400,10,0),
            (GPIO,11,0),(GPIO+0x800,10,0),(GPIO+0x800,12,0),(GPIO+0x800,11,0)),
            (GPIO+0x428,1<<12,0))
        self.pin(GPIO+0x400,12,1,0);assert not self.u32(GPIO+0x414)&(1<<12)
        for port,pin in ((GPIO+0x400,10),(GPIO,11),(GPIO+0x800,10),(GPIO+0x800,12),(GPIO+0x800,11)):
            self.pin(port,pin,0,1)
        assert not self.call('m1_power_gpio_prepared')
    def prepare(self):
        assert self.call('m1_usb_power_down',1)==0
        start=len(self.writes);initial={p:self.u32(p) for p in self.pin_guard}
        assert self.call('m1_power_gpio_prepare',1)
        self.order(start,initial,((GPIO+0x400,12,0),(GPIO+0x400,10,1),(GPIO,11,1)),
            (GPIO+0x428,1<<10,1))
        assert self.call('m1_power_gpio_prepared')
        self.pin(GPIO+0x400,12,0,0);self.pin(GPIO+0x400,10,1,0);self.pin(GPIO,11,1,0)
        assert not self.u32(GPIO+0x414)&(1<<10)


def power_gpio(elf):
    for mask in (0,1):
        d=PowerGpioArm(elf);d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
        # Cold restore is allowed without powering up an unused USB core.
        pa11=d.u32(GPIO+0x14)&(1<<11)
        d.input_script=[1<<10,1<<12];d.restore()
        assert d.cpu.mem_read(RADIO_OUT,1)[0]==3 and not d.input_script
        assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        for bits in range(8):
            d.input_script=[(bits&1)<<10,((bits>>1)&1)<<12,((bits>>2)&1)<<11]
            assert d.call('m1_power_gpio_switches',RADIO_OUT)
            assert d.cpu.mem_read(RADIO_OUT,1)[0]==bits and not d.input_script
        assert not d.call('m1_power_gpio_prepare',1) # explicit USB power-down first
        d.call('m1_battery_hal_service',1);assert d.battery_reads==1
        d.prepare();assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert d.u32(GPIO+0x14)&(1<<11)==pa11 # PA11 latch is never invented
        writes=len(d.writes)
        assert not d.call('m1_power_gpio_prepare',1)
        d.call('m1_battery_hal_service',2);assert d.battery_reads==1
        d.call('m1_battery_hal_init');d.call('m1_battery_hal_service',1000)
        assert d.call('m1_test_battery_status')==0 and len(d.writes)==writes and d.battery_reads==1
        assert not d.call('m1_startup_begin',0,1)
        assert d.call('m1_usb_hw_start',1)==2 # BUSY, before any USB/clock writes
        assert len(d.writes)==writes
        # Reconnect while asleep: restoring input roles is still required and
        # allowed before starting USB. The PC13 input latch is external stimulus.
        d.put(GPIO+0x810,d.u32(GPIO+0x810)&~(1<<13))
        d.restore();assert d.u32(GPIO+0x14)&(1<<11)==pa11
        assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        d.cpu.reg_write(UC_ARM_REG_PRIMASK,1);d.call('m1_battery_hal_init')
        d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
        d.call('m1_battery_hal_service',1001);assert d.battery_reads==2
    # Permission, malformed output, active peripherals and live USB all reject
    # before GPIO/clock writes. Failed restore retains the prepared ownership.
    d=PowerGpioArm(elf)
    assert not d.call('m1_power_gpio_restore',0,RADIO_OUT)
    assert not d.call('m1_power_gpio_restore',1,0)
    assert not d.call('m1_power_gpio_prepare',0)
    assert not d.call('m1_power_gpio_switches',0) and not d.writes
    cases=[(CRM+8,0),(TMR3,1),(TMR6,1),(ADC+8,1),(SPI+8,128),(RADIO_SPI+8,128)]
    cases.extend((DMA+offset,1) for offset in (8,0x1c,0x30,0x6c))
    cases.extend((0xe000e108,1<<irq) for irq in (10,11,12,13))
    cases.extend((USB_HS+offset,1) for offset in (8,0x14))
    cases.extend((USB_HS+base+ep*0x20,1<<31) for base in (0x900,0xb00) for ep in range(8))
    for address,value in cases:
        d=PowerGpioArm(elf);d.restore()
        assert d.call('m1_usb_power_down',1)==0
        d.put(address,value);writes=len(d.writes)
        assert not d.call('m1_power_gpio_prepare',1),(hex(address),value)
        assert len(d.writes)==writes and not d.call('m1_power_gpio_prepared')
        d=PowerGpioArm(elf);d.restore();d.prepare();d.put(address,value)
        writes=len(d.writes);d.cpu.mem_write(RADIO_OUT,b'\xcd')
        assert not d.call('m1_power_gpio_restore',1,RADIO_OUT),(hex(address),value)
        assert len(d.writes)==writes and d.cpu.mem_read(RADIO_OUT,1)[0]==0xcd
        assert d.call('m1_power_gpio_prepared')
    for register,value in ((UC_ARM_REG_BASEPRI,1),(UC_ARM_REG_FAULTMASK,1),
                           (UC_ARM_REG_CONTROL,1),(UC_ARM_REG_IPSR,3)):
        d=PowerGpioArm(elf);d.cpu.reg_write(register,value)
        assert not d.call('m1_power_gpio_restore',1,RADIO_OUT)
        assert not d.call('m1_power_gpio_prepare',1) and not d.writes
    print('PASS M1 sleep GPIO: exact owned pin roles/order, retained PA11 latch, sequential switch reads, idle/USB/context guards, battery/USB exclusion and cable-arrival restoration')


FACTORY_UPPER,FACTORY_LOWER,FACTORY_PAGE = 0x08032000,0x08032800,2048


def factory_memory(dev,distinct=False):
    """Synthetic private-page fixtures; no original device data or image."""
    dev.cpu.mem_map(FACTORY_UPPER,2*FACTORY_PAGE)
    pages=bytearray(b'\xff'*(2*FACTORY_PAGE))
    for record in m1_records():
        cell=record[2]*6+record[1]
        struct.pack_into('<H',pages,cell*2,3900-cell if distinct else 3999)
        struct.pack_into('<H',pages,FACTORY_PAGE+cell*2,1000+cell if distinct else 999)
    for offset in (0,FACTORY_PAGE):pages[offset+2045:offset+2048]=bytes((1,0x55,0xaa))
    dev.cpu.mem_write(FACTORY_UPPER,bytes(pages))
    dev.cpu.mem_protect(FACTORY_UPPER,2*FACTORY_PAGE,UC_PROT_READ)
    dev.factory_reads=[]
    def read(cpu,access,address,size,value,user):
        offset=(address-FACTORY_UPPER)%FACTORY_PAGE
        assert size==1 and (offset<252 or 2045<=offset<2048),hex(address)
        assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
        dev.factory_reads.append(address)
    dev.cpu.hook_add(UC_HOOK_MEM_READ,read,begin=FACTORY_UPPER,end=FACTORY_LOWER+FACTORY_PAGE-1)
    return bytes(pages)


def factory(elf):
    for mask in (0,1):
        d=M1Arm(elf);pages=factory_memory(d,distinct=True)
        d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
        d.cpu.mem_write(RGB,b'\xa5'*332)
        assert d.call('m1_factory_load',RGB)==0
        values=struct.unpack('<164H',d.cpu.mem_read(RGB,328))
        cells=[r[2]*6+r[1] for r in m1_records()]
        assert values==tuple([1001+c for c in cells]+[3901-c for c in cells])
        assert d.cpu.mem_read(RGB+328,4)==b'\xa5'*4 and not d.writes
        assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert len(d.factory_reads)==510 and len(set(d.factory_reads))==510
        assert bytes(d.cpu.mem_read(FACTORY_UPPER,len(pages)))==pages
        before=bytes(d.cpu.mem_read(RGB,332));d.factory_reads.clear()
        assert d.call('m1_factory_load',0)==1 and not d.factory_reads
        d.put(0x40023c0c,1)
        assert d.call('m1_factory_load',RGB)==3 and not d.factory_reads
        d.put(0x40023c0c,0)
        for address,value,expected in ((FACTORY_UPPER+2047,0,4),
                (FACTORY_LOWER+2045,0,5),(FACTORY_LOWER,0xffff,6)):
            d.cpu.mem_write(address,struct.pack('<H',value) if value>255 else bytes((value,)))
            result=d.call('m1_factory_load',RGB)
            assert result==expected,(hex(address),value,result,expected)
            assert bytes(d.cpu.mem_read(RGB,332))==before and not d.writes
            d.cpu.mem_write(FACTORY_UPPER,pages)
        assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
    for reg,value in ((UC_ARM_REG_BASEPRI,1),(UC_ARM_REG_FAULTMASK,1),
                      (UC_ARM_REG_CONTROL,1),(UC_ARM_REG_IPSR,3)):
        d=M1Arm(elf);factory_memory(d);d.cpu.reg_write(reg,value)
        assert d.call('m1_factory_load',RGB)==2 and not d.factory_reads and not d.writes
    print('PASS M1 factory calibration: read-only exact fields, physical remap/normalization, all-or-nothing import, busy/context rejection and IRQ preservation')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    args=parser.parse_args()
    lighting(args.elf)
    scanner(args.elf)
    scanner_capture(args.elf)
    startup(args.elf)
    battery_startup(args.elf)
    battery(args.elf)
    radio(args.elf)
    wireless(args.elf)
    wireless_power(args.elf)
    usb_power(args.elf)
    power_gpio(args.elf)
    sleep_hal(args.elf)
    factory(args.elf)


if __name__=='__main__':main()
