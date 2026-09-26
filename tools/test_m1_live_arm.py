"""M1 foreground integration: scripted acquisitions, actual app/SDK USB/GUI.

Does not open hardware or model physical cadence, DMA movement or LED timing.
"""
import argparse
import struct
import zlib
from test_m1_usb_arm import Device, INPUT, OUTPUT, USB
from test_m1_hal_arm import RadioArm, DMA, GPIO, RADIO_SPI, factory_memory, FACTORY_UPPER
import midi_sysex as sx
from keyboard_gui_model import decode,decode_power,decode_bounds
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
        self.consumer=[];self.radio_consumer=[]
        self.radio_packets=[];self.radio_complete=True;self.peer_mode=mode;self.peer_pending=False;self.peer_state=3
        self.radio_slots=bytes(6);self.radio_bitmap=bytes(15);self.radio_modifiers=0
        self.ops=self.call('m1_transport_ops' if transports=='runtime' else 'm1_test_live_transports') if transports else 0
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
        if self.u32(USB+0x960)&(1<<31):
            self.consumer.append(struct.unpack('<H',self.cpu.mem_read(self.get(9),2))[0]);self.complete(3)
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
            if packet[0]==0x94 and packet[2] in (0,2):self.peer_state=4
            if packet[0]==0x81 and packet[2]==1:
                self.radio_modifiers=packet[3];self.radio_slots=packet[4:10]
            if packet[0]==0x81 and packet[2]==2:self.radio_bitmap=packet[3:18]
            if packet[0]==0x81 and packet[2]==3:self.radio_consumer.append(struct.unpack_from('<H',packet,3)[0])
            if packet[0]==9:
                payload=bytes((0x10,0,self.peer_state,self.peer_mode))
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


def wireless_supported(path):
    """Ask the artifact whether the Bluetooth/2.4 GHz feature is built in."""
    return bool(Live(path,True).call('m1_wireless_supported'))


def knob_integration(path):
    wireless=wireless_supported(path)
    modes=((False,6),(True,6),(False,0),(True,5)) if wireless else ((False,6),(True,6))
    for high,mode in modes:
        d=Live(path,high,mode=mode,transports='runtime');d.run(400)
        def samples(phase,pressed=False,count=None):
            d.put(GPIO+0x810,((phase&1)<<10)|((phase>>1)<<12)|(0 if pressed else 0x800))
            for _ in range(count if count is not None else D['ENCODER_PHASE_STABLE_SAMPLES']):
                d.call('m1_encoder_irq');d.tick()
        def turn(positive=True):
            for phase in ((1,3,2,0) if positive else (2,3,1,0)):samples(phase)
        output=d.consumer if mode==6 else d.radio_consumer
        d.put(GPIO+0x810,0x800);d.call('m1_encoder_start');d.run(8);output.clear()
        turn();d.run(800);assert output==[D['DEFAULT_M1_ENCODER_POSITIVE_USAGE'],0],(mode,output)
        output.clear();turn(False);d.run(800)
        assert output==[D['DEFAULT_M1_ENCODER_NEGATIVE_USAGE'],0],(mode,output)
        output.clear();samples(0,True,80);d.run(800);samples(0,False,80);d.run(100)
        assert output==[D['DEFAULT_M1_ENCODER_BUTTON_USAGE'],0],(mode,output)
        # Fn/menu interaction never drains old motion into the next host action.
        output.clear();d.samples[77]=3000;d.tick();turn();d.run(100)
        d.samples[77]=3900;d.run(100)
        assert all(v==0 for v in output),output
        output.clear();turn();d.run(800)
        assert output==[D['DEFAULT_M1_ENCODER_POSITIVE_USAGE'],0]
        # An acquisition failure cancels an accepted pulse and all staged motion.
        output.clear();turn();d.run(8);d.sequence+=2;d.tick();d.run(800)
        assert not output or output[-1]==0
        output.clear();d.run(800);assert all(v==0 for v in output)
        output.clear();turn();d.run(8)
        next_output=d.radio_consumer if mode==6 else d.consumer;next_output.clear()
        if wireless or mode!=6:
            d.chord(1 if mode==6 else 5);d.run(1200)
            assert d.call('m1_live_transport')==(0 if mode==6 else 6)
        else:
            # USB-only artifact: Fn+F1 must not select a transport it lacks.
            d.chord(1);d.run(1200)
            assert d.call('m1_live_transport')==6
        assert output[-1]==0 and all(v==0 for v in next_output),(output,next_output)
    print('PASS knob-to-host integration: real GPIO decoder, volume/mute pulses, Fn/loss cleanup and neutral transport handoff'+
          ('' if wireless else ' (USB-only build: wireless selection refused)'))


def recovery_preflight(path):
    for high in (False,True):
        d=Live(path,high)
        d.send(sx.HELLO);d.wait(sx.READY)
        assert not d.call('m1_live_update_requested')
        d.command('bootloader',error=True)
        assert not d.call('m1_live_update_requested')
        # Simulated read-only metadata qualification. The independent storage
        # audit executes the real page reader; main-loop tests own arming/reset.
        d.put(d.symbols['m1_test_recovery_result'],0)
        d.command('bootloader')
        assert d.call('m1_live_update_requested')
    print('PASS M1 live SysEx update preflight: rejected metadata leaves request clear; qualified page queues explicit update at FS/HS')


def integration(path):
    for high in (False,True):
        d=Live(path,high)
        d.send(sx.HELLO);assert b'MG-M1V5TMR' in d.wait(sx.READY)[3]
        d.command('stream gui');s=d.snapshot()
        assert s.count==82 and s.sample_hz==8000 and s.raw==(3959,)*82
        # The GUI-visible capability must match the artifact that answers it.
        assert bool(s.transport_flags & 8)==(not d.call('m1_wireless_supported')),s.transport_flags
        b=decode_bounds(d.command('calibration read')[3])
        assert b.flags==3 and b.samples==(3900,)*82 and b.control==(3959,)*82
        assert b.lower==(1000,)*82 and b.upper==(4000,)*82
        power=decode_power(d.command('power status')[3])
        assert power.flags==0 and power.percent==0 and power.adc==65535
        d.call('m1_test_live_battery',3,1)
        power=decode_power(d.command('power status')[3])
        assert power.flags==29 and power.percent==3
        d.call('m1_test_live_battery',0,0)
        d.command('runtime stats');diagnostic=d.wait(sx.DUMP)[3]
        assert diagnostic[:8]==b'M1PF\x02\x08\x7c\x00' and len(diagnostic)==124
        stamp,sequence,losses,hal_errors=struct.unpack_from('<4I',diagnostic,8)
        assert stamp and sequence and losses==hal_errors==0
        assert struct.unpack_from('<I',diagnostic,120)[0]==(3<<16|1)
        for stage in range(8):
            calls,total,maximum=struct.unpack_from('<3I',diagnostic,24+12*stage)
            assert calls and total==maximum==0 # modeled TMR2 is stationary, not physical timing
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
    # A queue overflow before the first observed sequence can otherwise look
    # consecutive; the acquisition-error counter must still invalidate input.
    d=Live(path,True)
    d.samples[45]=3000;d.tick();assert d.held(4)
    d.call('m1_test_live_scan_errors',1);d.tick()
    assert d.call('m1_live_scan_losses')==1 and not d.held(4)
    print('PASS M1 foreground: independent clock/scan wrap, duplicate rejection and explicit restart after release drain')


def midi_scan_work(path):
    """Instruction-work regression, NOT a real-time or hardware timing test."""
    from unicorn import UC_HOOK_CODE
    d=Live(path,True,storage=True);d.run(20)
    instructions=[0]
    def count(*args):instructions[0]+=1
    handle=d.cpu.hook_add(UC_HOOK_CODE,count)
    # Hooks must also instrument blocks translated during initialization.
    d.cpu.ctl_flush_tb()
    def measure():
        instructions[0]=0
        for _ in range(3):d.tick()
        return instructions[0]
    keyboard=measure()
    d.chord(56);d.run(160) # real menu release, cleanup, neutral rearm
    midi=measure()
    d.samples[45]=3000
    strike=measure()
    d.run(20)
    held=measure()
    for sensor in range(29,42):d.samples[sensor]=3000
    d.run(20)
    chord=measure()
    d.cpu.hook_del(handle)
    # Inactive MIDI should not add another full keyboard's worth of work.
    # This regression allowance is not a cycle budget or an 8 kHz guarantee.
    assert midi*100 < keyboard*135,(keyboard,midi)
    # One held key must not reactivate full-board velocity/note passes.
    assert max(strike,held)*100 < midi*120,(midi,strike,held)
    assert chord*100 < midi*140,(midi,chord)
    assert not d.call('m1_live_scan_losses')
    print(f'PASS scan instruction-work regression: keyboard={keyboard//3}, MIDI={midi//3}, strike={strike//3}, held={held//3}, chord={chord//3}; physical cadence unmodeled')


def modal_scan_work(path):
    """Real menu dispatch; verify observation ownership, not physical timing."""
    from unicorn import UC_HOOK_CODE
    from keyboard_boards import m1_records
    keys={record[7]:record[0] for record in m1_records()}
    # Fn+R is the reset confirmation. This loop keeps the scripted save gate
    # deferred, so confirming must be refused without erasing or a scan gap;
    # the confirmed erase has its own audit below.
    for selector,choice in (('V','0'),('Tab','5'),('E','Q'),('S','H'),('R','Y')):
        d=Live(path,True,storage=True);d.run(20);d.chord(56);d.run(160)
        d.samples[77]=d.samples[keys[selector]]=3000;d.tick();d.run(16)
        d.samples[keys[selector]]=3900;d.tick() # selector first, Fn still held
        d.samples[77]=3900;d.run(160)
        calls=[0];work=[0]
        forbidden={d.symbols[name]&~1 for name in
                   ('keyboard_raw_invalidate','keyboard_engine_init','keyboard_engine_release_all')}
        def hook(cpu,pc,size,user):
            work[0]+=1
            if pc in forbidden:calls[0]+=1
        handle=d.cpu.hook_add(UC_HOOK_CODE,hook);d.cpu.ctl_flush_tb()
        d.run(16)
        assert calls[0]==0,(selector,calls[0]) # no per-scan rearm/reset cycle
        d.cpu.hook_del(handle)
        d.events.clear()
        d.samples[keys[choice]]=1000;d.run(16)
        assert not any(e[1]==0x90 for e in d.events),(selector,'menu leaked a note')
        d.samples[keys[choice]]=3900;d.run(16)
        # Escape closes the digit pages; selection release closes music/reset.
        d.samples[keys['Esc']]=1000;d.tick()
        d.samples[keys['Esc']]=3900;d.run(160)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui');s=d.snapshot()
        assert s.flags&2 and s.performance_mode==1 and not s.midi_errors
        if selector=='V':assert s.velocity_start==10
        if selector=='Tab':assert s.press[0]<D['RAW_DEFAULT_PRESS']
        if selector=='R':
            assert s.press[81]==D['RAW_DEFAULT_PRESS']
            assert not d.call('m1_test_live_storage_count',3) # refused, never erased
        assert not d.call('m1_live_scan_losses')
        print(f'PASS M1 Fn+{selector}: held/release entry, stable modal observation ({work[0]//16} instructions/loop), choice, exit/rearm; cadence unmodeled')


def system_menu_work(path):
    from unicorn import UC_HOOK_CODE
    for selector in (1,2,3,4,5,75): # Fn+F1..F5 and Fn+Space
        d=Live(path,True,transports=True);d.run(20)
        d.samples[77]=d.samples[selector]=3000;d.tick()
        forbidden={d.symbols[name]&~1 for name in
                   ('keyboard_raw_invalidate','keyboard_engine_init','keyboard_engine_release_all')}
        calls=[0]
        def hook(cpu,pc,size,user):
            if pc in forbidden:calls[0]+=1
        handle=d.cpu.hook_add(UC_HOOK_CODE,hook);d.cpu.ctl_flush_tb()
        d.run(32)
        assert calls[0]==0,(selector,'held',calls[0])
        d.cpu.hook_del(handle)
        d.samples[77]=d.samples[selector]=3900;d.run(4)
        if selector<5: # deliberately stalled host-drain, all keys neutral
            handle=d.cpu.hook_add(UC_HOOK_CODE,hook);d.cpu.ctl_flush_tb();calls[0]=0
            d.run(32)
            assert calls[0]==0,(selector,'draining',calls[0])
            d.cpu.hook_del(handle)
            assert not d.call('m1_test_live_selection',1)
        assert not d.call('m1_live_scan_losses') and d.hid==bytes(30)
    print('PASS M1 system input ownership: held Fn+F1..F5/Space and neutral host-drain wait do not reset/rearm per scan')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('elf');args=p.parse_args()
    recovery_preflight(args.elf)
    integration(args.elf)
    midi_scan_work(args.elf)
    modal_scan_work(args.elf)
    system_menu_work(args.elf)
    knob_integration(args.elf)
    wireless_integration(args.elf)
    runtime_transports(args.elf)
    runtime_pairing(args.elf)
    runtime_reconnect(args.elf)
    persistence(args.elf)
    reset_profile(args.elf)
    calibration_persistence(args.elf)
    power_handoff(args.elf)
    source_handoff(args.elf)
    source_switch_handoff(args.elf)


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
        d.run(4);assert d.call('m1_live_power_activity',OUTPUT)
        assert d.call('m1_live_power_activity',OUTPUT) and bytes(d.cpu.mem_read(OUTPUT,1))==b'\0'
        d.samples[81]=3000;d.tick();d.samples[81]=3900;d.tick()
        assert d.call('m1_live_power_activity',OUTPUT) and bytes(d.cpu.mem_read(OUTPUT,1))==b'\1'
        d.run(D['RAW_VELOCITY_WINDOW']);d.call('m1_live_power_activity',OUTPUT)
        assert d.call('m1_live_power_activity',OUTPUT) and bytes(d.cpu.mem_read(OUTPUT,1))==b'\0'
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
        d.call('m1_test_live_led',0)
        for _ in range(300):
            d.tick()
            assert not d.call('m1_test_live_get',2), 'draining retained an acquisition'
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
    wireless=wireless_supported(path)
    for mode in ((0,1,2,5) if wireless else ()):
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
    print('PASS M1 radio power handoff: all four modes release locally, parked scheduler ownership, explicit restoration, retained keymaps and no MIDI'
          if wireless else 'SKIP radio power handoff: this build has no Bluetooth/2.4 GHz')
    for mode in ((0,1,2,5) if wireless else ()):
        d=Live(path,True,mode=mode,storage=True);d.peer_state=1;d.run(400)
        assert d.call('m1_wireless_selected',mode) and not d.call('m1_wireless_ready')
        assert not d.call('m1_wireless_reports_sent')
        d.samples[81]=3000;d.run(20)
        assert d.call('m1_live_power_suspend',d.time//1000);park(d)
        # A never-linked neutral baseline is cancellable, not a delivered
        # release. This must permit critical sleep without an RF host.
        assert d.call('m1_wireless_request_sleep',3,1)
        for _ in range(100):
            d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
            if d.call('m1_wireless_sleep_sent')==3:break
        assert d.call('m1_wireless_sleep_sent')==3
        assert not d.call('m1_wireless_reports_sent') and not any(d.radio_slots)
        d.call('m1_wireless_stop');d.start_radio(mode)
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        for _ in range(400):
            d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
            if d.call('m1_wireless_selected',mode):break
        assert d.call('m1_wireless_selected',mode) and not d.call('m1_wireless_ready')
        assert d.call('m1_live_power_resume',d.time//1000,1)
        d.run(10);s=reconnect(d);assert not s.flags&2
        # A key held through wake/connection is not replayed. New neutral
        # acquisition and a fresh press are still required on host arrival.
        d.peer_state=3;d.run(2*D['M1_RADIO_QUERY_US']//125)
        assert d.call('m1_wireless_ready')
        assert not d.radio_held(0x4f)
        d.samples[81]=3900;d.run(100);d.samples[81]=3000;d.run(200)
        assert d.radio_held(0x4f)
    print('PASS M1 unlinked power handoff: neutral cancellation, critical sleep packet, fresh searching-mode restore and no held-key replay on connect'
          if wireless else 'SKIP unlinked power handoff: this build has no Bluetooth/2.4 GHz')
    for mode in ((0,1,2) if wireless else ()):
        d=Live(path,True,mode=mode);d.run(400)
        assert not d.call('m1_wireless_resume_retained',1,d.time)
        assert d.call('m1_live_power_suspend',d.time//1000);park(d)
        for _ in range(1000):
            if d.call('m1_wireless_request_sleep',5,1):break
            d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
        else:raise AssertionError('retention request did not drain')
        assert not d.call('m1_wireless_resume_retained',1,d.time)
        for _ in range(100):
            d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
            if d.call('m1_wireless_sleep_sent')==5:break
        assert d.call('m1_wireless_sleep_sent')==5
        assert not d.call('m1_wireless_resume_retained',0,d.time)
        writes=len(d.writes)
        assert d.call('m1_wireless_resume_retained',1,d.time)
        assert len(d.writes)==writes and not d.call('m1_wireless_selected',mode)
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        for _ in range(400):
            d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
            if d.call('m1_wireless_selected',mode):break
        assert d.call('m1_wireless_selected',mode)
        assert d.call('m1_live_power_resume',d.time//1000,1)
    print('PASS retained BT resume: no GPIO reset pulse, explicit restoration, fresh mode handshake and neutral rearm'
          if wireless else 'SKIP retained BT resume: this build has no Bluetooth/2.4 GHz')


def source_handoff(path):
    def detach(d):
        # Mirror the hardware owner's endpoint abort at the class boundary.
        # Physical reset/PHY behavior belongs to the separate HAL audit.
        d.call('m1_usb_bind',0)
        for at in (0x920,0x940,0x960):d.put(USB+at,0)
        assert not d.call('m1_usb_ready') and d.call('m1_usb_in_idle')
    def park(d):
        for _ in range(1200):
            d.tick()
            if d.call('m1_live_power_park'):return
        raise AssertionError('Source handoff failed to park')
    targets=((False,0),(True,6),(True,5)) if wireless_supported(path) else ((True,6),(False,6))
    for high,target in targets:
        d=Live(path,high,storage=True);d.run(10)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        d.command('cfg key 1 81 135');d.command('cfg set 2 81 2700 3100');d.snapshot(2)
        d.samples[81]=2500;d.run(10);assert d.held(135)
        if target==5:
            d.samples[81]=3900;d.run(10);d.chord(56)
            for i in range(D['RAW_VELOCITY_WINDOW']):d.samples[29]=3499-i*100;d.tick()
            d.run(10);assert any(e[1]==0x90 and e[3] for e in d.events)
        assert not d.call('m1_live_source_suspend',d.time//1000,1) # host still owns endpoints
        detach(d)
        assert d.call('m1_live_source_suspend',d.time//1000,1)
        park(d)
        assert not d.call('m1_live_power_resume',d.time//1000,1)
        assert not d.call('m1_live_source_resume',d.time//1000,target,0)
        if target!=6:
            assert not d.call('m1_live_source_resume',d.time//1000,target,1)
            d.start_radio(target)
            for _ in range(400):
                d.time+=125;d.call('m1_wireless_service',d.time);d.collect()
                if d.call('m1_wireless_selected',target):break
            assert d.call('m1_wireless_selected',target)
        # Source resume may restore USB before host enumeration. Ordinary
        # battery wake is intentionally stricter; this exception is explicit.
        assert d.call('m1_live_source_resume',d.time//1000,target,1)
        d.run(10)
        d.call('m1_test_usb_init',int(high));d.hid=bytes(30)
        d.messages.clear();d.commands=0;d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        s=d.snapshot()
        assert s.keyboard_mapping[81]==135 and s.press[81]==2700 and s.release[81]==3100
        assert s.storage_flags==2 and not d.call('m1_test_live_storage_count',2)
        assert s.performance_mode==0 # USB MIDI is cancelled by wireless fallback
        assert not s.flags&2 and not d.held(135) and not d.radio_held(135)
        d.samples=[3900]*82;d.run(200);d.samples[81]=2500;d.run(200)
        assert (d.held(135) if target==6 else d.radio_held(135))
        assert not d.call('m1_live_transport_fault') and d.call('m1_live_scan_losses')==1
    for mode in ((0,1,2,5) if wireless_supported(path) else ()):
        d=Live(path,True,mode=mode);d.run(400)
        d.samples[81]=3000;d.run(200);assert d.radio_held(0x4f)
        detach(d);assert d.call('m1_live_source_suspend',d.time//1000,1)
        assert not d.call('m1_live_power_park') # USB loss is NOT a wireless release
        park(d);assert not any(d.radio_slots) and not any(d.radio_bitmap)
        assert d.call('m1_live_source_resume',d.time//1000,mode,1)
        d.run(200);assert not d.radio_held(0x4f)
    print('PASS source handoff: explicit USB abandonment, radio drain, offline USB restore, unsaved settings, fresh control lease and no held-key replay'+
          ('' if wireless_supported(path) else ' (USB-only build: no radio drain case)'))


def source_switch_handoff(path):
    # A physical cable edge may cancel an Fn selection only BEFORE the first
    # platform select/pair call. Sleep must not steal that user transaction.
    for high in (False,True):
        wireless=wireless_supported(path)
        cases=((6,1),(0,5)) if wireless else ((6,1),)
        for original,selector in cases:
            d=Live(path,high,mode=original,transports=True,storage=True);d.run(400)
            d.call('m1_test_live_transport_gate',0,0)
            d.chord(selector)
            assert not d.call('m1_test_live_selection',1)
            assert not d.call('m1_live_power_suspend',d.time//1000)
            detached=original==6
            if detached:
                # Rejection before endpoint release must leave the switch live.
                assert not d.call('m1_live_source_suspend',d.time//1000,1)
                assert not d.call('m1_live_power_suspend',d.time//1000)
                d.call('m1_usb_bind',0)
                for at in (0x920,0x940,0x960):d.put(USB+at,0)
            assert d.call('m1_live_source_suspend',d.time//1000,int(detached))
            for _ in range(1200):
                d.tick()
                if d.call('m1_live_power_park'):break
            else:raise AssertionError('Cancelled Fn selection did not drain/park')
            assert not d.call('m1_test_live_selection',1)
            assert d.call('m1_live_transport')==original
            assert d.call('m1_live_source_resume',d.time//1000,original,1)
            if detached:d.call('m1_test_usb_init',int(high))
            d.samples[81]=3000;d.run(20)
            assert not d.held(0x4f) and not d.radio_held(0x4f)
            d.samples[81]=3900;d.run(20)
            d.samples[81]=3000;d.run(200)
            assert d.held(0x4f) if original==6 else d.radio_held(0x4f)
            assert not d.call('m1_live_transport_fault')
            assert not d.call('m1_test_live_selection',1)
    # Once the platform callback ran, it may already have touched the peer.
    # Neither cable handling nor ordinary sleep can silently roll it back.
    d=Live(path,True,transports=True);d.run(20)
    d.call('m1_test_live_transport_gate',1,0);d.chord(1)
    assert d.call('m1_test_live_selection',1)>0
    assert not d.call('m1_live_source_suspend',d.time//1000,0)
    assert not d.call('m1_live_power_suspend',d.time//1000)
    print('PASS FS/HS source/Fn overlap: cancel before platform selection, retain old host, drain, neutral rearm; attempted selection still protected')


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
        # A deferred save gate must refuse the now-available RESET without
        # erasing; the destructive path has its own audit.
        d.command('cfg clean 3');assert d.snapshot(3).result==2
        assert not d.call('m1_test_live_storage_count',3)
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
        b=decode_bounds(d.command('calibration read')[3])
        assert b.lower==(1000,)*82 and b.upper==(4000,)*82 # never publish uncommitted candidates
        assert bounds(d,'lower')==(1000,)*82 and bounds(d,'upper')==(4000,)*82
        assert not d.call('m1_test_live_storage_count',2) and d.hid==bytes(30)
        return d
    for high in (False,True):
        d=start(high,fn=high)
        advance(d,D['CALIBRATION_IDLE_MS']+20)
        s=state(d,lambda s:s.calibration_state==5)
        assert s.calibration_completed==82 and not s.calibration_idle
        d.command('stream off');d.run(50)
        d.command('runtime storage');gate=d.wait(sx.DUMP)[3]
        assert gate[:8]==b'M1SG\x01\x00\x24\x00' and gate[32:]==bytes((82,5,82,0))
        assert struct.unpack_from('<I',gate,16)[0]>0
        d.command('stream gui')
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
        b=decode_bounds(d.command('calibration read')[3])
        assert b.flags==3 and b.samples==b.lower==tuple(range(1000,1082)),(b.flags,b.samples[:4],b.lower[:4],b.control[:4])
        assert b.upper==(3900,)*82 and b.control==(1,)*82
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
    wireless=wireless_supported(path)
    for mode in ((0,5) if wireless else ()):
        d=start(mode=mode,fn=mode==0)
        d.call('m1_test_live_storage_gate',1,1,0)
        s=state(d,lambda s:s.calibration_state==6)
        assert s.calibration_generation==1 and s.storage_flags==1
        assert bounds(d,'lower')==tuple(range(1000,1082)) and bounds(d,'upper')==(3900,)*82
        assert d.call('m1_live_transport')==mode and d.call('m1_wireless_ready')
        assert not any(d.radio_slots) and not any(d.radio_bitmap) and not d.events
        assert d.call('m1_live_scan_losses')==1 and d.call('m1_test_live_storage_count',2)==1
    print('PASS M1 BT/2.4GHz calibration: local neutral-output gate, retained transport and no performance MIDI')
    for failure in ('write','begin','resume','cancel','scan','usb'):
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
            if failure=='scan':d.sequence+=1;d.tick()
            if failure=='usb':
                d.call('m1_test_usb_event',2);d.tick();d.call('m1_test_usb_event',3);d.tick()
                d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY);d.commands=0;d.command('stream gui')
            s=state(d,lambda s:s.calibration_state==7)
            assert s.calibration_reason=={'cancel':3,'scan':2,'usb':2}[failure]
            assert not d.call('m1_test_live_storage_count',2)
        assert bounds(d,'lower')==(1000,)*82 and bounds(d,'upper')==(4000,)*82
        writes=d.call('m1_test_live_storage_count',2);advance(d,200)
        assert d.call('m1_test_live_storage_count',2)==writes
    print('PASS M1 calibration faults: no active-bound publication on failed write/gate/resume, cancellation, acquisition gap or USB epoch; no automatic retry')


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


def reset_profile(path):
    """Fn+R / cfg clean: both custom pages are erased and verified blank, RAM
    returns to defaults with factory electrical bounds, and a failed or denied
    erase never reports success. Factory, calibration and bootloader pages are
    outside every callback this path can reach."""
    def advance(d,ms):
        for _ in range(ms):d.tick(step=1000)
    def stored(d,predicate):
        for _ in range(16):
            s=d.snapshot()
            if predicate(s):return s
        raise AssertionError((s.storage_flags,s.calibration_generation,s.calibration_error))
    def restart(d):
        d.call('m1_live_stop',d.time//1000)
        for _ in range(200):d.tick(frame=False)
        assert d.call('m1_live_init',6,d.ops,d.storage_ops)
        d.tick();d.messages.clear();d.send(sx.HELLO);d.wait(sx.READY)
        d.commands=0;d.command('stream gui')
    def blank(d,slot):
        page=d.call('m1_test_live_storage_page',slot)
        return bytes(d.cpu.mem_read(page,2048))==b'\xff'*2048
    def bounds(d,name):
        return struct.unpack('<82H',d.cpu.mem_read(d.symbols[name],164))
    def distinct_bounds(template):
        # Same independent synthetic fixture as the restore audit: every key
        # gets its own electrical bound, so the factory fallback is visible.
        record=bytearray(template);record[5]=1;struct.pack_into('<I',record,10,7)
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
        return bytes(record)
    for high in (False,True):
        d=Live(path,high,storage=True)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        default=d.snapshot()
        assert not d.call('m1_test_live_storage_count',3)
        d.command('cfg key 1 81 135');d.command('cfg set 2 81 2700 3100')
        d.command('cfg velocity 3 7');d.chord(56);d.chord(51)
        d.call('m1_test_live_storage_gate',1,1,0);advance(d,400)
        s=stored(d,lambda s:s.storage_generation==1)
        assert s.storage_flags==1 and s.storage_slot==0 and s.keyboard_mapping[81]==135
        assert s.velocity_start==7 and s.performance_mode==1 and s.flags&64
        writes=d.call('m1_test_live_storage_count',2)
        ends=d.call('m1_test_live_storage_count',1)
        assert (writes,ends)==(1,1)
        template=bytes(d.cpu.mem_read(d.call('m1_test_live_storage_page',0),2048))
        d.cpu.mem_write(d.call('m1_test_live_storage_page',0),distinct_bounds(template))
        restart(d)
        assert bounds(d,'lower')==tuple(range(1000,1082))
        assert bounds(d,'upper')==tuple(range(4000,3918,-1))
        # A deferred save gate refuses RESET without erasing or latching a fault.
        d.call('m1_test_live_storage_gate',0,1,0)
        d.command('cfg clean 4');assert d.snapshot(4).result==2
        assert not d.call('m1_test_live_storage_count',3)
        assert not d.call('m1_live_storage_fault')
        # The confirmed reset erases both custom pages inside one gate pair,
        # writes nothing, and leaves the factory records in place.
        d.call('m1_test_live_storage_gate',1,1,0)
        d.command('cfg clean 5');assert d.snapshot(5).result==1
        assert d.call('m1_test_live_storage_count',3)==2
        assert d.call('m1_test_live_storage_count',2)==writes
        assert d.call('m1_test_live_storage_count',1)==ends+1
        assert blank(d,0) and blank(d,1)
        assert not d.call('m1_live_storage_fault') and d.call('m1_live_factory_result')==0
        # Defaults and factory bounds are active on the confirmed neutral frame.
        advance(d,400);s=d.snapshot()
        assert (s.keyboard_mapping,s.press,s.release)==(default.keyboard_mapping,default.press,default.release)
        assert s.velocity_start==D['DEFAULT_MIDI_VELOCITY_START']
        assert s.performance_mode==D['DEFAULT_MIDI_MODE'] and not s.flags&64
        assert bounds(d,'lower')==(1000,)*82 and bounds(d,'upper')==(4000,)*82
        # No restart may resurrect the cleared profile.
        restart(d);s=d.snapshot()
        assert s.keyboard_mapping[81]!=135 and s.press[81]!=2700 and s.velocity_start!=7
        assert not s.flags&64 and bounds(d,'lower')==(1000,)*82
        # A failed erase latches the store fault instead of claiming success,
        # and no later edit can write the profile again.
        d.call('m1_test_live_storage_gate',1,1,0x3100b)
        d.command('cfg clean 6');assert d.snapshot(6).result==2
        assert d.call('m1_test_live_storage_count',3)==3
        s=stored(d,lambda s:bool(s.storage_flags&4))
        assert s.calibration_error==0x3100b
        written=d.call('m1_test_live_storage_count',2)
        d.command('cfg velocity 7 9');advance(d,400)
        assert d.call('m1_test_live_storage_count',2)==written
        print(f'PASS M1 {"HS" if high else "FS"} profile RESET: confirmed clean erases both custom pages, factory bounds return, defaults survive restart and a failed or denied erase never claims success')


def wireless_integration(path):
    if not wireless_supported(path):
        print('SKIP wireless integration: this build has no Bluetooth/2.4 GHz')
        return
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
    # Selection can stop/reinitialize the old radio after its neutral handoff;
    # there is no requirement to keep draining a driver that no longer exists.
    d=Live(path,True,mode=0,transports=True);d.run()
    d.call('m1_test_live_transport_gate',1,0);d.chord(2)
    assert d.call('m1_test_live_selection',1)>0 and d.call('m1_live_transport')==0
    d.call('m1_wireless_stop');d.start_radio(1);d.run()
    assert not d.call('m1_live_transport_fault') and d.call('m1_live_transport')==0
    d.call('m1_test_live_transport_gate',0,1);d.run() # old handoff already latched
    assert d.call('m1_live_transport')==1 and not d.call('m1_live_transport_fault')
    d.samples[45]=3000;d.run();assert d.radio_held(4)
    d.samples[45]=3900;d.run();d.call('m1_live_stop',d.time//1000);d.run()
    assert not d.call('m1_live_init',1,d.ops,0) # outer owner still denies handoff
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
    print('PASS M1 Fn transport integration: explicit neutral-handoff gate, actual mode confirmation, neutral routing and terminal ambiguous selection')
def runtime_transports(path):
    if not wireless_supported(path):
        print('SKIP runtime transport selection: this build has no Bluetooth/2.4 GHz')
        return
    # Real runtime callbacks and SPI/SDK, not the permissive scripted adapter.
    for high in (False,True):
        d=Live(path,high,transports='runtime');d.run(160)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        for key,target,wire in ((1,0,2),(2,1,3),(3,2,4),(4,5,5),(5,6,1)):
            d.chord(key);d.run(250)
            assert d.call('m1_live_transport')==target and not d.call('m1_live_transport_fault')
            # Discard pre-switch snapshots; the next one must identify the target.
            d.messages=[m for m in d.messages if m[0]!=sx.SNAPSHOT]
            s=d.snapshot();assert s.transport==wire and s.transport_flags==1
            d.samples[45]=3000;d.run(250)
            assert d.held(4)==(target==6) and d.radio_held(4)==(target!=6)
            d.samples[45]=3900;d.run(250)
            assert not d.radio_held(4) and not d.held(4)
        assert {p[2] for p in d.radio_packets if p[0]==0x93}=={0,1,2,5,6}
        d.peer_state=0;d.chord(1);d.run(250)
        assert d.call('m1_live_transport')==0 and not d.call('m1_wireless_ready')
        d.samples[45]=3000;d.run(250);assert not d.radio_held(4)
        d.peer_state=3;d.run(1000)
        assert d.call('m1_wireless_ready') and not d.radio_held(4) # no offline-held replay
        d.samples[45]=3900;d.run(250);d.samples[45]=3000;d.run(250)
        assert d.radio_held(4)
        d.samples[45]=3900;d.run(250)
        d.chord(5);d.run(250);assert d.call('m1_live_transport')==6
        d.peer_state=0;d.chord(2);d.run(250)
        assert d.call('m1_live_transport')==1 and not d.call('m1_wireless_ready')
        d.call('m1_test_usb_event',2);d.chord(5);d.run(250)
        assert d.call('m1_live_transport')==1 and not d.call('m1_live_transport_fault')
        d.call('m1_test_usb_event',3);d.chord(5);d.run(250)
        assert d.call('m1_live_transport')==6 # unpaired mode can return to USB
        print(f'PASS M1 {"HS" if high else "FS"} runtime Fn transport owner: all slots/USB, routing, telemetry, unpaired escape and no held-key replay')


def runtime_pairing(path):
    if not wireless_supported(path):
        print('SKIP runtime pairing: this build has no Bluetooth/2.4 GHz')
        return
    for start,key,target,wire in ((6,1,0,2),(0,1,0,2),(1,3,2,4),(5,4,5,5)):
        d=Live(path,True,mode=start,transports='runtime');d.run(200)
        d.samples[77]=d.samples[key]=3000;d.tick()
        for _ in range(D['M1_PAIR_HOLD_MS']+1):d.tick(step=1000)
        assert not any(p[0]==0x94 for p in d.radio_packets) # preview only
        d.samples[77]=d.samples[key]=3900;d.run(400)
        assert not d.call('m1_live_transport_fault')
        assert d.call('m1_live_transport')==target
        commands=[p for p in d.radio_packets if p[0]==0x94]
        assert len(commands)==1 and commands[0][2]==(0 if target==5 else 2)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        s=d.snapshot();assert s.transport==wire and s.transport_flags==4
        d.samples[45]=3000;d.run(200);assert not d.radio_held(4)
        d.peer_state=3;d.run(1000)
        assert d.call('m1_wireless_ready') and not d.radio_held(4)
        d.samples[45]=3900;d.run(200);d.samples[45]=3000;d.run(200)
        assert d.radio_held(4)
        d.samples[45]=3900;d.run(200)
        assert len([p for p in d.radio_packets if p[0]==0x94])==1
        assert not d.call('m1_live_transport_fault')
    print('PASS M1 runtime pairing: long-hold/release, same-slot success, single command, GUI state and neutral rearm')


def runtime_reconnect(path):
    if not wireless_supported(path):
        print('SKIP runtime reconnect: this build has no Bluetooth/2.4 GHz')
        return
    for mode,state,wire in ((0,0,2),(1,1,3),(2,2,4),(5,4,5)):
        d=Live(path,True,mode=mode,transports='runtime');d.run(300)
        d.send(sx.HELLO);d.wait(sx.READY);d.command('stream gui')
        d.put(GPIO+0x810,0x800);d.call('m1_encoder_start');d.run(20)
        d.samples[45]=d.samples[46]=d.samples[72]=3000;d.run(250)
        assert d.radio_held(4) and d.radio_held(22) and d.radio_modifiers==1
        d.peer_state=state;d.peer_pending=True;d.put(GPIO+0xc10,0);d.run(300)
        assert d.call('m1_wireless_healthy') and not d.call('m1_live_transport_fault')
        assert not d.call('m1_wireless_ready') and not d.call('m1_live_scan_losses')
        d.messages.clear();s=d.snapshot()
        assert s.transport==wire and s.transport_flags==(4 if state==4 else 0)
        assert not s.flags&2 and s.raw[45]<s.press[45]
        # Offline knob motion must not become a volume pulse on reconnect.
        for phase in (1,3,2,0):
            d.put(GPIO+0x810,((phase&1)<<10)|((phase>>1)<<12)|0x800)
            for _ in range(D['ENCODER_PHASE_STABLE_SAMPLES']):d.call('m1_encoder_irq');d.tick()
        d.radio_packets.clear();d.radio_consumer.clear();d.run(300)
        assert not any(p[0]==0x81 for p in d.radio_packets)
        d.peer_state=3;d.run(1000)
        assert d.call('m1_wireless_ready') and not d.call('m1_live_transport_fault')
        assert not d.radio_held(4) and not d.radio_held(22) and not d.radio_modifiers
        assert d.radio_consumer and all(v==0 for v in d.radio_consumer)
        assert not any(p[0] in (0x93,0x94) for p in d.radio_packets) # no reselect/re-pair
        d.messages.clear();s=d.snapshot();assert s.transport_flags==1 and not s.flags&2
        d.samples[45]=d.samples[46]=d.samples[72]=3900;d.run(200)
        d.samples[45]=3000;d.run(200);assert d.radio_held(4)
        d.samples[45]=3900;d.run(200);assert not d.radio_held(4)
        # Another disconnect is recoverable; the user can still choose USB.
        d.peer_state=1;d.run(1000);assert not d.call('m1_wireless_ready')
        d.chord(56);assert not d.snapshot().performance_mode
        d.chord(5);d.run(200)
        assert d.call('m1_live_transport')==6 and not d.call('m1_live_transport_fault')
        d.samples[45]=3000;d.run(200);assert d.held(4)
    print('PASS M1 live reconnect: all wireless modes, GUI status, held-key/modifier/knob suppression, neutral rearm and USB escape')


if __name__=='__main__':main()
