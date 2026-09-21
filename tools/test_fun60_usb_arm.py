"""Execute the FUN60 USB class and shared control runtime as Cortex-M4 code.

Only the vendor transport/clock boundary is stubbed. This is not a USB PHY,
FIFO or interrupt timing test and never opens a physical device.
"""
from pathlib import Path
import struct
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR,
                              UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_M4)
import midi_sysex as sysex

RETURN=0x2001f000
SCRATCH=0x20010000


class Usb:
    def __init__(self,path):
        self.cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for base,size in ((0x08000000,0x40000),(0x20000000,0x20000),
                          (0xe0000000,0x100000),(0x1ffff000,0x1000)):
            self.cpu.mem_map(base,size)
        with Path(path).open('rb') as stream:
            elf=ELFFile(stream)
            for segment in elf.iter_segments():
                if segment['p_type']=='PT_LOAD':
                    self.cpu.mem_write(segment['p_vaddr'],segment.data())
            self.symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        self.stubs={}
        self.opens=[];self.sends=[];self.receive=None;self.reply=None;self.stalled=False
        self.rx_length=0
        names=('crm_periph_reset','crm_periph_clock_enable','crm_pllu_output_set',
               'crm_flag_get','crm_usb_clock_source_select','usbd_init',
               'usbd_ept_open','usbd_ept_close','usbd_ept_recv','usbd_ept_send',
               'usbd_ctrl_send','usbd_ctrl_unsupport','usbd_get_recv_len')
        for name in names:
            address=self.symbols[name]&~1;self.stubs[address]=name
            self.cpu.hook_add(UC_HOOK_CODE,self.stub,begin=address,end=address)
        assert self.call('at32_usb_init')==1
        self.call('probe_bind',self.core,self.handlers,self.descriptors)
        self.dev=self.core+4  # official otg_core_type: register pointer then dev

    def stub(self,cpu,address,size,_):
        name=self.stubs[address]
        a,b,c,d=(cpu.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))
        result=0
        if name=='crm_flag_get':result=1
        elif name=='usbd_init':
            assert (b,c)==(0,1)
            self.core=a;self.handlers=d
            self.descriptors=self.u32(cpu.reg_read(UC_ARM_REG_SP))
        elif name=='usbd_ept_open':self.opens.append((b,c,d))
        elif name=='usbd_ept_recv':self.receive=(b,c,d)
        elif name=='usbd_ept_send':self.sends.append((b,c,bytes(cpu.mem_read(c,d))))
        elif name=='usbd_ctrl_send':self.reply=bytes(cpu.mem_read(b,c))
        elif name=='usbd_ctrl_unsupport':self.stalled=True
        elif name=='usbd_get_recv_len':result=self.rx_length
        cpu.reg_write(UC_ARM_REG_R0,result)
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))

    def u32(self,address):return struct.unpack('<I',self.cpu.mem_read(address,4))[0]
    def call(self,name,*args):
        for reg,value in ((UC_ARM_REG_SP,0x2001e000),(UC_ARM_REG_LR,RETURN|1)):
            self.cpu.reg_write(reg,value)
        for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):
            self.cpu.reg_write(reg,value)
        address=self.symbols[name] if isinstance(name,str) else name
        self.cpu.emu_start(address|1,RETURN,count=1000000)
        assert self.cpu.reg_read(UC_ARM_REG_PC)==RETURN,(name,hex(self.cpu.reg_read(UC_ARM_REG_PC)))
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def callback(self,index,*args):return self.call(self.u32(self.handlers+4*index),self.dev,*args)
    def descriptor(self,index):
        pointer=self.call(self.u32(self.descriptors+4*index))
        size=struct.unpack('<H',self.cpu.mem_read(pointer,2))[0]
        return bytes(self.cpu.mem_read(self.u32(pointer+4),size))
    def setup(self,request_type,request,value,index,length,dispatch=None):
        self.reply=None;self.stalled=False
        self.cpu.mem_write(SCRATCH,struct.pack('<BBHHH',request_type,request,value,index,length))
        result=self.callback(2,SCRATCH) if dispatch is None else self.call('probe_request',self.dev,SCRATCH,dispatch)
        return result,self.reply,self.stalled
    def configured(self,high=True):
        self.call('probe_speed',self.dev,high);self.opens=[]
        self.callback(0);self.call('midi_control_service')
        assert self.opens==[(0x81,3,16),(0x82,2,512 if high else 64),(2,2,512 if high else 64)]


def descriptors(usb):
    assert usb.descriptor(0)[8:12]==bytes.fromhex('51312d50')
    for index,packet in ((2,64),(10,512),(3,64)):
        data=usb.descriptor(index)
        assert len(data)==struct.unpack_from('<H',data,2)[0]==166
        assert data[1]==(7 if index==3 else 2) and data[4]==3
        at=0;interfaces=[];endpoints=[]
        while at<len(data):
            size,kind=data[at:at+2];assert size>=2 and at+size<=len(data)
            if kind==4:interfaces.append(data[at+2])
            if kind==5:endpoints.append((data[at+2],struct.unpack_from('<H',data,at+4)[0]))
            at+=size
        assert interfaces==[0,1,2] and endpoints==[(0x81,16),(2,packet),(0x82,packet)]
    # Other-speed query must not poison the normal configuration's type.
    assert usb.descriptor(2)[1]==2
    for index in (5,6,7,8,9):
        data=usb.descriptor(index);assert data[0]==len(data) and data[1]==3
        assert data[2:].decode('utf-16le')


def main(path):
    usb=Usb(path);descriptors(usb)
    for high in (False,True):
        usb.configured(high)
        assert usb.setup(0x80,6,0x0600,0,10,dispatch=0)[1][1]==6
        other=usb.setup(0x80,6,0x0700,0,255,dispatch=0)[1]
        assert other[1]==7 and struct.unpack_from('<H',other,140)[0]==(64 if high else 512)
        assert usb.setup(0x82,0,0,0x1ff,2,dispatch=1)[2]
        assert usb.setup(0x82,0,0,0x83,2,dispatch=1)[2]
        assert not usb.setup(0x02,1,0,0x82,0)[2]
        result,report,stalled=usb.setup(0x81,6,0x2200,0,255)
        assert result==0 and not stalled and report[-1]==0xc0
        assert usb.setup(0x81,6,0x2100,0,9)[1][1]==0x21
        assert usb.setup(0x81,10,0,2,1)[1]==b'\0'
        assert usb.setup(0x01,11,1,2,0)[2]  # invalid alternate
        assert usb.setup(0x81,6,0x2200,2,255)[2]  # no HID on MIDI
        assert usb.setup(0x21,11,0,0,0)[2]  # boot protocol not advertised
        assert usb.setup(0xa1,1,0x0100,0,16)[1]==bytes(16)
        usb.cpu.mem_write(SCRATCH,bytes(range(16)))
        assert usb.call('at32_usb_keyboard',SCRATCH)==1
        endpoint,buffer,original=usb.sends[-1];assert endpoint==0x81
        usb.cpu.mem_write(SCRATCH,bytes(16));assert usb.call('at32_usb_keyboard',SCRATCH)==0
        assert bytes(usb.cpu.mem_read(buffer,16))==original
        usb.callback(5,1);assert usb.call('at32_usb_keyboard',SCRATCH)==1
        usb.callback(5,1)
        assert usb.setup(0x21,10,2<<8,0,0)[0]==0
        assert usb.setup(0xa1,2,0,0,1)[1]==b'\2'
        usb.cpu.mem_write(usb.symbols['probe_milliseconds'],struct.pack('<I',8))
        previous=len(usb.sends);usb.call('at32_usb_service');assert len(usb.sends)==previous+1
        usb.callback(5,1)
        usb.cpu.mem_write(usb.symbols['probe_milliseconds'],bytes(4))
        usb.sends=[]
        events=sysex.usb_events(sysex.encode(sysex.HELLO,1234))
        ep,rx,capacity=usb.receive;assert ep==2 and capacity==(512 if high else 64)
        for offset in range(0,len(events),capacity):
            packet=events[offset:offset+capacity];usb.cpu.mem_write(rx,packet)
            usb.rx_length=len(packet);usb.callback(6,2)
        # Main-context only; EP OUT must never directly dispatch/send a reply.
        assert not usb.sends
        wire=bytearray()
        for _ in range(100):
            usb.call('midi_control_service')
            while usb.sends:
                ep,pointer,packet=usb.sends.pop(0);assert ep==0x82
                assert not usb.call('at32_usb_midi',9,0x90,60,100)
                assert bytes(usb.cpu.mem_read(pointer,len(packet)))==packet
                for at in range(0,len(packet),4):
                    cin=packet[at]&15;assert packet[at]>>4==1
                    wire.extend(packet[at+1:at+1+(3 if cin==4 else cin-4)])
                usb.callback(5,2)
        kind,session,sequence,payload=sysex.decode(wire)
        assert (kind,session,sequence)==(sysex.READY,1234,0)
        assert b'monsgeek_fun60_pro_wired' in payload
        assert usb.call('at32_usb_midi',9,0x90,60,100)==1
        assert usb.sends[-1][2]==b'\x09\x90\x3c\x64'
        usb.callback(1)  # unconfigure also cancels stream/session ownership
        usb.call('midi_control_service');assert usb.call('midi_control_ready')==0
        usb.call('probe_connected',usb.dev,0)
        assert not usb.call('at32_usb_keyboard',SCRATCH)
        usb.call('probe_connected',usb.dev,1)
    print('PASS: FUN60 ARM descriptors, FS/HS endpoints, HID requests/idle, immutable TX, SysEx handshake, reset')


if __name__=='__main__':main(sys.argv[1])
