"""Cold board-to-application handoff through actual M1 HALs and Artery SDK.

Counter/oscillator/DMA/USB register effects are scripted, not electrical proof.
Only profile storage is replaced with erased reads and a rejecting writer.
No device access, reset vector, application flash image or real flash writes.
"""
import argparse
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
                              UC_ARM_REG_CONTROL, UC_ARM_REG_IPSR, UC_ARM_REG_FAULTMASK)
from test_m1_hal_arm import (BatteryStartupArm, CRM, GPIO, DMA, TMR2, TMR6,
                             ADC, RTC, RADIO_SPI, factory_memory, FACTORY_UPPER)
from test_m1_usb_arm import Hardware, USB, DWT, DEMCR
from firmware_defaults import DEFAULTS as D

OFF, RAILS, LINKS, RADIO, APPLICATION, READY, FAILED, FATAL = range(8)


class Boot(BatteryStartupArm):
    def __init__(self, path, external=True, failure=None):
        super().__init__(path, failure)
        self.external=external
        self.attaches=self.cycles=self.elapsed_cycles=self.flags=self.pllu_polls=0
        self.trace_before=self.dwt_before=0;self.suspend_powerdown=False
        self.cpu.mem_map(DWT,0x1000)
        # BatteryStartupArm already maps USB and dispatches self.read_usb/write.
        self.cpu.hook_add(UC_HOOK_MEM_READ,Hardware.read_clock.__get__(self),begin=CRM,end=CRM+4)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_cycles,begin=DWT+4,end=DWT+7)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=DWT,end=DWT+0xfff)
        self.put(GPIO+0x810,(0 if external else 1<<13)|(1<<10))
        self.put(GPIO+0xc10,4)
        self.put(USB+0x48,0xc0);self.put(USB+0x10,1<<31)
        self.put(CRM+4,self.u32(CRM+4)|(5<<20))
        self.factory=factory_memory(self)

    def read_usb(self,*args):
        # The battery path first suspends the PHY, before USB is attached.
        if not self.external:
            BatteryStartupArm.read_usb(self,*args)
        else:Hardware.read_usb(self,*args)

    def read_cycles(self,*args):
        before=self.elapsed_cycles
        Hardware.read_cycles(self,*args)
        # Prove coordinator timestamps include time spent in SDK delay loops.
        if self.u32(TMR2)&1:
            self.put(TMR2+0x24,self.u32(TMR2+0x24)+(self.elapsed_cycles-before)//216)

    def write(self,cpu,access,address,size,value,user):
        if USB<=address<USB+0x10000:
            # Disconnected low-power PHY writes are distinct from USB attach.
            if self.u32(CRM+0x30)&(1<<29):Hardware.write_usb(self,cpu,access,address,size,value,user)
            else:self.writes.append((address,size,value))
            return
        if address in (CRM+0x10,DEMCR,DWT):
            Hardware.write(self,cpu,access,address,size,value,user);return
        if (RADIO_SPI<=address<RADIO_SPI+0x24 or GPIO+0xc00<=address<GPIO+0xc40 or
            DMA+0x1c<=address<DMA+0x44 or address in (DMA+0x108,DMA+0x10c)):
            self.writes.append((address,size,value))
        else:super().write(cpu,access,address,size,value,user)
        if address==CRM+0x20 and value&(1<<15):
            self.cpu.mem_write(RADIO_SPI,bytes(0x24));self.put(RADIO_SPI+8,2)
        for port in (GPIO,GPIO+0xc00):
            if address==port+0x18:self.put(port+0x14,self.u32(port+0x14)|value)
            if address==port+0x28:self.put(port+0x14,self.u32(port+0x14)&~value)
        if 0xe000e180<=address<0xe000e200:
            self.put(address-0x80,self.u32(address-0x80)&~value)
        if 0xe000e280<=address<0xe000e300:
            self.put(address-0x80,self.u32(address-0x80)&~value)

    def check_guards(self):
        assert bytes(self.cpu.mem_read(RTC+0x50,80))==b'\x79'*80
        assert self.u32(RTC+4)==0x00245678
        for start,end in ((0x44,0x6c),(0x80,0x100)):
            assert bytes(self.cpu.mem_read(DMA+start,end-start))==b'\x5a'*(end-start)
        if hasattr(self,'factory'):
            assert bytes(self.cpu.mem_read(FACTORY_UPPER,len(self.factory)))==self.factory

    def state(self):return self.call('m1_boot_state')

    def tick(self,us=1000):
        if self.u32(TMR2)&1:self.put(TMR2+0x24,self.u32(TMR2+0x24)+us)
        self.set_stamp(self.rtc_ticks+us//200)
        self.call('m1_boot_service',instructions=3000000)
        if self.call('m1_hal_capture_busy'):self.complete_capture()

    def until(self,state):
        for _ in range(500):
            if self.state()>=state:break
            self.tick()
        assert self.state()==state,(self.state(),self.call('m1_boot_error'),state)

    def failed(self,error):
        assert self.state()==FAILED and self.call('m1_boot_error')==error
        assert not self.call('m1_usb_hw_running') and not self.call('m1_radio_healthy')
        assert not self.call('m1_hal_healthy') and not self.call('m1_lighting_healthy')
        assert not self.u32(GPIO+0x414)&((1<<6)|(1<<13))
        assert not self.u32(GPIO+0x814)&((1<<6)|(1<<14))
        count=len(self.writes)
        self.call('m1_boot_service');assert not self.call('m1_boot_begin',6,0,1)
        assert len(self.writes)==count # no implicit reset/retry


def handoff(path):
    for external in (False,True):
        for mode in (0,1,2,5,6):
            for mask in (0,1):
                d=Boot(path,external);d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
                if mask:d.put(TMR2+0x24,0xffff0000)
                assert d.call('m1_boot_begin',mode,0,1)
                d.until(LINKS)
                assert not d.call('m1_hal_periodic_active') and not d.u32(ADC+8)&1
                before=d.u32(TMR2+0x24);d.tick()
                if external:assert (d.u32(TMR2+0x24)-before)&0xffffffff>=26000
                if mode!=6:
                    assert d.state()==RADIO
                    stamp=d.u32(TMR2+0x24)
                    d.tick(D['M1_RADIO_START_PULSE_US']-1)
                    assert d.state()==RADIO and not d.u32(GPIO+0x14)&(1<<15)
                    d.tick(1);assert d.state()==APPLICATION
                    assert (d.u32(TMR2+0x24)-stamp)&0xffffffff==D['M1_RADIO_START_PULSE_US']
                else:assert d.state()==APPLICATION
                d.tick();assert d.state()==READY
                assert d.call('m1_live_transport')==mode and d.call('m1_live_factory_result')==0
                assert d.call('m1_test_boot_storage_io')==2 # two slot reads, no writes
                assert d.call('m1_hal_periodic_active') and d.u32(TMR6+0x24)==0
                assert d.call('m1_usb_hw_running')==external and d.attaches==external
                assert not d.call('m1_usb_ready') and not d.call('m1_wireless_ready')
                assert d.call('m1_wireless_healthy')==(mode!=6)
                assert d.wakes==int(not external)
                assert not d.call('m1_live_storage_fault') and not d.call('m1_live_transport_fault')
                assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
                count=len(d.writes)
                d.call('m1_boot_service');assert not d.call('m1_boot_begin',mode,0,1)
                assert len(d.writes)==count # handed off, not a second service owner
    print('PASS M1 cold handoff: both power sources, all transports, real HAL/app/profile binding and fresh post-USB timing')


def faults(path):
    for reg,value in ((UC_ARM_REG_BASEPRI,32),(UC_ARM_REG_CONTROL,1),
                      (UC_ARM_REG_IPSR,16),(UC_ARM_REG_FAULTMASK,1)):
        d=Boot(path);d.cpu.reg_write(reg,value);count=len(d.writes)
        assert not d.call('m1_boot_begin',6,0,1)
        d.call('m1_boot_service');assert d.state()==OFF and len(d.writes)==count
    d=Boot(path);count=len(d.writes)
    assert not d.call('m1_boot_begin',6,0,0)
    assert not d.call('m1_boot_begin',3,0,1)
    assert not d.call('m1_boot_begin',6,0x2000e000,1) # missing callback pair
    assert d.state()==OFF and len(d.writes)==count
    d=Boot(path);d.call('m1_time_stop')
    assert not d.call('m1_boot_begin',6,0,1) and d.call('m1_boot_error')==1
    # A DMA error latched at the exact startup/pause boundary is fatal, not a
    # reason to attach USB while acquisition still owns a partial frame.
    d=Boot(path)
    address=d.symbols['m1_hal_pause']&~1
    d.cpu.hook_add(UC_HOOK_CODE,lambda *args:d.put(DMA,8<<20),begin=address,end=address)
    assert d.call('m1_boot_begin',6,0,1)
    d.until(FAILED);d.failed(4);assert not d.attaches
    for stage in (RAILS,LINKS,RADIO,APPLICATION):
        d=Boot(path);assert d.call('m1_boot_begin',0,0,1);d.until(stage)
        d.put(GPIO+0x810,1<<13);d.tick();d.failed(3)
    for failure in ('counter','pllu','unplug'):
        d=Boot(path,failure=failure);assert d.call('m1_boot_begin',6,0,1)
        d.until(LINKS);d.tick();d.failed(5)
    d=Boot(path);assert d.call('m1_boot_begin',6,0,1);d.until(APPLICATION)
    d.cpu.mem_write(FACTORY_UPPER+2047,b'\0');d.factory=bytes(d.cpu.mem_read(FACTORY_UPPER,len(d.factory)))
    d.tick();d.failed(9) # real application refuses malformed factory bounds
    d=Boot(path);assert d.call('m1_boot_begin',6,0,1);d.until(APPLICATION)
    d.put(CRM+8,d.u32(CRM+8)|(8<<4));d.tick();d.failed(1)
    d=Boot(path);assert d.call('m1_boot_begin',6,0,1);d.until(APPLICATION)
    d.call('m1_hal_stop');d.tick();d.failed(8)
    d=Boot(path,False);d.resume_failure='pll_switch'
    assert d.call('m1_boot_begin',0,0,1)
    for _ in range(100):
        d.tick()
        if d.state()==FATAL:break
    assert d.state()==FATAL and d.call('m1_boot_error')==2
    assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==1 and not d.u32(TMR2)&1
    count=len(d.writes);d.call('m1_boot_service');assert len(d.writes)==count
    print('PASS M1 cold handoff failures: context/source/USB/time/calibration/resume, fatal wake and no automatic retries')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf')
    args=parser.parse_args();handoff(args.elf);faults(args.elf)


if __name__=='__main__':main()
