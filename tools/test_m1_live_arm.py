"""M1 foreground integration: scripted acquisitions, actual app/SDK USB/GUI.

Does not open hardware or model physical cadence, DMA movement or LED timing.
"""
import argparse
import struct
import zlib
from test_m1_usb_arm import Device, INPUT, OUTPUT, USB
from test_m1_hal_arm import RadioArm, DMA, GPIO, RADIO_SPI, factory_memory, FACTORY_UPPER
import midi_sysex as sx
from keyboard_gui_model import decode
from keyboard_capture import KeyDecoder, StreamError
from firmware_defaults import DEFAULTS as D


class Live(Device,RadioArm):
    def call(self,name,*args,instructions=3000000):
        if name=='m1_live_init' and len(args)==3:args=(*args,0) # no provisional released frame
        return super().call(name,*args,instructions=instructions)

    def __init__(self,path,high,sequence=0,time=0,mode=6,transports=False,storage=False):
        super().__init__(path,high)
        self.samples=[3900]*82;self.sequence=sequence;self.time=time;self.commands=0
        self.messages=[];self.wire=bytearray();self.events=[];self.hid=bytes(30)
        self.radio_packets=[];self.radio_complete=True;self.peer_mode=mode;self.peer_pending=False
        self.radio_slots=bytes(6);self.radio_bitmap=bytes(15);self.radio_modifiers=0
        self.ops=self.call('m1_test_live_transports') if transports else 0
        self.storage_ops=self.call('m1_test_live_storage') if storage else 0
        if mode!=6:self.start_radio(mode)
        pages=factory_memory(self)
        assert self.call('m1_live_factory_result')==7
        self.cpu.mem_write(FACTORY_UPPER+2047,b'\0')
        assert not self.call('m1_live_init',mode,self.ops,self.storage_ops)
        assert self.call('m1_live_factory_result')==4
        self.cpu.mem_write(FACTORY_UPPER,pages)
        assert self.call('m1_live_init',mode,self.ops,self.storage_ops)
        assert self.call('m1_live_factory_result')==0
        assert not self.call('m1_live_init',mode,self.ops,self.storage_ops) # no live reinitialization
        self.tick()
    def start_radio(self,mode):
        self.peer_mode=mode
        self.time=self.radio_init(self.time)
        assert self.call('m1_wireless_init',mode,1,self.time)
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
        if self.radio_complete and self.u32(DMA+0x1c)&1:
            n=self.u32(DMA+0x20)
            packet=bytes(self.cpu.mem_read(self.u32(DMA+0x28),n));self.radio_packets.append(packet)
            reply=bytes(n)
            if packet[0]==0x93:self.peer_mode=packet[2]
            if packet[0]==0x92:self.peer_pending=True
            if packet[0]==0x81 and packet[2]==1:
                self.radio_modifiers=packet[3];self.radio_slots=packet[4:10]
            if packet[0]==0x81 and packet[2]==2:self.radio_bitmap=packet[3:18]
            if packet[0]==9:
                payload=bytes((0x10,0,3,self.peer_mode))
                reply=(bytes((0,4))+payload+bytes((sum(payload)&255,))).ljust(n,b'\0')
                self.peer_pending=False
            self.put(GPIO+0xc10,0 if self.peer_pending else 4)
            self.cpu.mem_write(self.u32(DMA+0x3c),reply)
            self.put(DMA,0x330);self.put(RADIO_SPI+8,2)
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
    def radio_held(self,usage):
        return usage in self.radio_slots or (usage<120 and bool(self.radio_bitmap[usage//8]&(1<<(usage%8))))
    def run(self,ticks=200):
        for _ in range(ticks):self.tick()
    def chord(self,sensor):
        self.samples[77]=self.samples[sensor]=3000;self.tick()
        self.samples[77]=self.samples[sensor]=3900;self.tick();self.tick()
        for _ in range(180):self.tick()


def integration(path):
    for high in (False,True):
        d=Live(path,high)
        d.send(sx.HELLO);assert b'MG-M1V5TMR' in d.wait(sx.READY)[3]
        d.command('stream gui');s=d.snapshot()
        assert s.count==82 and s.sample_hz==8000 and s.raw==(3959,)*82
        assert s.calibration_flags==2 and s.storage_flags in (0,2) and s.storage_slot==255
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
        points=[4096-((4000-(3499-100*i))*4095+1500)//3000 for i in range(D['RAW_VELOCITY_WINDOW'])]
        intervals=[a-b for a,b in zip(points,points[1:])]
        ordered=sorted(intervals);median=ordered[len(ordered)//2]
        intervals.pop(max(range(len(intervals)),key=lambda i:abs(intervals[i]-median)))
        assert abs(s.velocity[81]-(sum(intervals)/len(intervals))*8000/4500000)<0.000001
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
        assert not d.call('m1_live_init',6,0,0) # must drain MIDI releases first
        for _ in range(160):d.tick(frame=False)
        assert d.call('m1_live_init',6,0,0)
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
    wireless_integration(args.elf)
    persistence(args.elf)
    calibration_persistence(args.elf)
    power_handoff(args.elf)


def power_handoff(path):
    def park(d):
        for _ in range(1200):
            d.tick()
            if d.call('m1_live_power_park'):return
        raise AssertionError('Power handoff did not finish local output cleanup')
    def reconnect(d):
        d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY);d.commands=0
        d.command('stream gui');return d.snapshot()
    for high in (False,True):
        d=Live(path,high,storage=True)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        d.command('cfg key 1 81 135');d.command('cfg set 2 81 2700 3100')
        d.command('cfg velocity 3 7');d.snapshot(3)
        if high:
            d.chord(56);d.chord(51) # unsaved MIDI + Janko must survive sleep
            for i in range(D['RAW_VELOCITY_WINDOW']):d.samples[29]=3499-i*100;d.tick()
            d.run(10);assert any(e[1]==0x90 and e[3] for e in d.events)
        else:
            d.samples[81]=2500;d.tick();assert d.held(135)
        d.events.clear()
        assert not d.call('m1_live_power_park')
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        assert d.call('m1_live_power_suspend',d.time//1000)
        assert d.call('m1_live_power_suspend',d.time//1000) # no second panic/gap
        assert d.call('m1_live_scan_losses')==1
        assert not d.call('m1_live_power_park')
        d.call('m1_test_live_led',0);d.run(300)
        assert not d.call('m1_live_power_park')
        assert d.hid==bytes(30) and not d.call('midi_control_ready')
        if high:
            assert {e[2] for e in d.events if e[1]==0x80}==set(range(128))
            assert any(e[1:]==bytes((0xb0,64,0)) for e in d.events)
        # Host commands/HELLO cannot restart streams during handoff.
        d.send(sx.HELLO);d.run(2);assert not d.call('midi_control_ready')
        d.call('m1_test_live_led',1);park(d)
        assert not d.call('m1_live_init',6,d.ops,d.storage_ops)
        d.samples=[3900]*82;d.tick() # pending, deliberately unconsumed while parked
        assert d.call('m1_test_live_get',2)==1
        frames=d.call('m1_test_live_get',1);writes=len(d.writes)
        d.run(20)
        assert d.call('m1_test_live_get',1)==frames and len(d.writes)==writes
        assert not d.call('m1_test_live_storage_count',2)
        assert not d.call('m1_live_power_resume',d.time//1000,0)
        d.call('m1_test_live_periodic',0)
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        d.call('m1_test_live_periodic',1)
        assert d.call('m1_live_power_resume',d.time//1000,1)
        assert not d.call('m1_test_live_get',2) # unread pre-wake neutral discarded
        d.samples[81]=2500;d.tick()
        s=reconnect(d)
        assert s.keyboard_mapping[81]==135 and s.press[81]==2700 and s.release[81]==3100
        assert s.velocity_start==7 and s.performance_mode==int(high)
        assert bool(s.flags&64)==high and not s.flags&2 and s.scan_errors==1
        assert s.storage_flags==2 and not s.storage_generation and d.hid==bytes(30)
        assert not d.call('m1_test_live_storage_count',2)
        d.samples=[3900]*82;d.run(5)
        if not high:
            d.samples[81]=2500;d.tick();assert d.held(135)
        # Once parked, terminal stop must not restart foreground peripheral work.
        assert d.call('m1_live_power_suspend',d.time//1000);park(d)
        d.call('m1_live_stop',d.time//1000)
        writes=len(d.writes);d.run(20);assert len(d.writes)==writes
        assert not d.call('m1_live_power_suspend',d.time//1000)
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        assert not d.call('m1_live_init',6,d.ops,d.storage_ops)
        print(f'PASS M1 {"HS MIDI" if high else "FS HID"} power handoff: drain/backpressure, parked ownership, unsaved settings preserved, stale-frame discard, fresh lease and neutral rearm')
    for mode in (0,1,2,5):
        d=Live(path,True,mode=mode,storage=True);d.run()
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        d.command('cfg key 1 81 135');d.snapshot(1)
        d.samples[81]=3000;d.run();assert d.radio_held(135)
        assert d.call('m1_live_power_suspend',d.time//1000)
        assert not d.call('m1_live_power_park');park(d)
        assert not any(d.radio_slots) and not any(d.radio_bitmap) and not d.events
        packets=len(d.radio_packets);d.run();assert len(d.radio_packets)==packets
        d.call('m1_wireless_stop')
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        d.start_radio(mode)
        # The restoration owner, not parked live, services physical readiness.
        for _ in range(400):
            d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
            if d.call('m1_wireless_ready'):break
        assert d.call('m1_wireless_ready')
        assert d.call('m1_live_power_resume',d.time//1000,1)
        d.run();assert not d.radio_held(135)
        s=reconnect(d);assert s.keyboard_mapping[81]==135 and s.performance_mode==0
        assert not s.flags&2 and not d.call('m1_test_live_storage_count',2)
        d.samples[81]=3900;d.run();d.samples[81]=3000;d.run();assert d.radio_held(135)
    print('PASS M1 radio power handoff: all four modes release locally, parked scheduler ownership, explicit restoration, retained keymaps and no MIDI')


def calibration_persistence(path):
    def bounds(d,name):
        return struct.unpack('<82H',d.cpu.mem_read(d.symbols[name],164))
    def state(d,predicate):
        d.messages.clear()
        for _ in range(16):
            s=d.snapshot()
            if predicate(s):return s
        raise AssertionError((s.calibration_state,s.calibration_reason,s.calibration_flags,
                              s.storage_flags,s.calibration_error))
    def advance(d,ms):
        step=10 if d.peer_mode==6 else 1
        for elapsed in range(0,ms,step):
            if elapsed%D['MIDI_CONTROL_HEARTBEAT_MS']==0:d.send(sx.KEEPALIVE)
            d.tick(step=step*1000)
    def start(high=True,fn=False,mode=6):
        d=Live(path,high,storage=True,mode=mode)
        if mode!=6:d.run()
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        assert state(d,lambda s:s.calibration_flags==6).count==82
        d.command('cfg key 1 81 135');d.command('cfg set 2 81 2700 3100')
        d.command('cfg clean 3');assert d.snapshot(3).result==2 # RESET still unavailable
        if fn:d.chord(61)
        else:
            d.command('cfg calibrate 4');assert d.snapshot(4).result==1
        assert state(d,lambda s:s.calibration_state in (1,2)).calibration_flags==7
        advance(d,D['CALIBRATION_SETTLE_MS']+20)
        state(d,lambda s:s.calibration_state==3)
        d.samples=list(range(1000,1082));d.tick()
        advance(d,D['CALIBRATION_HOLD_MS']+20)
        s=state(d,lambda s:s.calibration_state==5)
        assert s.calibration_completed==82 and s.calibration_flags==7
        assert bounds(d,'lower')==(1000,)*82 and bounds(d,'upper')==(4000,)*82
        assert not d.call('m1_test_live_storage_count',2) and d.hid==bytes(30)
        return d
    for high in (False,True):
        d=start(high,fn=high)
        # The explicit save does not require held calibrated keys to release,
        # but LED backpressure and host output completion still gate it.
        d.call('m1_test_live_led',0);d.call('m1_test_live_storage_gate',1,1,0)
        before=d.call('m1_test_live_storage_count',0)
        advance(d,200)
        assert d.call('m1_test_live_storage_count',0)==before
        assert not d.call('m1_test_live_storage_count',2)
        d.call('m1_test_live_led',1)
        s=state(d,lambda s:s.calibration_state==6)
        assert s.calibration_flags==6 and s.calibration_generation==1
        assert s.storage_flags==1 and s.storage_generation==1 and s.storage_slot==0
        assert bounds(d,'lower')==tuple(range(1000,1082)) and bounds(d,'upper')==(3900,)*82
        assert s.press[81]==2700 and s.release[81]==3100 and s.keyboard_mapping[81]==135
        assert d.call('m1_test_live_storage_count',1)==1 and d.call('m1_test_live_storage_count',2)==1
        assert d.call('m1_live_scan_losses')==1 and d.hid==bytes(30) and not s.flags&2
        advance(d,200);assert d.hid==bytes(30) # held samples cannot rearm
        d.samples=[3900]*82;d.run(5);d.samples[81]=2500;d.tick();assert d.held(135)
        d.samples[81]=3900;d.tick()
        # Restoring a fully saved run must not depend on factory validity.
        d.call('m1_live_stop',d.time//1000);d.run(200)
        d.cpu.mem_write(FACTORY_UPPER+2047,b'\0')
        assert d.call('m1_live_init',6,d.ops,d.storage_ops)
        d.tick();d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY);d.commands=0
        d.command('stream gui');s=state(d,lambda s:s.calibration_generation==1)
        assert s.calibration_flags==6 and s.keyboard_mapping[81]==135
        assert bounds(d,'lower')==tuple(range(1000,1082)) and bounds(d,'upper')==(3900,)*82
        assert d.call('m1_live_factory_result')==4
        print(f'PASS M1 {"HS Fn+C" if high else "FS GUI"} parallel calibration: 82 endpoints, deferred LED/power gate, held-key save, atomic settings/bounds, scan gap and restart')
    for mode in (0,5):
        d=start(mode=mode,fn=mode==0)
        d.call('m1_test_live_storage_gate',1,1,0)
        s=state(d,lambda s:s.calibration_state==6)
        assert s.calibration_generation==1 and s.storage_flags==1
        assert bounds(d,'lower')==tuple(range(1000,1082)) and bounds(d,'upper')==(3900,)*82
        assert d.call('m1_live_transport')==mode and d.call('m1_wireless_ready')
        assert not any(d.radio_slots) and not any(d.radio_bitmap) and not d.events
        assert d.call('m1_live_scan_losses')==1 and d.call('m1_test_live_storage_count',2)==1
    print('PASS M1 BT/2.4GHz calibration: local neutral-output gate, retained transport and no performance MIDI')
    for failure in ('write','begin','resume','cancel','timeout','scan','usb'):
        d=start()
        if failure in ('write','begin','resume'):
            d.call('m1_test_live_storage_gate',2 if failure=='begin' else 1,
                   failure!='resume',0x3100b if failure=='write' else 0)
            s=state(d,lambda s:s.calibration_state==8)
            assert s.calibration_reason==4 and s.calibration_completed==0 and s.storage_flags&4
            assert s.calibration_error=={'write':0x3100b,'begin':0x3100e,'resume':0x3100d}[failure]
            assert d.call('m1_test_live_storage_count',1)==(failure!='begin')
            assert d.call('m1_test_live_storage_count',2)==(failure!='begin')
            assert bool(d.call('m1_live_storage_fault'))==(failure!='write')
            if failure=='write':
                d.command('cfg calibrate 5');assert d.snapshot(5).result==2
        else:
            if failure=='cancel':
                d.command('cfg calcancel 5');assert d.snapshot(5).result==1
            if failure=='timeout':advance(d,D['CALIBRATION_IDLE_MS']+20)
            if failure=='scan':d.sequence+=1;d.tick()
            if failure=='usb':
                d.call('m1_test_usb_event',2);d.tick();d.call('m1_test_usb_event',3);d.tick()
                d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY);d.commands=0;d.command('stream gui')
            s=state(d,lambda s:s.calibration_state==7)
            assert s.calibration_reason=={'cancel':3,'timeout':1,'scan':2,'usb':2}[failure]
            assert not d.call('m1_test_live_storage_count',2)
        assert bounds(d,'lower')==(1000,)*82 and bounds(d,'upper')==(4000,)*82
        writes=d.call('m1_test_live_storage_count',2);advance(d,200)
        assert d.call('m1_test_live_storage_count',2)==writes
    print('PASS M1 calibration faults: no active-bound publication on failed write/gate/resume, cancellation, timeout, acquisition gap or USB epoch; no automatic retry')


def persistence(path):
    def advance(d,ms):
        for _ in range(ms):d.tick(step=1000)
    def snapshot(d):
        d.messages.clear();return d.snapshot()
    def stored(d,predicate):
        # A snapshot already owned by USB may finish after the state changes.
        for _ in range(16):
            s=d.snapshot()
            if predicate(s):return s
        raise AssertionError((s.storage_flags,s.storage_generation,s.calibration_error))
    def restart(d):
        d.call('m1_live_stop',d.time//1000)
        for _ in range(200):d.tick(frame=False)
        assert d.call('m1_live_init',6,d.ops,d.storage_ops)
        d.tick();d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY)
        d.commands=0;d.command('stream gui')
    for high in (False,True):
        d=Live(path,high,storage=True)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        d.command('cfg key 1 81 135');d.command('cfg set 2 81 2700 3100')
        d.command('cfg velocity 3 7');d.chord(56) # persist MIDI mode too
        d.chord(51) # Fn+J: Janko
        advance(d,400)
        s=snapshot(d);assert s.storage_flags==2 and not s.storage_generation
        assert d.call('m1_test_live_storage_count',0)>0
        assert not d.call('m1_test_live_storage_count',2) # denied gate is not a flash error
        d.samples[81]=2500;d.tick()
        d.call('m1_test_live_storage_gate',1,1,0);advance(d,400)
        assert not d.call('m1_test_live_storage_count',2) # no save while a key is held
        d.samples[81]=3900;advance(d,400)
        s=snapshot(d)
        assert s.storage_flags==1 and s.storage_generation==1 and s.storage_slot==0
        assert d.call('m1_test_live_storage_count',1)==1 and d.call('m1_test_live_storage_count',2)==1
        saved=bytes(d.cpu.mem_read(d.call('m1_test_live_storage_page',0),2048))
        assert d.call('m1_live_scan_losses')==1 # intentional pause is visible to capture
        advance(d,400);assert d.call('m1_test_live_storage_count',2)==1
        d.samples[81]=2500;d.events.clear();restart(d);s=snapshot(d)
        assert s.performance_mode==1 and s.flags&64 and s.velocity_start==7 and s.keyboard_mapping[81]==135
        assert s.press[81]==2700 and s.release[81]==3100 and s.storage_generation==1
        assert not s.flags&2 and not any(e[1]==0x90 for e in d.events)
        d.samples[81]=3900;advance(d,30)
        # An accepted command is RAM-only until readback accepts the new page.
        d.command('cfg velocity 4 9');d.call('m1_test_live_storage_gate',1,1,0x3100b)
        advance(d,400);s=stored(d,lambda s:bool(s.storage_flags&4))
        assert s.storage_flags&4 and s.storage_generation==1 and s.calibration_error==0x3100b, (
            s.storage_flags,s.storage_generation,s.calibration_error,
            d.call('m1_test_live_storage_count',0),d.call('m1_test_live_storage_count',2),s.velocity_start)
        writes=d.call('m1_test_live_storage_count',2);advance(d,400)
        assert d.call('m1_test_live_storage_count',2)==writes and not d.call('m1_live_storage_fault')
        d.call('m1_test_live_storage_gate',0,1,0);restart(d)
        assert snapshot(d).velocity_start==7
        # Successful flash plus failed resume is terminal, not permission to
        # keep feeding stale scan state or silently clear the fault via init.
        d.command('cfg velocity 5 8');d.call('m1_test_live_storage_gate',1,0,0)
        advance(d,400)
        assert d.call('m1_live_storage_fault')
        assert not d.call('m1_live_init',6,d.ops,d.storage_ops)
        print(f'PASS M1 {"HS" if high else "FS"} profile lifecycle: pending gate, neutral save, no wear, GUI status, restart restoration, failure latch and terminal resume fault')
        d=Live(path,high,storage=True)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        d.call('m1_test_live_storage_gate',2,1,0);advance(d,400)
        s=stored(d,lambda s:bool(s.storage_flags&4))
        assert d.call('m1_live_storage_fault') and s.calibration_error==0x3100e
        assert not d.call('m1_test_live_storage_count',1) # no end without ownership
        assert not d.call('m1_test_live_storage_count',2) # no write after pause failure
        begins=d.call('m1_test_live_storage_count',0)
        d.call('m1_test_live_storage_gate',1,1,0);advance(d,400)
        assert d.call('m1_test_live_storage_count',0)==begins
        assert not d.call('m1_live_init',6,d.ops,d.storage_ops)
    # Independent synthetic record fixture, with a unique bound for every key.
    # The six fixed MIDI controls have 8 mapping bits; Fn has none, others 15.
    record=bytearray(saved);record[5]=1;struct.pack_into('<I',record,10,7)
    position=19*8
    for sensor in range(82):
        position+=23
        lower,upper=1000+sensor,4000-sensor
        code=(upper-1)*(upper-2)//2+lower-1
        for bit in range(23):
            byte,shift=divmod(position+bit,8)
            record[byte]=(record[byte]&~(1<<shift))|(((code>>bit)&1)<<shift)
        position+=23+(0 if sensor==77 else 8 if sensor in (72,73,74,75,76,78) else 15)
    struct.pack_into('<I',record,2044,zlib.crc32(record[:2044]))
    d=Live(path,True,storage=True)
    d.cpu.mem_write(d.call('m1_test_live_storage_page',0),bytes(record))
    d.cpu.mem_write(FACTORY_UPPER+2047,b'\0')
    restart(d);s=snapshot(d)
    assert d.call('m1_live_factory_result')==4 and s.calibration_generation==7
    assert struct.unpack('<82H',d.cpu.mem_read(d.symbols['lower'],164))==tuple(range(1000,1082))
    assert struct.unpack('<82H',d.cpu.mem_read(d.symbols['upper'],164))==tuple(range(4000,3918,-1))
    print('PASS M1 restore: complete custom calibration supersedes invalid factory bounds without modifying factory flash')


def wireless_integration(path):
    for mode in (0,1,2,5):
        d=Live(path,True,mode=mode)
        assert not d.call('m1_wireless_ready')  # status is not manufactured by live init
        d.run();assert d.call('m1_wireless_ready') and d.call('m1_live_transport')==mode
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        assert d.snapshot().performance_mode==0
        d.command('cfg key 1 81 135');assert d.snapshot(1).result==1
        d.command('cfg key 2 45 135');assert d.snapshot(2).result==1
        d.samples[81]=d.samples[45]=3000;d.run();assert d.radio_held(135)
        assert d.hid==bytes(30) and not d.events
        # GUI endpoint/session resets are independent of the active radio host.
        d.call('m1_test_usb_event',2);d.run()
        assert not d.call('m1_usb_ready') and d.radio_held(135)
        d.call('m1_test_usb_event',3);d.run()
        assert d.radio_held(135) and not d.call('midi_control_ready')
        d.samples[81]=3900;d.run();assert d.radio_held(135)
        d.samples[45]=3900;d.run();assert not d.radio_held(135)
        d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY);d.commands=0
        d.command('stream gui');d.chord(56)
        assert d.snapshot().performance_mode==0 and not d.events
        # Filtered metadata flows to the peer and Fn+Space stays a local menu.
        d.call('m1_test_live_battery',57,1);d.run()
        assert any(p[:4]==bytes((0x90,1,57,57)) for p in d.radio_packets)
        d.samples[77]=d.samples[75]=3000;d.run()
        assert not d.radio_held(44) and not any(d.radio_slots) and not any(d.radio_bitmap)
        assert any(d.cpu.mem_read(d.call('m1_test_live_get',0),246))
        d.samples=[3900]*82;d.run()
        # The copied report pair must outlive application state changes.
        d.samples[81]=3000;d.run()
        d.radio_complete=False
        d.samples[81]=3900
        for _ in range(100):
            d.tick()
            if d.u32(DMA+0x1c)&1:break
        assert d.u32(DMA+0x1c)&1
        pointer=d.u32(DMA+0x28);n=d.u32(DMA+0x20)
        before=bytes(d.cpu.mem_read(pointer,n));d.run(20)
        assert bytes(d.cpu.mem_read(pointer,n))==before
        d.radio_complete=True;d.run();assert not d.radio_held(135)
        d.samples[81]=3000;d.run();assert d.radio_held(135)
        d.sequence+=1;d.tick();d.run()
        assert d.call('m1_live_scan_losses')==1 and not d.radio_held(135)
        d.samples[81]=3900;d.run();d.samples[81]=3000;d.run();assert d.radio_held(135)
        d.call('m1_live_stop',d.time//1000);d.run();assert not d.radio_held(135)
        assert not d.events and d.hid==bytes(30)
        print(f'PASS M1 wireless mode {mode}: actual app/radio/SDK reports, GUI remapping, duplicate ownership, MIDI exclusion, battery, backpressure, GUI epochs and loss/release')
    # The board's host-delivery authorization is independently scripted here;
    # actual report DMA and peer mode eligibility are still required.
    d=Live(path,True,transports=True);d.start_radio(0);d.run();d.chord(56)
    d.events.clear()
    for i in range(D['RAW_VELOCITY_WINDOW']):d.samples[29]=3499-100*i;d.tick()
    d.run(10);assert any(e[1]==0x90 and e[3] for e in d.events)
    d.samples[29]=3900;d.run()
    d.chord(1);assert d.call('m1_live_transport')==6
    assert not d.call('m1_test_live_selection',1)  # local idle alone is insufficient
    d.call('m1_test_live_transport_gate',1,0);d.run(10)
    assert d.call('m1_test_live_selection',1)>0 and d.call('m1_live_transport')==6
    d.call('m1_test_live_transport_gate',1,1);d.run()
    assert d.call('m1_live_transport')==0
    d.samples[45]=3000;d.run();assert d.radio_held(4) and d.hid==bytes(30)
    d.samples[45]=3900;d.run()
    d.call('m1_test_live_transport_gate',0,1);d.chord(5)
    assert d.call('m1_live_transport')==0  # cannot abandon a radio host on local idle
    d.call('m1_test_live_transport_gate',1,1);d.run()
    assert d.call('m1_live_transport')==6 and not d.call('m1_live_transport_fault')
    d.samples[45]=3000;d.run();assert d.held(4) and not d.radio_held(4)
    # Selection can stop/reinitialize the old radio after its release proof;
    # there is no requirement to keep draining a driver that no longer exists.
    d=Live(path,True,mode=0,transports=True);d.run()
    d.call('m1_test_live_transport_gate',1,0);d.chord(2)
    assert d.call('m1_test_live_selection',1)>0 and d.call('m1_live_transport')==0
    d.call('m1_wireless_stop');d.start_radio(1);d.run()
    assert not d.call('m1_live_transport_fault') and d.call('m1_live_transport')==0
    d.call('m1_test_live_transport_gate',0,1);d.run() # old proof already latched
    assert d.call('m1_live_transport')==1 and not d.call('m1_live_transport_fault')
    d.samples[45]=3000;d.run();assert d.radio_held(4)
    d.samples[45]=3900;d.run();d.call('m1_live_stop',d.time//1000);d.run()
    assert not d.call('m1_live_init',1,d.ops,0) # local neutral is not host proof
    d.call('m1_test_live_transport_gate',1,1)
    assert d.call('m1_live_init',1,d.ops,0)
    # An adapter claiming success for the wrong/not-ready radio cannot switch.
    d=Live(path,True,transports=True);d.start_radio(0);d.run()
    d.call('m1_test_live_transport_gate',1,1);d.chord(2)
    assert d.call('m1_live_transport')==6
    # Keep frames fresh across the selection deadline; no fallback host typing.
    for _ in range(D['M1_TRANSPORT_SWITCH_TIMEOUT_MS']//10+1):d.tick(step=10000)
    assert d.call('m1_live_transport_fault') and d.call('m1_live_transport')==6
    d.samples[45]=3000;d.run();assert not d.held(4)
    assert not d.call('m1_live_init',6,d.ops,0) # no silent fault clear
    d=Live(path,True,transports=True);d.call('m1_test_live_transport_gate',1,0);d.chord(1)
    assert d.call('m1_test_live_selection',1)>0
    d.sequence+=1;d.tick();assert d.call('m1_live_transport_fault')
    d.samples[45]=3000;d.run();assert not d.held(4)
    print('PASS M1 Fn transport integration: explicit host-release gate, actual mode confirmation, neutral routing and terminal ambiguous selection')
if __name__=='__main__':main()
