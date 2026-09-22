"""Execute the installed M1 power controller; HAL completions/time are scripted.

Real policy, selector table, wake filter, state machine and SDK GPIO writes.
Not physical sleep, DMA, radio delivery or oscillator validation.
"""
import argparse
import struct
from pathlib import Path
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP
from test_m1_image_arm import Image, Reset, RAM_END, FLASH
from firmware_defaults import DEFAULTS as D

RETURN=FLASH+0x3fff0
PB,PC=0x40020400,0x40020800


class Runtime(Reset):
    def __init__(self,image,mode=0,critical=False,peer=3):
        super().__init__(image);self.reset()
        self.ms=self.us=0;self.mode=mode;self.critical=critical;self.peer=peer
        self.activity=False;self.eligible=True;self.park_allowed=True
        self.periodic=True;self.scan_healthy=True;self.led_healthy=True;self.led_ticks=0
        self.radio_healthy=True;self.radio_ready=True;self.prepared=False;self.usb_reduced=False
        self.request=self.sent=self.peer_ticks=self.captures=self.sequence=self.scan_inits=0
        self.sleeps=0;self.wake_after=D['M1_WAKE_ACQUIRE_FRAMES']+1
        self.capture_ticks=0;self.switches=7;self.sleep_result=0;self.sleep_gap=50000
        self.parked=False;self.resumed=False;self.confirmed=True;self.link_ticks=0
        self.trace=[];self.failure=None;self.calls={}
        self.put(PB+0x14,(1<<6)|(1<<13));self.put(PC+0x14,(1<<6)|(1<<14))
        names='''m1_live_service m1_live_power_activity m1_live_transport
            m1_wireless_status m1_battery_hal_status m1_battery_critical
            m1_sleep_time_ready m1_live_power_suspend m1_live_power_park
            m1_hal_healthy m1_lighting_healthy m1_wireless_healthy m1_hal_pause
            m1_lighting_offer m1_lighting_service m1_wireless_service
            m1_lighting_ready m1_lighting_stop m1_wireless_request_sleep
            m1_wireless_sleep_sent m1_radio_quiesce m1_wireless_stop
            m1_hal_stop m1_usb_hw_running m1_usb_power_down m1_usb_power_ready
            m1_power_gpio_prepare m1_power_gpio_switches m1_radio_ready
            m1_hal_periodic_active m1_hal_capture_busy m1_sleep_timed_wait
            m1_hal_init m1_hal_capture_start m1_hal_service m1_hal_frame
            m1_power_gpio_restore m1_lighting_init m1_wireless_resume_retained
            m1_radio_init m1_radio_service m1_radio_healthy m1_wireless_init m1_wireless_selected
            m1_battery_hal_init m1_hal_start m1_live_power_resume'''.split()
        self.by_address={self.s[n]&~1:n for n in names}
        self.cpu.hook_add(UC_HOOK_CODE,self.intercept)

    def write(self,cpu,access,address,size,value,user):
        super().write(cpu,access,address,size,value,user)
        if address in (PB+0x18,PC+0x18):self.put(address-4,self.u32(address-4)|value)
        if address in (PB+0x28,PC+0x28):self.put(address-20,self.u32(address-20)&~value)

    def call(self,name,*args):
        for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):self.cpu.reg_write(r,v&0xffffffff)
        self.cpu.reg_write(UC_ARM_REG_SP,self.s['__m1_stack_top__'])
        self.cpu.reg_write(UC_ARM_REG_LR,RETURN|1)
        self.cpu.emu_start(self.s[name],RETURN,count=200000)
        assert self.cpu.reg_read(UC_ARM_REG_PC)==RETURN,name
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def state(self):return self.call('m1_runtime_power_state')
    def tick(self,step=1000,external=False):
        self.us=(self.us+step)&0xffffffff;self.ms=(self.ms+step//1000)&0xffffffff
        self.call('m1_runtime_power_service',self.ms,self.us,external)

    def intercept(self,cpu,address,size,user):
        name=self.by_address.get(address)
        if not name:return
        a=tuple(cpu.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))
        self.trace.append((name,a,self.ms));self.calls[name]=self.calls.get(name,0)+1
        result=1
        if name=='m1_live_service':assert not self.parked
        elif name=='m1_live_power_activity':cpu.mem_write(a[0],bytes((self.activity,)));result=self.eligible
        elif name=='m1_live_transport':result=self.mode
        elif name=='m1_wireless_status':cpu.mem_write(a[0],bytes((0,self.peer,self.mode)))
        elif name=='m1_battery_hal_status':result=RAM_END-256
        elif name=='m1_battery_critical':result=self.critical
        elif name=='m1_live_power_park':self.parked=self.park_allowed;result=self.parked
        elif name=='m1_hal_healthy':result=self.scan_healthy
        elif name=='m1_lighting_healthy':result=self.led_healthy
        elif name=='m1_wireless_healthy':result=self.radio_healthy
        elif name=='m1_hal_pause':assert self.parked and self.periodic;self.periodic=False
        elif name=='m1_lighting_offer':
            assert not self.periodic and a[1]==246 and bytes(cpu.mem_read(a[0],a[1]))==bytes(246)
            self.led_ticks=2
        elif name=='m1_lighting_service':self.led_ticks=max(0,self.led_ticks-1)
        elif name=='m1_lighting_ready':result=self.led_ticks==0
        elif name=='m1_lighting_stop':self.led_healthy=False
        elif name=='m1_wireless_request_sleep':
            assert self.parked and not self.periodic and a[1]==1 and a[0] in (3,5)
            if self.request:assert self.sent==5 and a[0]==3
            self.request=a[0];self.sent=0;self.peer_ticks=2
        elif name=='m1_wireless_service':
            if self.peer_ticks:
                self.peer_ticks-=1
                if not self.peer_ticks:self.sent=self.request
            if self.link_ticks:
                self.link_ticks-=1
                if not self.link_ticks:self.confirmed=True
        elif name=='m1_wireless_sleep_sent':result=self.sent
        elif name=='m1_radio_quiesce':assert self.sent==3 and a[0]==1;self.radio_ready=False
        elif name=='m1_wireless_stop':self.radio_healthy=False
        elif name=='m1_hal_stop':self.scan_healthy=False;self.periodic=False
        elif name=='m1_usb_hw_running':result=0
        elif name=='m1_usb_power_down':
            assert not self.periodic and not self.led_healthy and a[0]==1
            self.usb_reduced=True;result=0
        elif name=='m1_usb_power_ready':result=self.usb_reduced
        elif name=='m1_power_gpio_prepare':assert self.usb_reduced;self.prepared=True
        elif name=='m1_power_gpio_switches':cpu.mem_write(a[0],bytes((self.switches,)))
        elif name=='m1_radio_ready':result=self.radio_ready
        elif name=='m1_hal_periodic_active':result=self.periodic
        elif name=='m1_hal_capture_busy':result=self.capture_ticks!=0
        elif name=='m1_sleep_timed_wait':
            assert a[:2]==(D['M1_RUNTIME_SLEEP_TICKS'],1)
            assert self.prepared and not self.periodic and not self.capture_ticks and not self.led_healthy
            assert not self.u32(PB+0x14)&((1<<6)|(1<<13)) and not self.u32(PC+0x14)&((1<<6)|(1<<14))
            assert self.sent in (3,5)
            self.sleeps+=1;self.ms=(self.ms+self.sleep_gap//1000)&0xffffffff;self.us=(self.us+self.sleep_gap)&0xffffffff
            result=self.sleep_result
        elif name=='m1_hal_init':self.scan_healthy=True;self.sequence=0;self.scan_inits+=1
        elif name=='m1_hal_capture_start':
            assert self.scan_healthy and not self.periodic and not self.capture_ticks
            self.capture_ticks=2
        elif name=='m1_hal_service':self.capture_ticks=max(0,self.capture_ticks-1)
        elif name=='m1_hal_frame':
            assert not self.capture_ticks and self.scan_healthy
            self.captures+=1;self.sequence+=1;frame=[3900]*82
            if self.captures>=self.wake_after:frame[81]=3500
            cpu.mem_write(a[0],struct.pack('<82H',*frame));cpu.mem_write(a[1],struct.pack('<I',self.sequence))
        elif name=='m1_power_gpio_restore':assert self.prepared;self.prepared=False;cpu.mem_write(a[1],b'\x03')
        elif name=='m1_lighting_init':self.led_healthy=True;self.led_ticks=2
        elif name=='m1_wireless_resume_retained':
            assert self.sent==5 and not self.prepared and a[0]==1
            self.confirmed=False;self.link_ticks=3;self.request=self.sent=0
        elif name=='m1_radio_init':assert self.sent==3 and not self.prepared;self.radio_ready=False
        elif name=='m1_radio_service':self.radio_ready=True
        elif name=='m1_wireless_init':
            assert self.radio_ready and a[:2]==(self.mode,1)
            self.radio_healthy=True;self.confirmed=False;self.link_ticks=3;self.request=self.sent=0
        elif name=='m1_wireless_selected':result=self.confirmed
        elif name=='m1_hal_start':assert not self.prepared and self.confirmed;self.periodic=True
        elif name=='m1_live_power_resume':
            assert self.periodic and self.led_healthy and not self.prepared and a[1]==1
            self.parked=False;self.resumed=True
        if self.failure and self.failure[0]==name and self.calls[name]==self.failure[1]:result=self.failure[2]
        cpu.reg_write(UC_ARM_REG_R0,int(result));cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))

    def shorten_idle(self):
        self.tick()
        # Only the policy's test idle limits change, not runtime state/timing.
        # The native policy suite separately tests full counter boundaries.
        at=self.s['policy']+4
        assert struct.unpack('<HH',self.cpu.mem_read(at,4))==(D['M1_RUNTIME_BT_IDLE_STEPS'],D['M1_RUNTIME_RADIO_IDLE_STEPS'])
        self.cpu.mem_write(at,struct.pack('<HH',1,1))

    def until(self,predicate,limit=1200):
        for _ in range(limit):
            self.tick(step=10000 if self.state()==0 else 1000)
            if predicate():return
        raise AssertionError(('no expected transition',self.state(),self.trace[-8:]))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('elf',type=Path)
    image=Image(parser.parse_args().elf.read_bytes())
    for mode,critical,peer in ((0,False,3),(1,False,3),(2,False,3),(5,False,3),(0,True,1),(5,True,1)):
        d=Runtime(image,mode,critical,peer);d.shorten_idle();d.until(lambda:d.resumed)
        assert d.state()==0 and not d.call('m1_runtime_power_error')
        assert d.captures==D['M1_WAKE_ACQUIRE_FRAMES']+1 and d.scan_inits==2
        labels=[x[0] for x in d.trace]
        assert labels.index('m1_live_power_park')<labels.index('m1_hal_pause')<labels.index('m1_lighting_offer')
        assert labels.index('m1_lighting_stop')<labels.index('m1_power_gpio_prepare')<labels.index('m1_sleep_timed_wait')
        assert labels.index('m1_power_gpio_restore')<labels.index('m1_hal_start')<labels.index('m1_live_power_resume')
        assert ('m1_wireless_resume_retained' in labels)==(not critical and mode!=5)
    d=Runtime(image);d.shorten_idle();d.wake_after=10000;d.sleep_gap=1000000
    d.until(lambda:d.sent==3 and d.calls.get('m1_radio_quiesce',0)>0)
    assert [args[0] for name,args,_ in d.trace if name=='m1_wireless_request_sleep']==[5,3]
    d.switches=6;d.until(lambda:d.resumed)
    assert not d.calls.get('m1_wireless_resume_retained')
    for mode,external,activity in ((6,False,False),(0,True,False),(0,False,True)):
        d=Runtime(image,mode,critical=external);d.activity=activity;d.shorten_idle()
        for _ in range(600):d.tick(10000,external)
        assert d.state()==0 and not d.calls.get('m1_live_power_suspend')
    # Held input never bypasses critical protection.
    d=Runtime(image,critical=True);d.activity=True;d.shorten_idle();d.until(lambda:d.resumed)
    # Long output drain still owns live service; scanner pauses only after park.
    d=Runtime(image);d.park_allowed=False;d.shorten_idle();d.until(lambda:d.state()==1)
    for _ in range(200):d.tick()
    assert d.periodic and not d.calls.get('m1_hal_pause')
    d.park_allowed=True;d.until(lambda:d.resumed)
    # A stalled park has a bounded, latched failure, even across clock wrap.
    d=Runtime(image);d.ms=0xfffff800;d.us=0xfffff000
    d.park_allowed=False;d.shorten_idle();d.until(lambda:d.state()==1)
    d.tick((D['M1_RUNTIME_HANDOFF_MS']-1)*1000)
    assert d.state()==1 and d.periodic
    d.tick(1000)
    assert d.state()==16 and d.call('m1_runtime_power_error')==2
    before=len(d.trace);d.tick();assert len(d.trace)==before
    # Independent millisecond/microsecond wrap must not shorten settling.
    for ms,us in ((0xffffff00,0),(0,0xffffff00)):
        d=Runtime(image);d.ms=ms;d.us=us;d.shorten_idle();d.until(lambda:d.resumed)
        assert d.state()==0 and not d.call('m1_runtime_power_error')
    # A newly powered source cannot continue the battery pin sequence.
    d=Runtime(image);d.shorten_idle();d.until(lambda:d.state()==4)
    before=len(d.trace);d.tick(external=True)
    assert d.state()==16 and len(d.trace)==before
    for name,result in (('m1_hal_pause',0),('m1_lighting_offer',0),('m1_usb_power_down',5),
                        ('m1_power_gpio_prepare',0),('m1_hal_init',0),('m1_power_gpio_restore',0),
                        ('m1_hal_start',0),('m1_live_power_resume',0)):
        d=Runtime(image);d.failure=(name,1,result);d.shorten_idle();d.until(lambda:d.state()>=16)
        assert d.state()==16 and d.call('m1_runtime_power_error')
        before=len(d.trace);d.tick();assert len(d.trace)==before
    for result,expected in ((7,17),(8,18),(5,16)):
        d=Runtime(image);d.sleep_result=result;d.shorten_idle();d.until(lambda:d.state()>=16)
        assert d.state()==expected
    print('PASS installed runtime power controller: idle/critical, all wireless modes, ordered ownership/rails, RTC wake filtering, retention escalation, restoration and terminal failures (HAL completions scripted)')


if __name__=='__main__':main()
