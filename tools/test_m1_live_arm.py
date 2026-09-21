"""M1 foreground integration: scripted acquisitions, actual app/SDK USB/GUI.

Does not open hardware or model physical cadence, DMA movement or LED timing.
"""
import argparse
import struct
from test_m1_usb_arm import Device, INPUT, OUTPUT, USB
import midi_sysex as sx
from keyboard_gui_model import decode
from keyboard_capture import KeyDecoder, StreamError
from firmware_defaults import DEFAULTS as D


class Live(Device):
    def __init__(self,path,high,sequence=0,time=0):
        super().__init__(path,high)
        self.samples=[3900]*82;self.sequence=sequence;self.time=time;self.commands=0
        self.messages=[];self.wire=bytearray();self.events=[];self.hid=bytes(30)
        self.cpu.mem_write(INPUT,struct.pack('<82H',*([1000]*82)))
        self.cpu.mem_write(OUTPUT,struct.pack('<82H',*([4096]*82)))
        assert not self.call('m1_live_init',0,OUTPUT)
        assert self.call('m1_live_init',INPUT,OUTPUT)
        assert not self.call('m1_live_init',INPUT,OUTPUT) # no live reinitialization
        self.tick()
    def collect(self):
        if self.u32(USB+0x920)&(1<<31):
            self.hid=bytes(self.cpu.mem_read(self.get(0),30));self.complete(1)
        if self.u32(USB+0x940)&(1<<31):
            count=self.u32(USB+0x950)&0x7ffff
            data=bytes(self.cpu.mem_read(self.get(2),count));self.complete(2)
            for at in range(0,count,4):
                event=data[at:at+4]
                if event[0]>>4==0:self.events.append(event);continue
                assert event[0]>>4==1
                cin=event[0]&15;assert 4<=cin<=7
                for byte in event[1:1+(3 if cin==4 else cin-4)]:
                    if byte==0xf0:self.wire.clear()
                    self.wire.append(byte)
                    if byte==0xf7:
                        self.messages.append(sx.decode(self.wire));self.wire.clear()
    def tick(self,frame=True,step=125):
        self.time+=step
        if frame:
            self.sequence=(self.sequence+1)&0xffffffff
            self.cpu.mem_write(INPUT,struct.pack('<82H',*self.samples))
            self.call('m1_test_live_frame',INPUT,self.sequence)
        self.call('m1_live_service',(self.time//1000)&0xffffffff,self.time&0xffffffff)
        self.collect()
    def wait(self,kind,sequence=None):
        for _ in range(1200):
            for i,message in enumerate(self.messages):
                if message[0]==kind and (sequence is None or message[2]==sequence):
                    return self.messages.pop(i)
            self.tick()
        raise AssertionError(f'No reply kind={kind} seq={sequence}')
    def send(self,kind,sequence=0,text=b''):
        wire=sx.encode(kind,123,sequence,text);events=bytearray()
        for at in range(0,len(wire),3):
            part=wire[at:at+3];last=at+3>=len(wire)
            events.extend(bytes((0x10|(4+len(part) if last else 4),))+part+bytes(3-len(part)))
        for at in range(0,len(events),self.maxpacket):
            part=events[at:at+self.maxpacket]
            self.cpu.mem_write(self.get(3),bytes(part));self.call('m1_test_usb_out',2,len(part));self.tick()
    def command(self,text,error=False):
        self.commands+=1;self.send(sx.COMMAND,self.commands,text.encode())
        return self.wait(sx.ERROR if error else sx.ACK,self.commands)
    def snapshot(self,ack=None):
        for _ in range(8):
            s=decode(self.wait(sx.SNAPSHOT)[3])
            if ack is None or s.ack==ack:return s
        raise AssertionError('No matching configuration readback')
    def held(self,usage):
        return bool(self.hid[2+(usage-4)//8]&(1<<((usage-4)%8)))
    def chord(self,sensor):
        self.samples[77]=self.samples[sensor]=3000;self.tick()
        self.samples[77]=self.samples[sensor]=3900;self.tick();self.tick()
        for _ in range(180):self.tick()


def integration(path):
    for high in (False,True):
        d=Live(path,high)
        d.send(sx.HELLO);assert b'MG-M1V5TMR' in d.wait(sx.READY)[3]
        d.command('stream gui');s=d.snapshot()
        assert s.count==82 and s.sample_hz==8000 and s.raw==(3900,)*82
        assert not s.calibration_flags&4 and not s.storage_flags and s.storage_slot==255
        d.command('cfg calibrate 1');assert d.snapshot(1).result==2
        d.command('cfg clean 2');assert d.snapshot(2).result==2
        for sensor in (61,33): # physical C/R: unsupported Fn actions stay inactive
            d.chord(sensor)
            s=d.snapshot();assert s.calibration_state==0 and s.mode==0
        d.command('cfg key 3 81 135');s=d.snapshot(3)
        assert s.result==1 and s.keyboard_mapping[81]==135
        d.samples[81]=3000;d.tick();assert d.held(135)
        d.samples[81]=3900;d.tick();assert not d.held(135)
        captures=d.snapshot().captures[81]
        d.command('cfg key 4 77 4');assert d.snapshot(4).result==2
        # Duplicate destinations remain held until both physical sources release.
        d.command('cfg key 5 45 135');d.snapshot(5)
        d.samples[45]=d.samples[81]=3000;d.tick();assert d.held(135)
        d.samples[45]=3900;d.tick();assert d.held(135)
        d.samples[81]=3900;d.tick();assert not d.held(135)
        d.command('stream key 3800 45 82',error=True)
        d.command('stream key 3800 45',error=True)
        d.command('stream key 3800 45 81')
        for raw in (3499,3400,3300):d.samples[81]=raw;d.tick()
        d.sequence+=2;d.tick() # acquisition loss cannot become a valid fit
        assert d.call('m1_live_scan_losses')==1 and not d.held(135)
        decoder=KeyDecoder(3800,45,82);lost=False
        for _ in range(40):
            try:list(decoder.feed(d.wait(sx.SAMPLES)[3]))
            except StreamError as error:
                assert 'loss' in str(error) or 'overflow' in str(error);lost=True;break
        assert lost
        d.command('stream gui');s=d.snapshot()
        assert s.scan_errors==1 and not s.velocity_state[81]&2
        assert s.captures[81]==captures
        d.samples[81]=3900;d.tick();d.tick()
        for i in range(D['RAW_VELOCITY_WINDOW']):
            d.samples[81]=3499-100*i;d.tick()
        s=d.snapshot();assert s.captures[81]==captures+1 and s.velocity_state[81]&2
        assert abs(s.velocity[81]-800000/4500000)<0.000001
        d.samples[81]=3900;d.tick()
        # MIDI mode and note generation use the same production coordinator.
        d.chord(56);assert d.snapshot().performance_mode==1
        d.events.clear()
        for i in range(D['RAW_VELOCITY_WINDOW']):d.samples[29]=3499-100*i;d.tick()
        for _ in range(10):d.tick()
        assert any(e[1]==0x90 and e[2]==72 and e[3] for e in d.events)
        d.samples[29]=3900;d.tick()
        for _ in range(10):d.tick()
        assert any(e[1]==0x80 and e[2]==72 for e in d.events)
        # Reset loses the GUI lease and held notes; a fresh HELLO is mandatory.
        d.call('m1_test_usb_event',2);d.tick();d.call('m1_test_usb_event',3);d.tick()
        assert not d.call('midi_control_ready')
        d.messages.clear();d.send(sx.HELLO);assert d.wait(sx.READY)[1]==123
        d.commands=0;d.command('stream gui')
        # A wake-only sample must not enter the periodic velocity/capture path.
        d.call('m1_test_live_periodic',0);d.tick()
        assert d.call('m1_test_live_get',2)==1 # unconsumed wake frame
        for _ in range(500):d.tick(frame=False)
        s=d.snapshot();assert s.flags&8 and not s.flags&2
        d.call('m1_test_live_periodic',1);d.samples=[3900]*82;d.tick();d.tick()
        # Staleness blanks the latest lighting once the output is available.
        d.call('m1_test_live_led',0);n=d.call('m1_test_live_get',1)
        d.tick(frame=False,step=110000)
        assert d.call('m1_test_live_get',1)==n
        d.call('m1_test_live_led',1);d.tick(frame=False)
        rgb=bytes(d.cpu.mem_read(d.call('m1_test_live_get',0),246));assert not any(rgb)
        d.call('m1_live_stop',d.time//1000);d.tick(frame=False)
        assert d.hid==bytes(30)
        d.cpu.mem_write(INPUT,struct.pack('<82H',*([1000]*82)))
        d.cpu.mem_write(OUTPUT,struct.pack('<82H',*([4096]*82)))
        assert not d.call('m1_live_init',INPUT,OUTPUT) # must drain MIDI releases first
        for _ in range(160):d.tick(frame=False)
        assert d.call('m1_live_init',INPUT,OUTPUT)
        d.tick();assert not d.call('midi_control_ready')
        print(f'PASS M1 {"HS" if high else "FS"} foreground: scan/keymap/HID/MIDI/GUI, capture loss, rearm, USB epoch, wake exclusion and lighting safety')
    d=Live(path,True,sequence=0xfffffffd,time=0xffffffff*1000-1000)
    for _ in range(32):d.tick()
    assert not d.call('m1_live_scan_losses') # frame sequence and both clocks wrapped
    d.samples[45]=3000;d.tick();assert d.held(4)
    d.sequence-=1;d.tick() # duplicate frame is also not a consecutive sample
    assert d.call('m1_live_scan_losses')==1 and not d.held(4)
    print('PASS M1 foreground: independent clock/scan wrap, duplicate rejection and explicit restart after release drain')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf');args=p.parse_args()
    integration(args.elf)
if __name__=='__main__':main()
