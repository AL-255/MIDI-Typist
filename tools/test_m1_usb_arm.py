"""Offline M1 composite class through linked, unmodified Artery SDK routines.

The harness supplies endpoint completion/register effects, not PHY timing,
bus enumeration or host delivery. It never discovers or opens USB hardware.
"""
import argparse
import struct
from unicorn import UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
from unicorn.arm_const import (UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
                              UC_ARM_REG_FAULTMASK, UC_ARM_REG_CONTROL, UC_ARM_REG_IPSR)
from test_m1_hal_arm import M1Arm, RAM, CRM, GPIO, DMA, ADC, TMR3, TMR6, SPI

USB=0x40040000
INPUT,OUTPUT=RAM+0xc000,RAM+0xc400
DWT,DEMCR=0xe0001000,0xe000edfc

class Hardware(M1Arm):
    """Clock/counter/reset effects only; actual SDK init and IRQ code executes."""
    def __init__(self,path,failure=None):
        super().__init__(path)
        self.failure=failure;self.attaches=0;self.cycles=0;self.elapsed_cycles=0
        self.flags=0;self.trace_before=0;self.dwt_before=0;self.pllu_polls=0
        self.suspend_powerdown=False
        self.cpu.mem_map(USB,0x10000);self.cpu.mem_map(DWT,0x1000)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write_usb,begin=USB,end=USB+0xffff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_usb,begin=USB,end=USB+0xffff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_clock,begin=CRM,end=CRM+4)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.read_cycles,begin=DWT+4,end=DWT+7)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=DWT,end=DWT+0xfff)
        self.put(CRM,0x03030003)
        self.put(CRM+4,self.u32(CRM+4)|(5<<20)) # reference PLLU /18
        self.put(CRM+0x30,4) # PC13 GPIO clock, no OTG clock yet
        self.put(GPIO+0x800,self.u32(GPIO+0x800)&~(3<<26)) # PC13 input
        self.put(GPIO+0x810,self.u32(GPIO+0x810)&~(1<<13)) # external power
        self.gpio_guard=bytes(self.cpu.mem_read(GPIO,0xc00))
        self.dma_guard=bytes(self.cpu.mem_read(DMA,0x200))
        self.put(USB+0x48,0xc0) # HS PHY type
        self.put(USB+0x10,1<<31) # AHB idle
    def read_clock(self,cpu,access,address,size,value,user):
        if address==CRM:
            if self.u32(CRM+4)&(1<<29):self.pllu_polls+=1
            stable=self.pllu_polls>=7 and self.failure!='pllu'
            self.put(CRM,(self.u32(CRM)&~(1<<26))|(int(stable)<<26))
    def read_cycles(self,cpu,access,address,size,value,user):
        stuck=self.failure=='counter' or (self.failure=='counter_late' and self.elapsed_cycles>=3*216000)
        if not stuck and self.u32(DWT)&1 and self.u32(DEMCR)&(1<<24):
            self.cycles=(self.cycles+21600)&0xffffffff;self.elapsed_cycles+=21600
        self.put(DWT+4,self.cycles)
        if self.failure=='unplug' and self.elapsed_cycles>=3*216000:
            self.put(GPIO+0x810,self.u32(GPIO+0x810)|(1<<13))
            self.gpio_guard=bytes(self.cpu.mem_read(GPIO,0xc00))
    def read_usb(self,cpu,access,address,size,value,user):
        if address==USB+0x10:
            stuck={'reset':1,'txflush':32,'rxflush':16}.get(self.failure,0)
            val=self.u32(address)&~(0x31&~stuck)
            val=(val&~(1<<31)) if self.failure=='ahb' else (val|(1<<31))
            self.put(address,val)
        elif address==USB+0x14:
            # GINTSTS flags are W1C; current mode is read-only.
            self.put(address,self.flags|int(self.failure=='mode'))
        elif address==USB+0x808 and self.suspend_powerdown:
            assert not self.u32(CRM+0x10)&(1<<29), 'cannot suspend a core held in reset'
            self.put(address,self.u32(address)|1)
    def write_usb(self,cpu,access,address,size,value,user):
        self.writes.append((address,size,value))
        if address==USB+0x14:self.flags&=~value
        if address==USB+0x804 and not value&2 and self.u32(address)&2:
            self.attaches+=1
            assert self.elapsed_cycles>=26*216000 # actual modeled cycle waits
            assert self.u32(DEMCR)==self.trace_before and self.u32(DWT)==self.dwt_before
            assert not self.failure
    def write(self,cpu,access,address,size,value,user):
        allowed=(address in (CRM+4,CRM+0x10,CRM+0x30,CRM+0x78,CRM+0xa4,DEMCR,DWT) or
                 0xe000e100<=address<0xe000e200 or 0xe000e280<=address<0xe000e300 or
                 0xe000e400<=address<0xe000e500)
        assert allowed,f'USB wrote unrelated register {address:08x}'
        self.writes.append((address,size,value))
        if address==CRM+0x10 and value&(1<<29):
            self.cpu.mem_write(USB,bytes(0x10000))
            self.flags=0
            self.put(USB+0x48,0xc0);self.put(USB+0x10,1<<31)
        # Model NVIC set/clear enable and pending writes.
        if 0xe000e180<=address<0xe000e200:
            self.put(address-0x80,self.u32(address-0x80)&~value)
        if 0xe000e280<=address<0xe000e300:
            self.put(address-0x80,self.u32(address-0x80)&~value)
    def check_guards(self):
        assert bytes(self.cpu.mem_read(GPIO,0xc00))==self.gpio_guard
        assert bytes(self.cpu.mem_read(DMA,0x200))==self.dma_guard

def hardware(path):
    for mask in (0,1):
        d=Hardware(path);d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
        assert d.call('m1_usb_hw_start',1)==0
        assert d.call('m1_usb_hw_running') and not d.call('m1_usb_ready')
        assert d.attaches==1 and d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert d.u32(CRM+0x30)&(1<<29) and d.u32(CRM+4)&(1<<29)
        assert d.u32(USB+0x18)==0x803c381c and d.u32(USB+8)&1
        assert d.u32(USB+0x24)==256
        assert d.u32(USB+0x28)==(64<<16)|256
        assert d.u32(USB+0x104)==(32<<16)|320
        assert d.u32(USB+0x108)==(256<<16)|352
        assert d.u32(0xe000e108)&(1<<13) # IRQ77 only
        writes=len(d.writes)
        assert d.call('m1_usb_hw_start',1)==2 and len(d.writes)==writes
        assert not d.call('m1_power_gpio_restore',1,OUTPUT)
        assert not d.call('m1_power_gpio_prepare',1) and len(d.writes)==writes
        epoch=d.call('m1_usb_generation')
        d.flags=1<<12;d.call('m1_usb_hw_irq') # actual SDK reset dispatch
        assert d.call('m1_usb_generation')==epoch+1 and not d.call('m1_usb_ready')
        assert d.flags==0
        assert d.call('m1_usb_hw_stop')==0 and not d.call('m1_usb_hw_running')
        assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
        assert d.u32(CRM+0x10)&(1<<29) and not d.u32(CRM+0x30)&(1<<29)
        assert not d.u32(CRM+4)&(1<<29) and not d.u32(0xe000e108)&(15<<10)
        writes=len(d.writes);d.call('m1_usb_hw_irq')
        assert d.call('m1_usb_hw_stop')==0 and len(d.writes)==writes
        assert d.call('m1_usb_hw_start',1)==0 and d.attaches==2
    for failure,result in (('pllu',5),('counter',6),('counter_late',6),('reset',7),('ahb',7),
                           ('txflush',7),('rxflush',7),('mode',7),('unplug',4)):
        d=Hardware(path,failure)
        assert d.call('m1_usb_hw_start',1,instructions=10000000)==result,failure
        assert not d.call('m1_usb_hw_running') and not d.call('m1_usb_ready')
        assert not d.attaches and not d.u32(CRM+0x30)&(1<<29)
        assert not d.u32(DEMCR)&(1<<24) and not d.u32(DWT)&1
    d=Hardware(path);d.cycles=0xfffff000
    d.trace_before=0x1000400;d.dwt_before=0x11
    d.put(DEMCR,d.trace_before);d.put(DWT,d.dwt_before)
    d.put(CRM+4,d.u32(CRM+4)|(1<<29)) # PLLU already shared by caller
    assert d.call('m1_usb_hw_start',1)==0 and d.attaches==1
    assert d.call('m1_usb_hw_stop')==0 and d.u32(CRM+4)&(1<<29)
    assert d.u32(DEMCR)==d.trace_before and d.u32(DWT)==d.dwt_before
    for reg,value in ((UC_ARM_REG_BASEPRI,0x20),(UC_ARM_REG_FAULTMASK,1),
                      (UC_ARM_REG_CONTROL,1),(UC_ARM_REG_IPSR,16)):
        d=Hardware(path);d.cpu.reg_write(reg,value)
        assert d.call('m1_usb_hw_start',1)==1 and not d.writes
        assert d.call('m1_usb_hw_stop')==1 and not d.writes
    d=Hardware(path)
    assert d.call('m1_usb_hw_start',0)==2 and not d.writes
    d.put(CRM+4,0) # invalid PLL denominator must not enter SDK divide
    assert d.call('m1_usb_hw_start',1)==3 and not d.writes
    for address,bits in ((DMA+8,1),(DMA+0x1c,1),(DMA+0x30,1),(DMA+0x6c,1),
                         (ADC+8,1),(TMR3,1),(TMR6,1),(SPI+8,128),(SPI+0x400+8,128),
                         (0xe000e010,1),(0xe000e108,1<<13)):
        d=Hardware(path);d.put(address,d.u32(address)|bits)
        d.dma_guard=bytes(d.cpu.mem_read(DMA,0x200))
        assert d.call('m1_usb_hw_start',1)==2 and not d.writes
    d=Hardware(path);d.put(GPIO+0x810,d.u32(GPIO+0x810)|(1<<13))
    d.gpio_guard=bytes(d.cpu.mem_read(GPIO,0xc00))
    assert d.call('m1_usb_hw_start',1)==4 and not d.writes
    d=Hardware(path)
    assert d.call('m1_usb_hw_start',1)==0 and d.call('m1_usb_hw_stop')==0
    d.put(GPIO+0x810,d.u32(GPIO+0x810)|(1<<13))
    d.gpio_guard=bytes(d.cpu.mem_read(GPIO,0xc00));d.suspend_powerdown=True
    assert d.call('m1_usb_power_down',1)==0 and d.call('m1_usb_power_ready')
    d.put(GPIO+0x810,d.u32(GPIO+0x810)&~(1<<13))
    d.gpio_guard=bytes(d.cpu.mem_read(GPIO,0xc00));d.suspend_powerdown=False
    assert d.call('m1_usb_hw_start',1)==0 and d.call('m1_usb_hw_running')
    assert not d.call('m1_usb_power_ready')
    print('PASS M1 USB HAL: actual SDK clock/init/FIFO/reset IRQ, deferred attach, owned shutdown/restart and bounded failures')
class Device(M1Arm):
    def __init__(self,path,high_speed):
        super().__init__(path)
        self.disable_completes=True
        self.stuck_flush=0
        self.cpu.mem_map(USB,0x10000)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.usb_write,begin=USB,end=USB+0xffff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,self.usb_read,begin=USB,end=USB+0xffff)
        self.call('m1_test_usb_init',high_speed)
        self.maxpacket=512 if high_speed else 64
        assert self.call('m1_usb_ready') and self.call('m1_usb_drained')
        assert self.u32(USB+0x920)&0x7ff==30
        assert self.u32(USB+0x940)&0x7ff==self.maxpacket
        assert self.u32(USB+0xb40)&0x7ff==self.maxpacket
    def usb_write(self,cpu,access,address,size,value,user):
        self.writes.append((address,size,value))
    def usb_read(self,cpu,access,address,size,value,user):
        if address==USB+0x10:
            self.put(address,self.u32(address)&~(0x31&~self.stuck_flush)) # flush/reset done
        if self.disable_completes and address in (USB+0x900,USB+0x920,USB+0x940,USB+0x960) and self.u32(address)&(1<<30):
            self.put(address,self.u32(address)&~(3<<30)) # endpoint disable done
    def get(self,field):return self.call('m1_test_usb_get',field)
    def complete(self,endpoint):
        address=USB+0x900+32*endpoint
        self.put(address,self.u32(address)&~(1<<31))
        self.call('m1_test_usb_in',endpoint)
    def setup(self,kind,request,value=0,index=0,length=0,stall=False,instructions=500000):
        # A fresh SETUP clears the hardware EP0 halt/transfer state.
        for offset in (0x900,0xb00): self.put(USB+offset,self.u32(USB+offset)&~((3<<30)|(1<<21)))
        self.call('m1_test_usb_setup',kind,request,value,index|(length<<16),instructions=instructions)
        halted=bool(self.u32(USB+0x900)&(1<<21))
        assert halted==stall,(kind,request,value,index,length,halted)
        if not halted and kind&0x80 and length:
            return bytes(self.cpu.mem_read(self.get(4),self.get(6)))
        return b''

def descriptors(path):
    for high in (False,True):
        d=Device(path,high)
        device=d.setup(0x80,6,0x100,length=18)
        assert len(device)==18 and device[7]==64 and device[14:17]==bytes((1,2,0))
        assert d.setup(0x80,6,0x600,length=10)==bytes((10,6,0,2,0xef,2,1,64,1,0))
        for other in (False,True):
            data=d.setup(0x80,6,0x700 if other else 0x200,length=65535)
            assert len(data)==191 and data[:5]==bytes((9,7 if other else 2,191,0,4))
            records=[];i=0
            while i<len(data):
                n=data[i];assert n>=2 and i+n<=len(data)
                records.append(data[i:i+n]);i+=n
            assert [r[2] for r in records if r[1]==4]==[0,1,2,3]
            endpoints=[r for r in records if r[1]==5]
            assert [r[2] for r in endpoints]==[0x81,2,0x82,0x83]
            packet=512 if high!=other else 64
            assert [struct.unpack_from('<H',r,4)[0] for r in endpoints]==[30,packet,packet,2]
            assert [r[3:] for r in records if r[1]==0x25]==[bytes((2,1,5)),bytes((2,3,7))]
        for index in (0,1,2,4,5):
            string=d.setup(0x80,6,0x300+index,length=255)
            assert string[0]==len(string) and string[1]==3
        from midi_backend import is_control_port
        product=d.setup(0x80,6,0x302,length=255)[2:].decode('utf-16le')
        control=d.setup(0x80,6,0x305,length=255)[2:].decode('utf-16le')
        performance=d.setup(0x80,6,0x304,length=255)[2:].decode('utf-16le')
        assert len(product+' '+control)<=30
        assert is_control_port(f'{product}:{product} {control} 24:1')
        assert not is_control_port(f'{product}:{product} {performance} 24:0')
        report=d.setup(0x81,6,0x2200,length=65535)
        assert b'\x19\x04\x29\xdf' in report and report[-1]==0xc0
        hid=d.setup(0x81,6,0x2100,length=9)
        assert len(hid)==9 and hid[7]==len(report)
        consumer=d.setup(0x81,6,0x2200,index=3,length=65535)
        assert consumer[:6]==bytes((5,12,9,1,0xa1,1)) and consumer[-7:]==bytes((0x75,16,0x95,1,0x81,0,0xc0))
        assert d.setup(0x81,6,0x2100,index=3,length=9)[7]==len(consumer)
        for interface in range(4):
            assert d.setup(0x81,10,index=interface,length=1)==b'\0'
            d.setup(1,11,index=interface)
            d.setup(1,11,1,index=interface,stall=True)
        for kind,req,val,idx,n in ((0x81,6,0x2200,1,64),(0x81,10,0,4,1),
            (0x21,9,0x200,0,64),(0x21,9,0x201,0,1),(0x21,9,0x100,0,1),
            (0x21,11,0,0,0),(0xa1,3,0,0,1),(0x21,10,1,0,0),(0,9,2,0,0),
            (0,3,2,0,0),(0x40,0x7f,0,0,0),(0x80,6,0x303,0,32),(2,3,0,0,0)):
            d.setup(kind,req,val,idx,n,stall=True)
        # Every invalid low-byte endpoint, plus high-byte contamination, must
        # stall before the vendor code can index an out-of-range record.
        for endpoint in range(256):
            valid=endpoint in (0,0x80,0x81,0x82,0x83,2)
            d.setup(0x82,0,index=endpoint,length=2,stall=not valid)
            d.setup(0x82,0,index=endpoint+256,length=2,stall=True)
    print('PASS M1 FS/HS descriptors, dual-speed requests, two MIDI cables, bounded controls and endpoint-index rejection')

def consumer(path):
    for high in (False,True):
        d=Device(path,high)
        assert not d.call('m1_usb_consumer_send',1024)
        assert d.call('m1_usb_consumer_send',0xe9)
        assert not d.call('m1_usb_consumer_send',0) and not d.call('m1_usb_drained')
        assert d.cpu.mem_read(d.get(9),2)==b'\xe9\0'
        assert d.setup(0xa1,1,0x100,3,65535)==b'\xe9\0'
        d.complete(1);assert not d.call('m1_usb_drained')
        d.complete(3);assert d.call('m1_usb_drained')
        assert d.call('m1_usb_consumer_send',0xe9) and d.call('m1_usb_drained')
        d.setup(0x21,10,0x200,3);assert d.setup(0xa1,2,index=3,length=1)==b'\2'
        assert d.setup(0xa1,2,index=0,length=1)==b'\0' # independent keyboard idle
        unit=4*(8 if high else 1)
        d.call('m1_test_usb_sof',unit+1);d.setup(0x21,10,0x300,3)
        d.call('m1_test_usb_sof',unit-1);assert not d.call('m1_usb_drained')
        d.complete(3)
        assert d.setup(0xa1,2,index=3,length=1)==b'\3'
        d.setup(0x21,10,0,3)
        for kind,request,value,length in ((0x21,9,0x200,1),(0xa1,1,0x101,2),(0x21,11,0,0)):
            d.setup(kind,request,value,3,length,stall=True)
        assert d.call('m1_usb_consumer_send',0)
        d.setup(2,3,index=0x83);assert not d.call('m1_usb_consumer_send',0xe2)
        d.setup(2,1,index=0x83);assert d.call('m1_usb_consumer_send',0)
        assert d.cpu.mem_read(d.get(9),2)==bytes(2);d.complete(3)
        for mask in (0,1):
            d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
            assert d.call('m1_usb_consumer_send',0xea if mask else 0xe2)
            assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask;d.complete(3)
        d.cpu.reg_write(UC_ARM_REG_PRIMASK,0)
        d.setup(0,9,0);assert not d.call('m1_usb_consumer_send',0)
        d.setup(0,9,1);assert d.setup(0xa1,1,0x100,3,2)==bytes(2)
    print('PASS consumer HID: private buffer, backpressure, GET_REPORT, independent idle, endpoint halt, IRQ masks and reset neutral')


def transfers(path):
    for high in (False,True):
        d=Device(path,high)
        report=bytes((2,0,1))+bytes(27)
        d.cpu.mem_write(INPUT,report)
        for mask in (0,1):
            report=bytes((mask+2,0,1))+bytes(27);d.cpu.mem_write(INPUT,report)
            d.cpu.reg_write(UC_ARM_REG_PRIMASK,mask)
            assert d.call('m1_usb_hid_send',INPUT)
            assert d.cpu.reg_read(UC_ARM_REG_PRIMASK)==mask
            address=d.get(0);assert address!=INPUT and d.get(1)==30
            assert bytes(d.cpu.mem_read(address,30))==report
            assert not d.call('m1_usb_hid_send',INPUT) and not d.call('m1_usb_drained')
            assert d.setup(0xa1,1,0x100,length=65535)==report
            d.complete(2);assert not d.call('m1_usb_drained') # wrong endpoint cannot free HID
            d.complete(1);assert d.call('m1_usb_drained')
            assert d.call('m1_usb_hid_send',INPUT) and d.call('m1_usb_drained') # unchanged: idle scheduler owns duplicates
        for n in (0,1,3,d.maxpacket+4): assert not d.call('m1_usb_midi_send',INPUT,n)
        events=bytes((i*17)&255 for i in range(d.maxpacket));d.cpu.mem_write(INPUT,events)
        assert d.call('m1_usb_midi_send',INPUT,len(events))
        address=d.get(2);assert address!=INPUT
        d.cpu.mem_write(INPUT,bytes(len(events)))
        assert bytes(d.cpu.mem_read(address,len(events)))==events
        assert not d.call('m1_usb_midi_send',INPUT,4)
        epoch=d.call('m1_usb_generation');d.call('m1_test_usb_event',2) # suspend
        assert not d.call('m1_usb_ready') and d.call('m1_usb_generation')==epoch+1
        d.call('m1_test_usb_event',3) # wake: outstanding transfer still owned
        assert d.call('m1_usb_ready') and not d.call('m1_usb_midi_send',INPUT,4)
        d.complete(2);assert d.call('m1_usb_drained')
        for endpoint,send in ((0x81,'m1_usb_hid_send'),(0x82,'m1_usb_midi_send')):
            args=(INPUT,) if endpoint==0x81 else (INPUT,4)
            assert d.call(send,*args)
            epoch=d.call('m1_usb_generation')
            d.setup(2,3,index=endpoint)
            assert d.call('m1_usb_generation')>epoch and d.call('m1_usb_ready')
            assert not d.call(send,*args)
            start=len(d.writes)
            d.setup(2,1,index=endpoint)
            # HALT already disabled the endpoint. CLEAR must still flush its
            # old FIFO before reusing the private transfer buffer.
            assert any(a==USB+0x10 and v&0x20 and (v>>6)&31==endpoint&127
                       for a,_,v in d.writes[start:])
            assert d.call(send,*args)
            d.complete(endpoint&127)
        rx=d.get(3);d.cpu.mem_write(rx,events)
        d.call('m1_test_usb_out',2,len(events))
        start=len(d.writes)
        assert not d.call('m1_usb_midi_take',OUTPUT,len(events)-1)
        assert len(d.writes)==start # no rearm, no overwrite on undersized destination
        assert d.call('m1_usb_midi_take',OUTPUT,len(events))==len(events)
        assert bytes(d.cpu.mem_read(OUTPUT,len(events)))==events
        assert d.get(7)==d.maxpacket and not d.call('m1_usb_midi_take',OUTPUT,d.maxpacket)
        d.setup(0x21,9,0x200,length=1)
        d.cpu.mem_write(d.get(5),b'\xff');d.call('m1_test_usb_out',0,1)
        assert d.call('m1_usb_leds')==31
        assert d.setup(0xa1,1,0x200,length=1)==b'\x1f'
        # New SETUP cancels the previous pending LED report.
        d.setup(0x21,9,0x200,length=1)
        d.setup(0x80,6,0x100,length=18)
        d.call('m1_test_usb_out',0,1);assert d.call('m1_usb_leds')==31
        d.setup(0x21,10,0x100)
        assert d.setup(0xa1,2,length=1)==b'\x01'
        ticks=32 if high else 4
        d.call('m1_test_usb_sof',ticks-1);assert d.call('m1_usb_drained')
        d.call('m1_test_usb_sof',1);assert not d.call('m1_usb_drained')
        d.complete(1)
        # SET_IDLE does not restart elapsed time; near-expiry edits wait for
        # the scheduled report, then govern the next period.
        d.setup(0x21,10,0xa00)
        d.call('m1_test_usb_sof',ticks*9+1)
        d.setup(0x21,10,0x1400)
        assert d.setup(0xa1,2,length=1)==b'\x14'
        d.call('m1_test_usb_sof',ticks-1);assert not d.call('m1_usb_drained')
        d.complete(1)
        d.call('m1_test_usb_sof',ticks*19);assert d.call('m1_usb_drained')
        d.call('m1_test_usb_sof',ticks);assert not d.call('m1_usb_drained')
        d.complete(1)
        d.call('m1_test_usb_sof',ticks*10)
        d.setup(0x21,10,0x500);d.call('m1_test_usb_sof',1)
        assert not d.call('m1_usb_drained') # elapsed exceeds newly requested period
        d.complete(1)
        d.call('m1_test_usb_out',2,3) # malformed event packet: latch until reset
        assert d.call('m1_usb_errors')==1 and not d.call('m1_usb_ready')
        d.call('m1_test_usb_event',1);assert not d.call('m1_usb_ready')
        d.call('m1_test_usb_init',high);assert d.call('m1_usb_ready')
        d.setup(0,9,0);assert not d.call('m1_usb_ready')
        d.setup(0,9,1);assert d.call('m1_usb_ready')
        d.cpu.mem_write(INPUT,report);assert d.call('m1_usb_hid_send',INPUT)
        d.disable_completes=False
        d.setup(2,3,index=0x81)
        assert d.call('m1_usb_errors')==1 and not d.call('m1_usb_ready')
        # Bounded SDK polling can return while hardware is still busy. Neither
        # a TX nor RX flush timeout may leave the class ready for buffer reuse.
        for request,endpoint in ((1,0x81),(1,0x82),(9,0)):
            d=Device(path,high)
            d.stuck_flush=0x10 if request==9 else 0x20
            d.setup(0 if request==9 else 2,request,index=endpoint,instructions=10000000)
            assert d.call('m1_usb_errors')==1 and not d.call('m1_usb_ready')
            assert not d.call('m1_usb_hid_send',INPUT)
            assert not d.call('m1_usb_midi_send',INPUT,4)
    print('PASS M1 linked SDK transfers: private buffers, backpressure, LED output, idle rate, IRQ masks, suspend/reset/deconfigure and malformed OUT')

def control(path):
    import midi_sysex as sx
    from keyboard_gui_model import decode, parse_build
    from keyboard_boards import get_board, M1_TARGET
    for high in (False,True):
        d=Device(path,high);d.call('m1_test_usb_control_init')
        def receive():
            wire=bytearray()
            for _ in range(100):
                d.call('m1_test_usb_service')
                if not d.call('m1_usb_drained'):
                    count=d.u32(USB+0x950)&0x7ffff
                    events=bytes(d.cpu.mem_read(d.get(2),count))
                    for at in range(0,len(events),4):
                        cin=events[at]&15;assert events[at]>>4==1 and 4<=cin<=7
                        n=3 if cin==4 else cin-4
                        wire.extend(events[at+1:at+1+n])
                    d.complete(2)
                    if wire and wire[-1]==0xf7:return sx.decode(wire)
            raise AssertionError('No complete M1 USB SysEx reply')
        def send(kind,seq=0,text=b''):
            wire=sx.encode(kind,123,seq,text);events=bytearray()
            for at in range(0,len(wire),3):
                part=wire[at:at+3];last=at+3>=len(wire)
                events.extend(bytes((0x10|(4+len(part) if last else 4),))+part+bytes(3-len(part)))
            for at in range(0,len(events),d.maxpacket):
                part=events[at:at+d.maxpacket]
                d.cpu.mem_write(d.get(3),bytes(part));d.call('m1_test_usb_out',2,len(part))
                d.call('m1_test_usb_service')
            return receive()
        kind,session,seq,payload=send(sx.HELLO)
        assert (kind,session,seq)==(sx.READY,123,0)
        assert parse_build(payload.decode()+'\n')[2]==M1_TARGET
        assert send(sx.COMMAND,1,b'cfg key 10 81 135')[:3]==(sx.ACK,123,1)
        assert send(sx.COMMAND,2,b'cfg set 11 81 2500 2800')[:3]==(sx.ACK,123,2)
        assert d.call('m1_test_usb_publish')
        kind,session,seq,payload=receive();assert kind==sx.SNAPSHOT
        s=decode(payload);assert get_board(M1_TARGET).validates_wire(s)
        assert s.count==82 and s.keyboard_mapping[81]==135 and s.press[81]==2500 and s.release[81]==2800
        assert (s.ack,s.result)==(11,1)
        assert send(sx.COMMAND,3,b'cfg key 12 77 4')[:3]==(sx.ACK,123,3)
        assert d.call('m1_test_usb_publish')
        s=decode(receive()[3]);assert s.result==2 and s.keyboard_mapping[77]==0
        d.call('m1_test_usb_event',2);d.call('m1_test_usb_service')
        d.call('m1_test_usb_event',3);d.call('m1_test_usb_service')
        assert not d.call('m1_test_usb_publish') # no stale GUI session after resume
        assert send(sx.HELLO)[0]==sx.READY
    print('PASS M1 USB-to-GUI: real SDK endpoints, shared SysEx, 82-key decoder, key/threshold ACK-readback, immutable Fn and session reset')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf');args=p.parse_args()
    descriptors(args.elf);consumer(args.elf);transfers(args.elf);control(args.elf);hardware(args.elf)
if __name__=='__main__':main()
