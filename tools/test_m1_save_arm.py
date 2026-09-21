"""Run actual save gate/scanner/time/battery/LED code without device access.

Transport readiness and raw ADC/time/power levels are scripted. No flash
operation runs here; the independent storage audit executes that SDK backend.
"""
import argparse
import struct
from unicorn.arm_const import (UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
    UC_ARM_REG_FAULTMASK, UC_ARM_REG_CONTROL, UC_ARM_REG_IPSR)
from test_m1_hal_arm import M1Arm, RGB, GPIO, DMA, ADC, TMR2, TMR3, TMR6, CRM, SPI
from firmware_defaults import DEFAULTS as D


class Save(M1Arm):
    def __init__(self,elf,external=True,raw=1500,qualify=True):
        super().__init__(elf,scanner=True)
        self.cpu.mem_map(0x40040000,0x20000)
        self.put(CRM+0x30,self.u32(CRM+0x30)|(1<<29))
        self.now=0;self.external=external;self.raw=raw
        assert self.call('m1_hal_init') and self.call('m1_hal_start')
        assert self.call('m1_time_start') and self.call('m1_lighting_init',0)
        self.call('m1_battery_hal_init')
        for port,pins in ((GPIO+0x400,(6,13)),(GPIO+0x800,(6,14))):
            for pin in pins:
                self.put(port,(self.u32(port)&~(3<<(pin*2)))|(1<<(pin*2)))
                self.put(port+20,self.u32(port+20)|(1<<pin))
        self.put(GPIO+0x810,0 if external else 1<<13)
        self.put(GPIO+0x410,0)
        self.call('m1_test_save_transport',6 if external else 0,0,3 if external else 28)
        for i in range(11 if qualify else 1):self.frame(i*30000)
        self.call('m1_lighting_service',self.now+1000)
        assert self.call('m1_lighting_ready')
        self.writes.clear()

    def check_guards(self):
        # Startup pin values are explicit fixture inputs here. Save operations
        # separately assert no GPIO/radio/USB writes and no unrelated DMA writes.
        pass

    def write(self,cpu,access,address,size,value,user):
        if TMR2<=address<TMR2+0x100:
            self.writes.append((address,size,value));return
        super().write(cpu,access,address,size,value,user)
        if address==CRM+0x20 and value&1:self.cpu.mem_write(TMR2,bytes(0x100))

    def frame(self,now=None):
        if now is not None:self.now=now
        self.put(TMR2+0x24,self.now)
        self.put(TMR6+0x10,1);self.call('m1_hal_timer_irq')
        for bank in range(6):
            row=[3900]*15
            if bank==5:row[4]=self.raw
            self.cpu.mem_write(self.u32(DMA+0x78),struct.pack('<15H',*row))
            self.put(DMA,3<<20);self.call('m1_hal_dma_irq')
        self.call('m1_battery_hal_service',self.now//1000)

    def unchanged_links(self):
        assert not any(GPIO<=a<GPIO+0x1000 or 0x40040000<=a<0x40060000 or
            SPI<=a<SPI+0x24 or 0x40003c00<=a<0x40003c24 or
            DMA+8<=a<DMA+0x6c or DMA+0x80<=a<DMA+0x100 for a,s,v in self.writes)


def run(elf):
    for external in (False,True):
        for mask in (0,1):
            d=Save(elf,external);d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
            assert d.call('m1_test_save_begin')==1
            assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==1
            assert not d.call('m1_hal_periodic_active') and d.call('m1_hal_healthy')
            assert not d.call('m1_hal_frame',RGB,RGB+200)
            assert not d.call('m1_hal_battery',RGB+204,RGB+208)
            assert d.u32(TMR2)&1 and not d.u32(TMR6)&1 and not d.u32(ADC+8)&1
            d.now+=42000;d.put(TMR2+0x24,d.now)
            assert d.call('m1_test_save_end') and not d.call('m1_save_fault')
            assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
            assert d.call('m1_hal_periodic_active')
            assert not d.call('m1_hal_frame',RGB,RGB+200)
            assert d.call('m1_time_now',RGB)
            assert struct.unpack('<II',d.cpu.mem_read(RGB,8))==(d.now,d.now//1000)
            d.unchanged_links()
            d.frame();assert d.call('m1_hal_frame',RGB,RGB+200)
    for external,raw,qualified in ((False,1280,True),(False,1145,True),
                                  (False,1500,False),(True,1500,False)):
        d=Save(elf,external,raw,qualified)
        assert d.call('m1_test_save_begin')==0 and not d.writes
        assert d.call('m1_hal_periodic_active') and not d.call('m1_save_fault')
    # External supply can qualify with an empty/absent battery; it does not
    # reinterpret a charger pin as proof that the battery is full.
    d=Save(elf,True,0);assert d.call('m1_test_save_begin')==1
    assert d.call('m1_test_save_end')
    d=Save(elf);d.frame(0xfffffff0)
    assert d.call('m1_test_save_begin')==1
    d.now+=42000;d.put(TMR2+0x24,d.now)
    assert d.call('m1_test_save_end') and d.call('m1_time_now',RGB)
    assert struct.unpack('<II',d.cpu.mem_read(RGB,8))==(d.now&0xffffffff,d.now//1000)
    d=Save(elf,False);d.raw=1145;d.frame(d.now+125);d.writes.clear()
    assert d.call('m1_test_save_begin')==0 and not d.writes  # fresh raw drop beats slow display
    for address,value in ((GPIO+0x810,1<<13),(GPIO+0x814,0),(GPIO+0x414,0),
            (GPIO+0x800,0),(DMA+8,1),(DMA+0x1c,1),(DMA+0x30,1),
            (0x40026408,1),(SPI+8,128),(0x40003c08,128),(0x40040008,32),
            (0xe000e010,1)):
        d=Save(elf);d.put(address,value)
        assert d.call('m1_test_save_begin')==0 and not d.writes,(hex(address),value)
        assert d.call('m1_hal_periodic_active') and not d.call('m1_save_fault')
    for selected,radio,flags in ((6,0,1),(6,0,0),(6,0,7),(0,0,12),
            (0,1,28),(0,0,20),(0,0,13),(6,0,31)):
        d=Save(elf);d.call('m1_test_save_transport',selected,radio,flags)
        result=d.call('m1_test_save_begin')
        if flags==31:assert result==1 and d.call('m1_test_save_end') # both links idle
        else:assert result==0 and not d.writes,(selected,radio,flags)
    d=Save(elf);d.put(TMR2+0x24,d.now+D['M1_BATTERY_SAMPLE_MS']*2000)
    assert d.call('m1_test_save_begin')==0 and not d.writes  # stale source qualification
    d=Save(elf);d.put(DMA,8<<20)
    assert d.call('m1_test_save_begin')==2 and d.call('m1_save_fault')
    assert not d.call('m1_hal_healthy')
    for failure in ('time','power','dma','duration','unmask','nested'):
        d=Save(elf);assert d.call('m1_test_save_begin')==1
        if failure=='time':d.put(TMR2+0x28,214)
        elif failure=='power':d.put(GPIO+0x810,1<<13)
        elif failure=='dma':d.put(DMA+8,1)
        elif failure=='duration':d.put(TMR2+0x24,d.now+D['M1_FLASH_MAX_PAUSE_US']+1)
        elif failure=='unmask':d.cpu.reg_write(UC_ARM_REG_PRIMASK,0)
        else:assert d.call('m1_test_save_begin')==2
        assert not d.call('m1_test_save_end') and d.call('m1_save_fault'),failure
        assert not d.call('m1_hal_periodic_active') and d.cpu.reg_read(UC_ARM_REG_PRIMASK)==0
        writes=len(d.writes)
        assert d.call('m1_test_save_begin')==2 and len(d.writes)==writes
    for reg,value in ((UC_ARM_REG_BASEPRI,1),(UC_ARM_REG_FAULTMASK,1),
                      (UC_ARM_REG_CONTROL,1),(UC_ARM_REG_IPSR,3)):
        d=Save(elf);d.cpu.reg_write(reg,value)
        assert d.call('m1_test_save_begin')==2 and not d.writes
        assert d.call('m1_save_fault')
    print('PASS M1 save owner: actual scanner/time/battery/LED HALs, retained links/rails, power/drain deferrals, masked pause, measured resume and terminal faults')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf')
    run(parser.parse_args().elf)
