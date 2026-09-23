"""Cold board-to-application handoff through actual M1 HALs and Artery SDK.

Counter/oscillator/DMA/USB register effects are scripted, not electrical proof.
Only profile storage is replaced with erased reads and a rejecting writer.
No device access, reset vector, application flash image or real flash writes.
"""
import argparse
import struct
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
                              UC_ARM_REG_CONTROL, UC_ARM_REG_IPSR, UC_ARM_REG_FAULTMASK,
                              UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_LR)
from test_m1_hal_arm import (wireless_supported, BatteryStartupArm, CRM, GPIO, DMA, TMR2, TMR6,
                             ADC, RTC, RADIO_SPI, factory_memory, FACTORY_UPPER)
from test_m1_usb_arm import Hardware, USB, DWT, DEMCR
from firmware_defaults import DEFAULTS as D

OFF, RAILS, LINKS, RADIO, APPLICATION, READY, FAILED, FATAL = range(8)


class Boot(BatteryStartupArm):
    def __init__(self, path, external=True, failure=None):
        super().__init__(path, failure)
        self.external=external;self.periodic_delivery=True
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
        elif self.periodic_delivery and self.call('m1_hal_periodic_active'):
            self.put(TMR6+0x10,1);self.call('m1_hal_timer_irq');self.complete_capture()

    def until(self,state):
        for _ in range(500):
            if self.state()>=state:break
            self.tick()
        assert self.state()==state,(self.state(),self.call('m1_boot_error'),state)

    def wireless(self):
        return bool(self.call('m1_wireless_supported'))

    def failed(self,error):
        assert self.state()==FAILED and self.call('m1_boot_error')==error
        assert self.call('m1_usb_hw_running')==bool(self.attaches and error not in (1,3))
        # A failed boot must leave no radio running; a USB-only artifact has no
        # radio to leave in any state.
        assert not self.call('m1_radio_healthy') if self.wireless() else self.call('m1_radio_healthy')
        assert not self.call('m1_hal_healthy') and not self.call('m1_lighting_healthy')
        assert not self.u32(GPIO+0x414)&((1<<6)|(1<<13))
        assert not self.u32(GPIO+0x814)&((1<<6)|(1<<14))
        count=len(self.writes)
        self.call('m1_boot_service');assert not self.call('m1_boot_begin',6,0,1)
        assert len(self.writes)==count # no implicit reset/retry


def handoff(path):
    wireless=wireless_supported(Boot(path,False))
    modes=(0,1,2,5,6) if wireless else (6,)
    if not wireless:
        print('SKIP boot radio handshake: this build has no Bluetooth/2.4 GHz; '
              'USB-only startup still verified')
    for external in (False,True):
        for mode in modes:
            for mask in (0,1):
                d=Boot(path,external);d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
                if mask:d.put(TMR2+0x24,0xffff0000)
                assert d.call('m1_boot_begin',mode,0,1)
                if external:
                    before=d.u32(TMR2+0x24);d.tick()
                    assert (d.u32(TMR2+0x24)-before)&0xffffffff>=26000
                    assert d.attaches==1 and not d.call('m1_hal_healthy')
                d.until(LINKS)
                assert not d.call('m1_hal_periodic_active') and not d.u32(ADC+8)&1
                before=d.u32(TMR2+0x24);d.tick()
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
                if wireless:
                    assert d.call('m1_wireless_healthy')==(mode!=6)
                else:
                    # A USB-only artifact has no wireless subsystem to fault.
                    assert d.call('m1_wireless_healthy')
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
    # reason to restart peripherals. USB was attached before acquisition.
    d=Boot(path)
    address=d.symbols['m1_hal_pause']&~1
    d.cpu.hook_add(UC_HOOK_CODE,lambda *args:d.put(DMA,8<<20),begin=address,end=address)
    assert d.call('m1_boot_begin',6,0,1)
    d.until(FAILED);d.failed(4);assert d.attaches==1
    stages=((RAILS,LINKS,RADIO,APPLICATION) if wireless_supported(Boot(path))
            else (RAILS,LINKS,APPLICATION))
    for stage in stages:
        d=Boot(path)
        if not wireless_supported(d):assert not d.call('m1_boot_begin',0,0,1)
        # RADIO only exists for a wireless transport in a build that has one.
        assert d.call('m1_boot_begin',0 if stage==RADIO else 6,0,1);d.until(stage)
        d.put(GPIO+0x810,1<<13);d.tick();d.failed(3)
    for failure in ('counter','pllu','unplug'):
        d=Boot(path,failure=failure);assert d.call('m1_boot_begin',6,0,1)
        d.until(FAILED);d.failed(5)
    d=Boot(path);assert d.call('m1_boot_begin',6,0,1);d.until(APPLICATION)
    d.cpu.mem_write(FACTORY_UPPER+2047,b'\0');d.factory=bytes(d.cpu.mem_read(FACTORY_UPPER,len(d.factory)))
    d.tick();assert d.state()==READY and d.call('m1_live_factory_result')==4
    # A real released frame supports explicitly provisional RAM-only bounds.
    assert d.call('m1_test_boot_storage_io')==2
    assert bytes(d.cpu.mem_read(FACTORY_UPPER,len(d.factory)))==d.factory
    assert d.call('m1_boot_scan',0x2000c000,0x2000c200)
    assert struct.unpack('<82H',d.cpu.mem_read(0x2000c000,164))==(3001,)*82
    assert not d.call('m1_boot_scan',0,0x2000c200)
    d=Boot(path);d.periodic_delivery=False
    assert d.call('m1_boot_begin',6,0,1);d.until(FAILED);d.failed(2)
    assert not d.call('m1_boot_scan',0x2000c000,0x2000c200)
    d=Boot(path);assert d.call('m1_boot_begin',6,0,1);d.until(APPLICATION)
    d.put(CRM+8,d.u32(CRM+8)|(8<<4));d.tick();d.failed(1)
    d=Boot(path);assert d.call('m1_boot_begin',6,0,1);d.until(APPLICATION)
    d.call('m1_hal_stop');d.tick();d.failed(8)
    d=Boot(path,False);d.resume_failure='pll_switch'
    # A build without the wireless stack has no battery radio transport.
    assert d.call('m1_boot_begin',0 if wireless_supported(d) else 6,0,1)
    for _ in range(100):
        d.tick()
        if d.state()==FATAL:break
    assert d.state()==FATAL and d.call('m1_boot_error')==2
    assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==1 and not d.u32(TMR2)&1
    count=len(d.writes);d.call('m1_boot_service');assert len(d.writes)==count
    print('PASS M1 cold handoff failures: context/source/USB/time/calibration/resume, fatal wake and no automatic retries')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf')
    args=parser.parse_args();handoff(args.elf);faults(args.elf);diagnostics(args.elf)


def diagnostics(path):
    """Real diagnostic/SysEx code; abstract port readiness and USB transfers."""
    import midi_sysex as sx
    from test_m1_hal_arm import M1Arm,RGB
    d=M1Arm(path);wire=bytearray();messages=[];cleanup=[];neutral=[];consumer=[]
    pages=factory_memory(d,distinct=True)
    d.cpu.mem_map(0x08004000,0x1000)
    responses={'m1_usb_hw_running':1,'m1_usb_ready':1,'m1_usb_generation':1,
               'm1_usb_midi_take':0,'m1_boot_state':FAILED,'m1_boot_error':9,
               'm1_live_factory_result':3,'m1_usb_midi_send':1,'m1_boot_scan':1,'m1_usb_hid_send':1,
               'm1_usb_consumer_send':1}
    names={d.symbols[name]&~1:name for name in responses}
    def port(cpu,address,size,user):
        name=names.get(address)
        if not name:return
        if name=='m1_boot_scan':
            cpu.mem_write(cpu.reg_read(UC_ARM_REG_R0),struct.pack('<82H',*range(2000,2082)))
            cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1),struct.pack('<I',1234))
        if name=='m1_usb_midi_send':
            assert cpu.reg_read(UC_ARM_REG_PRIMASK)==1
            events=bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R0),cpu.reg_read(UC_ARM_REG_R1)))
            for at in range(0,len(events),4):
                if events[at]>>4==0:
                    cleanup.append(events[at:at+4]);continue
                cin=events[at]&15;assert events[at]>>4==1 and 4<=cin<=7
                count=3 if cin==4 else cin-4;wire.extend(events[at+1:at+1+count])
                if cin!=4:messages.append(sx.decode(wire));wire.clear()
        if name=='m1_usb_hid_send':neutral.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R0),30)))
        if name=='m1_usb_consumer_send':consumer.append(cpu.reg_read(UC_ARM_REG_R0))
        cpu.reg_write(UC_ARM_REG_R0,responses[name]);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
    d.cpu.hook_add(UC_HOOK_CODE,port)
    def service():
        result=0
        for _ in range(24):result=d.call('m1_diagnostics_service',10)
        return result
    def send(kind,seq=0,payload=b''):
        events=sx.usb_events(sx.encode(kind,123,seq,payload))
        d.cpu.mem_write(RGB,events);d.call('midi_control_receive_usb',RGB,len(events))
        return service()
    assert not service() and not messages  # no unsolicited traffic without HELLO
    assert not send(sx.HELLO)
    assert messages[0][0]==sx.READY and b'MG-M1V5TMR' in messages[0][3]
    assert messages[-1][0]==sx.LOG and messages[-1][3]==b'Boot failed: application factory=0x00000003 dma=0x00000000'
    assert not send(sx.COMMAND,1,b'cfg set 7 1 2500 2800') and messages[-1][0]==sx.ERROR
    assert not send(sx.COMMAND,2,b'bootloader') and messages[-1][0]==sx.ERROR
    assert not send(sx.COMMAND,3,b'boot status') and messages[-1][0]==sx.LOG
    assert not send(sx.COMMAND,4,b'factory read') and messages[-1][0]==sx.ACK
    dump=messages[-2]
    assert dump[0]==sx.DUMP and dump[3]==(b'M1FC\x01\x00\x7e\x00'+
        pages[:252]+pages[2045:2048]+pages[2048:2300]+pages[4093:4096])
    assert bytes(d.cpu.mem_read(FACTORY_UPPER,len(pages)))==pages and not d.writes
    d.put(0x40023c0c,1)
    assert not send(sx.COMMAND,5,b'factory read') and messages[-1][0]==sx.ERROR
    d.put(0x40023c0c,0)
    assert not send(sx.COMMAND,6,b'factory read 0x08000000') and messages[-1][0]==sx.ERROR
    assert not send(sx.COMMAND,7,b'boot scan') and messages[-1][0]==sx.ACK
    assert messages[-2][0]==sx.DUMP and messages[-2][3]==(
        b'M1BS\x01\x00\x52\x00'+struct.pack('<I82H',1234,*range(2000,2082)))
    responses['m1_boot_scan']=0
    assert not send(sx.COMMAND,8,b'boot scan') and messages[-1][0]==sx.ERROR
    d.put(d.symbols['m1_test_recovery_result'],0)
    assert send(sx.COMMAND,9,b'bootloader') and messages[-1][0]==sx.ACK
    responses['m1_usb_generation']=2
    assert not service() and not d.call('midi_control_ready')
    responses['m1_boot_state']=READY
    count=len(messages);assert not service() and len(messages)==count
    d.call('m1_diagnostics_runtime_fault',5)
    service();service();assert neutral==[bytes(30)] and consumer==[0] and len(cleanup)==48
    assert cleanup==[bytes((0x0b,0xb0+ch,cc,0)) for ch in range(16) for cc in (64,120,123)]
    send(sx.HELLO)
    assert messages[-1][0]==sx.LOG and messages[-1][3]==b'Runtime failed: detail=0x00000005 store=0x00000000 scan=0x00000000'
    assert not send(sx.COMMAND,1,b'cfg set 7 1 2500 2800') and messages[-1][0]==sx.ERROR
    assert send(sx.COMMAND,2,b'bootloader') and messages[-1][0]==sx.ACK
    print('PASS M1 cold-start control: build handshake, failure text, mutation rejection, guarded reset request, USB epoch and live-owner handoff')
    print('PASS M1 terminal runtime recovery: neutral HID, MIDI cleanup, fault log and guarded software IAP without peripheral restart')


if __name__=='__main__':main()
