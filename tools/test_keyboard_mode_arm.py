#!/usr/bin/env python3
"""Execute actual ARM keyboard/SysEx/SDK paths, with synthetic ASIC and LED replies."""
import argparse
from keyboard_gui_model import Decoder
from test_keyboard_gui import packet
from test_midi_control_arm import MidiControlArm
from test_lighting_arm import LightingArm
from test_scan_stream_arm import drain, key_push
from keyboard_capture import press_velocity, KeyDecoder
from keyboard_labels import sensor_labels
from lighting_reference_tables import recover


def keyboard_mapping_tests(args):
    dev = LightingArm(args.elf,args.reference)
    dev.service(400)
    labels = sensor_labels()[61]
    shortcuts = {'Esc':0x35, **dict(zip(('1','2','3','4','5','6','7','8','9','0','-','='),range(0x3a,0x46))),
                 'BkS':0x4c,'Y':0x49,'P':0x46,'N':0x4d,'M':0x4e,'H':0x4a,'J':0x4b}
    def set_keys(**keys):
        for label,value in keys.items(): dev.raw[labels.index(label)] = value
        dev.service(10)
    def usages():
        report=dev.reports[-1]
        assert report[0]==0, report
        return {u for u in range(4,116) if report[2+(u-4)//8] & (1<<((u-4)%8))}
    set_keys(RAl=500,Mnu=500,RCt=500,RSh=500)
    assert usages()=={0x50,0x51,0x4f,0x52}, usages()
    set_keys(RAl=3900,Mnu=3900,RCt=3900,RSh=3900)
    assert not usages()
    maps,_=recover(args.reference)
    for label,usage in shortcuts.items():
        for release_fn_first in (False,True):
            set_keys(**{'Fn':500,label:500})
            assert usages()=={usage}, (label,usages())
            set_keys(**{'Fn' if release_fn_first else label:3900})
            assert not usages(), (label,usages())
            set_keys(**{'Fn':3900,label:3900})
    # Left Shift stays a normal modifier while Fn is held, so Fn+Shift+Esc is
    # a tilde: Shift + grave (0x35) when Fn precedes Esc, or Shift + the base
    # grave (0x29) when Esc was already held before Fn.
    for order,expected in ((('LSh','Fn','Esc'),0x35),(('Fn','LSh','Esc'),0x35),
                           (('LSh','Esc','Fn'),0x29),(('Fn','Esc','LSh'),0x35)):
        set_keys(LSh=3900,Fn=3900,Esc=3900)
        assert not usages()
        for label in order: set_keys(**{label:500})
        report=dev.reports[-1]
        assert report[0]==0x02, (order,bytes(report))
        pressed={u for u in range(4,116) if report[2+(u-4)//8] & (1<<((u-4)%8))}
        assert pressed=={expected}, (order,pressed)
        set_keys(LSh=3900,Fn=3900,Esc=3900)
        assert not usages(), order
    set_keys(Fn=500); dev.service(60)
    frame=next(p[2:] for _,p in reversed(dev.transactions) if len(p)==194)
    for label in shortcuts:
        c,r,g,b=maps[0][labels.index(label)]
        assert tuple(frame[c*192+i] for i in (r,g,b))==(0,255,0),label
    for _ in range(3):
        set_keys(Y=500); assert usages()=={0x49}
        set_keys(Y=3900); assert not usages()
    set_keys(Fn=3900)
    assert not dev.reset_requests and not dev.midi_packets
    print('PASS ARM keyboard: four NKRO arrows without modifiers, all 20 Fn shortcuts, both release orders, Fn-held repeats, green I2C hints')


def lower_row_tests(args):
    dev=LightingArm(args.elf,args.reference); dev.service(400)
    labels=sensor_labels()[61]; maps,_=recover(args.reference)
    def keys(**values):
        for label,value in values.items(): dev.raw[labels.index(label)]=value
        dev.service(20)
    def led(label):
        frame=next(p[2:] for _,p in reversed(dev.transactions) if len(p)==194)
        c,r,g,b=maps[0][labels.index(label)]
        return tuple(frame[c*192+i] for i in (r,g,b))
    def muted(value):
        dev.command('stream off'); dev.service(20)
        assert f'lower_muted={value}'.encode() in dev.command('menu status')
        snapshot(dev,'stream gui')
    keys(Fn=2400,LSh=2400); dev.service(60)
    assert led('LSh')==(0,0,0) # no new keyboard-mode setting
    keys(Fn=3900,LSh=3900)
    keys(Fn=2400,Ent=2400); keys(Fn=3900,Ent=3900); dev.service(200)
    initial=snapshot(dev,'stream gui'); assert initial.performance_mode==1
    keys(Fn=2400); dev.service(60); assert led('LSh')==(255,255,255)
    keys(LSh=2400); muted(0) # held preview has not toggled
    dev.midi_packets.clear()
    keys(LSh=3900); keys(Fn=3900); dev.service(200); muted(1)
    lower=('Cap','A','S','D','F','G','H','J','K','L',';','\'','LSh','Z','X','C','V','B','N','M',',','.','/','RSh')
    for label in lower: assert led(label)==(0,0,0),(label,led(label))
    for label in ('Ent','LCt','LGu','LAl','RAl','RCt','Spc'): assert led(label)==(0,0,255)
    dev.midi_packets.clear()
    keys(Spc=3499); dev.service(20)
    assert bytes([11,0xb0,64,127]) in dev.midi_packets
    keys(Spc=3600); dev.service(20)
    assert bytes([11,0xb0,64,0]) not in dev.midi_packets
    keys(Spc=3601); dev.service(20)
    assert bytes([11,0xb0,64,0]) in dev.midi_packets
    keys(Spc=3900)
    assert snapshot(dev,f'cfg midi 947 {labels.index("Spc")} 60').result==2
    for label in ('Tab','Q','W','E','R','T','Y','U','I','O','P','[',']','\\'): assert led(label)==(255,)*3
    dev.midi_packets.clear()
    keys(**{label:1000 for label in lower}); dev.service(40)
    assert not any(p[1] in (0x90,0xa0) for p in dev.midi_packets)
    keys(Tab=1000); dev.service(40)
    assert any(p[:3]==bytes([9,0x90,72]) for p in dev.midi_packets)
    keys(Tab=3900,**{label:3900 for label in lower}); dev.service(30)
    assert bytes([8,0x80,72,0]) in dev.midi_packets
    # Mapping a muted key remains possible but cannot bypass the row gate.
    index=labels.index('A'); assert snapshot(dev,f'cfg midi 955 {index} 72').result==1
    dev.service(200); dev.midi_packets.clear(); keys(A=1000); dev.service(30)
    assert not any(p[1]==0x90 for p in dev.midi_packets)
    keys(A=3900); keys(Fn=2400,LSh=2400); muted(1)
    keys(Fn=3900); keys(LSh=3900); dev.service(200); muted(0)
    assert led('A')==(255,)*3
    dev.midi_packets.clear(); keys(A=1000); dev.service(30)
    assert any(p[:3]==bytes([9,0x90,72]) for p in dev.midi_packets)
    keys(A=3900); dev.service(30)
    assert bytes([8,0x80,72,0]) in dev.midi_packets
    result=snapshot(dev); assert result.midi_mapping[index]==72
    assert result.press==initial.press and result.release==initial.release
    assert not result.midi_errors and not dev.reset_requests
    print('PASS ARM Fn+Left Shift: MIDI-only white hint, release-only mute/unmute, Caps/Shift row gate and dark LEDs, top rows/control hints intact, custom mappings preserved; Space blue, CC64 Schmitt on/off and reserved mapping')


def music_tests(args):
    dev=LightingArm(args.elf,args.reference); dev.service(400)
    labels=sensor_labels()[61]; maps,_=recover(args.reference)
    def keys(**values):
        for label,value in values.items(): dev.raw[labels.index(label)]=value
        dev.service(20)
    def status(**fields):
        dev.command('stream off'); dev.service(20)
        reply=dev.command('menu status')
        for name,value in fields.items(): assert f'{name}={value}'.encode() in reply,reply
        snapshot(dev,'stream gui')
    def leds():
        dev.service(60)
        frame=next(p[2:] for _,p in reversed(dev.transactions) if len(p)==194)
        return {label:tuple(frame[c*192+i] for i in (r,g,b)) for label,(c,r,g,b) in zip(labels,maps[0])}
    def page(key):
        keys(**{'Fn':2400,key:2400}); keys(**{'Fn':3900,key:3900}); dev.service(200)
    keys(Fn=2400,Ent=2400); keys(Fn=3900,Ent=3900); dev.service(200)
    initial=snapshot(dev,'stream gui')
    assert initial.press==(3500,)*61 and initial.release==(3600,)*61
    keys(Fn=2400); colors=leds()
    for label in ('E','S','LSh'): assert colors[label]==(255,)*3
    keys(Fn=3900)
    page('S'); status(music_page=10,root=0,scale=9)
    colors=leds()
    for label in 'JIDHYMLPOT': assert colors[label]==((0,255,0) if label=='T' else (77,)*3)
    assert colors['Esc']==(255,0,0) and colors['A']==(0,0,0)
    dev.midi_packets.clear(); keys(J=2400); status(scale=9)
    assert not any(p[1]==0x90 for p in dev.midi_packets)
    keys(J=3900); dev.service(200); status(scale=0,music_page=0)
    colors=leds(); assert colors['1']==(0,0,0) and colors['Tab']==(255,)*3
    # C# is currently filtered, but root selection must still offer it.
    page('E'); colors=leds()
    assert colors['1']==(77,)*3 and colors['Tab']==(0,255,0)
    keys(**{'1':2400}); status(root=0); keys(**{'1':3900}); dev.service(200)
    status(root=1,scale=0,music_page=0)
    colors=leds(); assert colors['1']==(255,)*3 and colors['Q']==(0,0,0)
    dev.midi_packets.clear(); keys(Q=1000,**{'1':1000}); dev.service(30)
    assert any(p[:3]==bytes([9,0x90,73]) for p in dev.midi_packets)
    assert not any(p[:3]==bytes([9,0x90,74]) for p in dev.midi_packets)
    keys(Q=3900,**{'1':3900}); dev.service(30)
    assert bytes([8,0x80,73,0]) in dev.midi_packets
    # H/P select distinct scales; Escape must not commit the held preview.
    for label,index in (('H',3),('P',7),('T',9)):
        page('S'); keys(**{label:2400}); keys(**{label:3900}); dev.service(200)
        status(scale=index,root=1)
    page('S'); keys(I=2400); keys(Esc=2400); keys(I=3900,Esc=3900); dev.service(200)
    status(scale=9,music_page=0)
    result=snapshot(dev)
    assert result.midi_mapping==initial.midi_mapping and result.press==initial.press
    assert not result.midi_errors and not dev.reset_requests
    print('PASS ARM root/scale menus: hints, release commit, C#/major packet+LED filter, selectors bypass filter, H/P/T, Escape and mapping preservation')


def midi_tests(args):
    dev = LightingArm(args.elf,args.reference)
    dev.service(400)
    labels = sensor_labels()[61]
    s = snapshot(dev,'stream gui')
    expected = {'Tab':72,'Q':74,'W':76,'E':77,'R':79,'T':81,'Y':83,'U':84,
                'I':86,'O':88,'P':89,'[':91,']':93,'\\':95,'1':73,'2':75,
                '4':78,'5':80,'6':82,'8':85,'9':87,'-':90,'=':92,'BkS':94,
                'LSh':60,'A':61,'Z':62,'S':63,'X':64,'C':65,'F':66,'V':67,
                'G':68,'B':69,'H':70,'N':71,'M':72,'K':73,',':74,'L':75,
                '.':76,'/':77,"'":78}
    assert s.performance_mode == 0
    assert s.press==(3500,)*61 and s.release==(3600,)*61
    # Explicit pairs retain these velocity/short-strike waveform fixtures.
    assert snapshot(dev,'cfg all 940 3600 3700').result==1
    dev.service(200)
    assert s.midi_mapping == tuple(expected.get(label,255) for label in labels)
    def set_keys(**keys):
        for label,value in keys.items(): dev.raw[labels.index(label)] = value
        dev.service(10)
    set_keys(Fn=3500,Ent=3500)
    s=snapshot(dev); assert s.performance_mode == 0 and s.mode_changes == 0
    dev.service(200); assert snapshot(dev).mode_changes == 0
    set_keys(Fn=3900,Ent=3900); dev.service(200) # cleanup starts on release
    assert snapshot(dev).performance_mode == 1
    maps,_ = recover(args.reference)
    def led(label):
        frame=next(p[2:] for _,p in reversed(dev.transactions) if len(p)==194)
        c,r,g,b=maps[0][labels.index(label)]
        return tuple(frame[c*192+i] for i in (r,g,b))
    for label in labels:
        assert led(label)==((0,0,255) if label in ('Ent','LCt','LGu','LAl','RAl','RCt','Spc') else (255,)*3 if label in expected else (0,)*3),(label,led(label))
    set_keys(Fn=3500,K=3500); set_keys(K=3900,Fn=3900); dev.service(200)
    for label in ('Ent','LCt','LGu','LAl','RAl','RCt','Spc'): assert led(label)==(0,0,224)
    assert led('Tab')==(224,224,224)
    set_keys(Fn=3500,L=3500); set_keys(L=3900,Fn=3900); dev.service(200)
    assert led('Ent')==(0,0,255) and led('Tab')==(255,255,255)
    spare=labels.index('Mnu')
    assert snapshot(dev,f'cfg midi 948 {spare} 60').result==1
    dev.service(70); assert led('Mnu')==(255,)*3
    assert snapshot(dev,f'cfg midi 949 {spare} 255').result==1
    dev.service(200); assert led('Mnu')==(0,)*3
    dev.midi_packets.clear(); dev.reports.clear()
    values = dev.raw.copy(); values[labels.index('Tab')] = 3500
    one_raw_frame(dev,values)
    for v in (3400,3300,3200,3100,3000,2900,2800,2700,2600):
        values[labels.index('Tab')] = v; one_raw_frame(dev,values)
    dev.service(40)
    assert bytes([9,0x90,72,23]) in dev.midi_packets, dev.midi_packets
    assert any(p[:3] == bytes([10,0xa0,72]) for p in dev.midi_packets)
    assert all(not any(p) for p in dev.reports), dev.reports
    # The lower row's Shift key emits C4; its fall crosses the bottom-out
    # threshold after two intervals, so the fit closes on three samples.
    values=dev.raw.copy(); values[labels.index('LSh')]=3500
    one_raw_frame(dev,values)
    for v in (3400,3300,1400,1200,1100):
        values[labels.index('LSh')]=v; one_raw_frame(dev,values)
    dev.service(30)
    assert bytes([9,0x90,60,23]) in dev.midi_packets
    set_keys(LSh=3900)
    assert bytes([8,0x80,60,0]) in dev.midi_packets
    set_keys(RCt=3500); dev.service(40)
    assert snapshot(dev).octave == 1
    # Exercise the compiled overlay with actual recovered control-key routing.
    maps,_ = recover(args.reference)
    _,r,g,b = maps[0][labels.index('RCt')]
    _,cr,cg,cb = maps[0][labels.index('RAl')]
    for t,color in ((600000,(0,0,255)),(600600,(0,0,0))):
        dev.cpu.mem_write(0x2003d000,bytes([7])*204)
        dev.call('keyboard_midi_lights',dev.symbols['s_midi'],0x2003d000,t)
        rgb=bytes(dev.cpu.mem_read(0x2003d000,204))
        assert (rgb[r],rgb[g],rgb[b]) == color
        assert (rgb[cr],rgb[cg],rgb[cb]) == (0,0,255) # idle control remains visible
    set_keys(RCt=3900,Tab=3900)
    assert bytes([8,0x80,72,0]) in dev.midi_packets
    dev.midi_packets.clear()
    set_keys(LGu=1000,LAl=1000); dev.service(20)
    assert bytes([11,0xb0,1,127]) in dev.midi_packets
    assert bytes([14,0xe0,127,127]) in dev.midi_packets
    set_keys(LCt=1000); dev.service(20)
    assert bytes([14,0xe0,0,64]) in dev.midi_packets
    set_keys(LAl=3800); dev.service(20)
    assert bytes([14,0xe0,0,0]) in dev.midi_packets
    set_keys(LCt=3800,LGu=3800); dev.service(20)
    assert bytes([11,0xb0,1,0]) in dev.midi_packets
    assert not any(p[1]==0x90 for p in dev.midi_packets)
    assert snapshot(dev).octave==1
    set_keys(LCt=3900,LAl=3900,LGu=3900)
    s=snapshot(dev,'cfg midi 950 32 60'); assert s.result == 1 and s.midi_mapping[32] == 60
    dev.service(180)
    for cmd in ('cfg midi 951 32 128','cfg midi 951 99 60',f'cfg midi 951 {labels.index("Fn")} 60',
                'cfg midi 951 32 60 junk','cfg midi 951 32'):
        s=snapshot(dev,cmd); assert s.result == 2 and s.midi_mapping[32] == 60
    set_keys(A=3500); dev.service(30)
    assert any(p[:3] == bytes([9,0x90,72]) for p in dev.midi_packets)
    dev.call('keyboard_live_usb_reset'); dev.service(180)
    dev.command('stream gui')
    assert bytes([8,0x80,72,0]) in dev.midi_packets
    assert bytes([11,0xb0,120,0]) in dev.midi_packets
    set_keys(A=3900); dev.service(10)
    set_keys(Fn=3500,Ent=3500); dev.service(180)
    assert snapshot(dev).performance_mode == 1
    set_keys(Fn=3900,Ent=3900); assert snapshot(dev).performance_mode == 0
    dev.service(70); assert led('Ent')==(0,255,0) and led('Tab')==(255,255,255)
    set_keys(A=3500)
    assert a(dev)
    print('PASS ARM USB MIDI: defaults, chord/hold, Note On velocity=23, poly aftertouch, octave-latched Off, GUI map ACK/reject, reset cleanup, HID isolation/recovery')


def snapshot(dev,command=None):
    dev.output.clear()
    if command: dev.command(command)
    dev.service(50)
    values = list(Decoder().feed(dev.output))
    assert values, bytes(dev.output)
    return values[-1]


def a(dev): return bool(dev.reports[-1][2] & 1)


def one_raw_frame(dev,values):
    dev.raw = list(values)
    before = sum(r[0] == 0xa0 for r in dev.requests)
    for _ in range(10):
        dev.service(1)
        if sum(r[0] == 0xa0 for r in dev.requests) > before: return
    raise AssertionError('Synthetic ASIC did not deliver a frame')


def velocity_tests(args):
    dev = LightingArm(args.elf,args.reference,3)  # exercise every supported sensor
    dev.service(400)
    snapshot(dev,'stream gui')
    s = snapshot(dev,'cfg enable 301 0')
    assert len(s.velocity) == 65 and not s.flags & 1
    assert s.press==(3500,)*65 and s.release==(3600,)*65
    assert snapshot(dev,'cfg all 300 3600 3700').result==1
    one_raw_frame(dev,[3500]*65)  # trigger: every window starts at [3500]
    for j in range(1,10): one_raw_frame(dev,[3500-(i+1)*j for i in range(65)])
    s = snapshot(dev)
    assert s.captures == (1,)*65, s.captures  # ten samples (trigger + nine) close the window
    assert all(abs(v - 8000*(i+1)/4500000) < 1e-7 for i,v in enumerate(s.velocity)), s.velocity
    assert s.velocity_state == (2,)*65
    assert all(not any(r) for r in dev.reports)
    dev.raw[0] = 3700; s = snapshot(dev); assert not s.velocity_state[0] & 1
    dev.raw[0] = 3701; s = snapshot(dev); assert s.velocity_state[0] & 1

    # Bottom-out cut: a very fast press fits on four samples (no median
    # filter), and the below-1500 sample that closes it is excluded.
    baseline = snapshot(dev).captures[0]  # raw[0] = 3701: release rearm only
    values = dev.raw.copy(); values[0] = 3500; one_raw_frame(dev,values)  # trigger
    for value in (3200,2900,2600,1400):
        values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
    s = snapshot(dev)
    assert s.captures[0] == baseline+1 and abs(s.velocity[0] - 2400000/4500000) < 1e-7, (s.velocity[0],s.captures[0])

    # Rapid retriggers: the newest press owns the window; the completed fit
    # uses only the final press's readbacks.
    values = dev.raw.copy(); values[0] = 3900; one_raw_frame(dev,values)  # release rearms
    rapid = [3500,3800,3490,3810,3480,3400,3390,3380,3370,3360]
    for value in rapid:
        values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
    for _ in range(3): one_raw_frame(dev,dev.raw)
    values = dev.raw.copy(); values[0] = 3360; one_raw_frame(dev,values)  # tenth sample
    s = snapshot(dev)
    final_window = [3480,3400,3390,3380,3370,3360,3360,3360,3360,3360]
    assert s.captures[0] == baseline+2 and s.captures[1:] == (1,)*64, s.captures
    from firmware_defaults import DEFAULTS
    assert abs(s.velocity[0] - max(0,min(1,press_velocity(final_window,DEFAULTS['HUNTSMAN_ASSUMED_SCAN_HZ'])/4500000))) < 1e-7

    fixtures = [
        ([3510,3520,3530,3540,3550,3560,3570,3580,3590], False, -80000),   # rising -> 0
        ([3500]*9, False, 0),                                             # flat
        ([3490,3480,3470,3460,3450,3440,3430,3420,3410], False, 80000),   # filtered slow fall
        ([3200,2900,2601], True, 899/3*8000),    # four samples, no filter, fractional
        ([2938], True, 562*8000),                # just below 4500000
        ([2937], True, 563*8000),                # just above 4500000
        ([2500], True, 1000*8000),               # far above 4500000
        ([2900,2890,2880,2870,2860,2850,2840,2830,2820], False, 80000),   # ten-sample filtered fall
    ]
    for points,bottom,raw_speed in fixtures:
        values = dev.raw.copy(); values[0] = 3900; one_raw_frame(dev,values)
        baseline = snapshot(dev).captures[0]
        values = dev.raw.copy(); values[0] = 3500; one_raw_frame(dev,values)  # trigger
        for value in points:
            values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
        if bottom:
            values = dev.raw.copy(); values[0] = 1400; one_raw_frame(dev,values)
        s = snapshot(dev)
        expected = max(0,min(1,raw_speed/4500000))
        assert type(s.velocity[0]) is float and abs(s.velocity[0]-expected) < 1e-7, (points,s.velocity[0],expected,s.captures[0],baseline)
        assert s.captures[0] == baseline+1
    # Long windows filter one glitch interval at every position; five-sample
    # windows skip the filter entirely (exact d(x)/count); ties discard the
    # earliest interval.
    for outlier in range(9):
        for spike in (-500,500):
            values = dev.raw.copy(); values[0] = 3900; one_raw_frame(dev,values)
            values = dev.raw.copy(); values[0] = 3500; one_raw_frame(dev,values)
            value = 3500
            for j in range(9):
                value -= spike if j == outlier else 10
                values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
            s = snapshot(dev)
            assert abs(s.velocity[0] - 80000/4500000) < 1e-7
    for spike in (-100,100):  # small glitches stay below release: no retrigger at the closer
        values = dev.raw.copy(); values[0] = 3900; one_raw_frame(dev,values)
        values = dev.raw.copy(); values[0] = 3500; one_raw_frame(dev,values)
        value = 3500
        for j in range(4):
            value -= spike if j == 2 else 10
            values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
        values = dev.raw.copy(); values[0] = 1400; one_raw_frame(dev,values)  # bottom-out closes the five-sample window
        s = snapshot(dev)
        assert abs(s.velocity[0] - max(0,min(1,(30+spike)/4*8000/4500000))) < 1e-7
    for expected,samples in ((90000,[3500,3500,3490,3480,3470,3460,3450,3440,3430,3410]),
                             (70000,[3500,3480,3470,3460,3450,3440,3430,3420,3410,3410])):
        values = dev.raw.copy(); values[0] = 3900; one_raw_frame(dev,values)
        for value in samples:
            values = dev.raw.copy(); values[0] = value; one_raw_frame(dev,values)
        s = snapshot(dev)
        assert abs(s.velocity[0] - expected/4500000) < 1e-7
    print('PASS ARM float32 velocity: bottom-out windows, retrigger ownership, clamps, glitch filter gating and tie handling')
    s = snapshot(dev,'cfg all 302 3100 3300')
    assert (s.ack,s.result,s.revision) == (302,1,2)
    assert s.press == (3100,)*65 and s.release == (3300,)*65
    assert not any(v & 6 for v in s.velocity_state)
    for bad in ('cfg all 303 3300 3100','cfg all 303 3000 4096',
                'cfg all 303 3000 3300 junk','cfg all 303 0 3300'):
        s = snapshot(dev,bad)
        assert (s.ack,s.result,s.revision) == (303,2,2)
        assert s.press == (3100,)*65 and s.release == (3300,)*65
    print('PASS ARM DMA -> 65 independent velocity fits, bottom-out windows, retrigger ownership, HID disabled, atomic all-key command/readback')


def janko_tests(args):
    dev = LightingArm(args.elf,args.reference); dev.service(400)
    labels = sensor_labels()[61]

    def keys(**values):
        for label,value in values.items(): dev.raw[labels.index(label)]=value
        dev.service(20)

    def strike(label,note):
        index = labels.index(label)
        dev.midi_packets.clear()
        values = dev.raw.copy(); values[index] = 3400; one_raw_frame(dev,values)
        for value in (3300,3200,3100,3000):
            values = dev.raw.copy(); values[index] = value; one_raw_frame(dev,values)
        values = dev.raw.copy(); values[index] = 1400; one_raw_frame(dev,values)  # bottom-out fires the note
        dev.service(40)
        assert bytes([9,0x90,note,23]) in dev.midi_packets, (label,dev.midi_packets)
        values = dev.raw.copy(); values[index] = 3900; one_raw_frame(dev,values)
        dev.service(20); dev.midi_packets.clear()

    snapshot(dev,'stream gui')
    keys(Fn=2400,Ent=2400); keys(Fn=3900,Ent=3900); dev.service(200)
    assert snapshot(dev).performance_mode == 1  # MIDI mode
    # Fn+J toggles the built-in Jankó layout on release.
    keys(Fn=2400,J=2400); keys(Fn=3900,J=3900); dev.service(200)
    assert snapshot(dev).flags & 64, snapshot(dev).flags
    dev.command('stream off'); dev.service(20)
    assert b'janko=1' in dev.command('menu status')
    snapshot(dev,'stream gui')
    for label,note in (('Esc',58),('1',60),('BkS',84),('Tab',59),('Q',61),('Y',71),
                       (']',83),('\\',85),('Cap',60),('Ent',84),('LSh',61),('B',71),('RSh',83)):
        strike(label,note)
    # The lower row stays enabled in Jankó mode: Fn+Left Shift is ineffective.
    keys(Fn=2400,LSh=2400); keys(Fn=3900,LSh=3900); dev.service(200)
    dev.command('stream off'); dev.service(20)
    assert b'lower_muted=1' in dev.command('menu status')
    snapshot(dev,'stream gui')
    strike('Z',63)
    keys(Fn=2400,LSh=2400); keys(Fn=3900,LSh=3900); dev.service(200)
    # Leaving the layout restores the configured mapping (Q is D5 again).
    keys(Fn=2400,J=2400); keys(Fn=3900,J=3900); dev.service(200)
    assert not snapshot(dev).flags & 64
    strike('Q',74)
    print('PASS ARM Jankó mode: Fn+J toggle, staggered notes on MIDI packets, telemetry bit, lower-row bypass, mapping restored')


def velocity_start_tests(args):
    dev = LightingArm(args.elf,args.reference); dev.service(400)
    labels = sensor_labels()[61]

    def keys(**values):
        for label,value in values.items(): dev.raw[labels.index(label)]=value
        dev.service(20)

    def release_all():
        for index in range(len(labels)): dev.raw[index]=3900
        dev.service(60)

    def status():
        dev.command('stream off'); dev.service(20)
        reply = dev.command('menu status')
        snapshot(dev,'stream gui')
        return reply

    def page(label):
        keys(**{'Fn':2400,label:2400}); keys(**{'Fn':3900,label:3900}); dev.service(200)

    def strike(label):
        index = labels.index(label)
        dev.midi_packets.clear()
        values = dev.raw.copy(); values[index] = 3400; one_raw_frame(dev,values)
        for value in (3300,3200,3100,3000):
            values = dev.raw.copy(); values[index] = value; one_raw_frame(dev,values)
        values = dev.raw.copy(); values[index] = 1400; one_raw_frame(dev,values)
        dev.service(40)
        velocity = next((p[3] for p in dev.midi_packets if p[0] == 9 and p[1] == 0x90), 0)
        values = dev.raw.copy(); values[index] = 3900; one_raw_frame(dev,values)
        dev.service(20); dev.midi_packets.clear()
        return velocity

    # The build identity is a SysEx reply, not a telemetry field: text and
    # telemetry uses separate SysEx types; this audit switches to log-only output.
    dev.command('stream off'); dev.service(20)
    assert b'build=v0.1.0-RZ03-0499' in dev.command('version')
    reply = dev.command('menu status')
    snapshot(dev,'stream gui')
    assert b'build=v0.1.0-RZ03-0499' in reply
    # Still in keyboard mode: the host sets the start anyway, because the GUI
    # cannot toggle MIDI mode (Fn+Enter does) and a discarded write must not
    # acknowledge success.
    assert snapshot(dev).velocity_start == 1
    dev.command('stream off'); dev.service(20)
    dev.command('cfg velocity 940 4')
    snapshot(dev,'stream gui')
    assert snapshot(dev).velocity_start == 4
    dev.command('stream off'); dev.service(20)
    dev.command('cfg velocity 941 1')
    snapshot(dev,'stream gui')
    assert snapshot(dev).velocity_start == 1
    keys(Fn=2400,Ent=2400); keys(Fn=3900,Ent=3900); dev.service(200)
    assert b'velocity_start=1' in status()   # default: the measured velocity
    assert strike('Q') == 23
    # Telemetry reports the setting and the host command writes it with readback.
    assert snapshot(dev).velocity_start == 1
    dev.command('stream off'); dev.service(20)
    reply = dev.command('cfg velocity 950 5')
    snapshot(dev,'stream gui')
    assert snapshot(dev).velocity_start == 5, reply
    dev.command('stream off'); dev.service(20)
    assert b'ERR' not in dev.command('cfg velocity 951 0')      # rejected: out of range
    snapshot(dev,'stream gui')
    assert snapshot(dev).velocity_start == 5
    dev.command('stream off'); dev.service(20)
    dev.command('cfg velocity 952 1')
    snapshot(dev,'stream gui')
    assert snapshot(dev).velocity_start == 1
    # Fn+V opens the modal ten-step page: 1 is 0%, 0 is 100%.
    page('V')
    assert strike('Q') == 0                 # the page consumes playing keys
    keys(**{'0':2400}); keys(**{'0':3900}); dev.service(200)
    assert b'velocity_start=10' in status()
    assert strike('Q') == 0                 # still inside the page
    page('Esc')                             # Escape leaves the page
    release_all()
    assert strike('Q') == 127               # always full velocity
    page('V'); keys(**{'5':2400}); keys(**{'5':3900}); dev.service(200)
    assert b'velocity_start=5' in status()
    page('Esc'); release_all()
    assert strike('Q') == 69                # 56 + round(71 * 0.17778)
    page('V'); keys(**{'1':2400}); keys(**{'1':3900}); dev.service(200)
    page('Esc'); release_all()
    assert strike('Q') == 23                # back to the measured value
    assert b'velocity_start=1' in status()
    print('PASS ARM velocity start: Fn+V modal page, ten-step digits, 0%/100% endpoints, floor mapping, Escape exit')


def midi_trigger_tests(args):
    dev = LightingArm(args.elf,args.reference); dev.service(400)
    labels = sensor_labels()[61]

    def keys(**values):
        for label,value in values.items(): dev.raw[labels.index(label)]=value
        dev.service(20)

    def release_all():
        for index in range(len(labels)): dev.raw[index]=3900
        dev.service(60)

    def press_threshold():
        snapshot(dev)  # fresh telemetry
        return snapshot(dev).press

    def strike(label):
        index = labels.index(label)
        dev.midi_packets.clear()
        values = dev.raw.copy(); values[index] = 3400; one_raw_frame(dev,values)
        for value in (3300,3200,3100,3000):
            values = dev.raw.copy(); values[index] = value; one_raw_frame(dev,values)
        values = dev.raw.copy(); values[index] = 1400; one_raw_frame(dev,values)
        dev.service(40)
        fired = any(p[0] == 9 and p[1] == 0x90 for p in dev.midi_packets)
        values = dev.raw.copy(); values[index] = 3900; one_raw_frame(dev,values)
        dev.service(20); dev.midi_packets.clear()
        return fired

    snapshot(dev,'stream gui')
    keys(Fn=2400,Ent=2400); keys(Fn=3900,Ent=3900); dev.service(200)
    assert press_threshold() == (3500,)*61        # default point in MIDI mode
    assert strike('Q')
    # Fn+Tab opens the raw trigger page; digits select the press threshold.
    keys(Fn=2400,Tab=2400); keys(Fn=3900,Tab=3900); dev.service(200)
    for level in (2,5,10,1):
        expected = 1500 + (level-1)*2099//9  # floor (level 1) up to release-1 (level 0)
        label = '0' if level == 10 else str(level)
        keys(**{label:500}); keys(**{label:3900}); dev.service(200)
        press = press_threshold()
        assert set(press) == {expected}, (level, expected, sorted(set(press))[:3])
        snapshot(dev)  # release thresholds are untouched below
        assert snapshot(dev).release == (3600,)*61
    # Still inside the page: select the deepest point (level 1), then leave.
    keys(**{'1':500}); keys(**{'1':3900}); dev.service(200)
    assert set(press_threshold()) == {1500}
    keys(Esc=500); keys(Esc=3900); dev.service(200)
    release_all()
    # A deep point still measures velocity: the trigger sits at the floor and
    # the single follow-up readback closes the fit with one interval.
    index = labels.index('Q')
    dev.midi_packets.clear()
    values = dev.raw.copy(); values[index] = 1400; one_raw_frame(dev,values)
    values = dev.raw.copy(); values[index] = 1200; one_raw_frame(dev,values)
    dev.service(40)
    assert any(p[0] == 9 and p[1] == 0x90 and p[3] > 1 for p in dev.midi_packets), dev.midi_packets
    values = dev.raw.copy(); values[index] = 3900; one_raw_frame(dev,values)
    dev.service(20)
    # A deep point needs firm presses for the chord too: 2400 is no longer
    # below the 1500 threshold.
    keys(Fn=500,Tab=500); keys(Fn=3900,Tab=3900); dev.service(200)
    keys(**{'0':500}); keys(**{'0':3900}); dev.service(200)  # shallowest: release - 1
    keys(Esc=500); keys(Esc=3900); dev.service(200)
    assert set(press_threshold()) == {3599}
    print('PASS ARM MIDI trigger: Fn+Tab page level 1 = bottom-out 1500 .. 0 = 3599, release preserved, deep point still fires with velocity')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf'); parser.add_argument('--reference',required=True)
    args = parser.parse_args()
    keyboard_mapping_tests(args)
    lower_row_tests(args)
    music_tests(args)
    midi_tests(args)
    velocity_tests(args)
    janko_tests(args)
    velocity_start_tests(args)
    midi_trigger_tests(args)
    dev = LightingArm(args.elf,args.reference)
    # No SysEx open: enumeration must be enough for scanning and keyboard output.
    dev.peer.send(11); dev.peer.drain(); dev.output.clear()
    dev.service(400)
    assert b'RAW armed' not in dev.output
    dev.raw[32] = 3500; dev.service(10); assert not a(dev)
    dev.raw[32] = 3499; dev.service(10); assert a(dev)
    for value in (3500,3550,3600,3501,3599):
        dev.raw[32] = value; dev.service(10); assert a(dev)
    dev.raw[32] = 3601; dev.service(10); assert not a(dev)
    dev.raw[32] = 3900; dev.service(10) # fully release before testing different GUI pairs
    assert dev.transactions, 'lighting did not run alongside NKRO'
    dev.peer.hello(); dev.peer.sequence = 0
    s = snapshot(dev,'stream gui')
    assert s.flags & 7 == 7 and s.count == 61 and not s.scan_errors and not s.light_errors
    dev.key(0x3b,True)  # FN
    dev.key(2,True)     # number 1 -> F1
    usage = 0x3a-4
    assert dev.reports[-1][2+usage//8] & (1 << (usage%8))
    s = snapshot(dev); assert s.flags & 32
    dev.key(2,False); dev.key(0x10,True)  # FN+Tab retained editor
    s = snapshot(dev); assert s.mode == 0 and not any(s.report) # preview only
    dev.key(0x10,False); dev.key(0x3b,False)
    s = snapshot(dev); assert s.mode == 1 and not s.flags & 32
    dev.key(0x6e,True); dev.key(0x6e,False)  # Escape exits
    s = snapshot(dev); assert s.mode == 0
    s = snapshot(dev,'cfg set 123 32 3000 3200')
    assert (s.ack,s.result,s.press[32],s.release[32],s.revision) == (123,1,3000,3200,1)
    for command in ('cfg set 124 32 3300 3200','cfg set 124 61 3000 3200',
                    'cfg set 124 32 0 3200','cfg set 124 32 3000 4097',
                    'cfg set 124 32 3000 3200 junk','cfg enable 124 2'):
        s = snapshot(dev,command)
        assert (s.ack,s.result,s.revision) == (124,2,1), command
    s = snapshot(dev,'cfg get 4294967295'); assert s.ack == 0xffffffff and s.result == 1
    s = snapshot(dev,'cfg get 4294967296'); assert s.ack == 0xffffffff
    dev.raw[32] = 3100; dev.service(10); assert not a(dev)
    dev.raw[32] = 2999; dev.service(10); assert a(dev)
    s = snapshot(dev); assert s.down[32] and s.report[2] & 1
    s = snapshot(dev,'cfg enable 150 0'); assert not s.flags & 3 and not a(dev)
    s = snapshot(dev,'cfg enable 151 1'); assert s.flags & 1 and not s.flags & 2 and not a(dev)
    dev.raw[32] = 3200; dev.service(10); assert not a(dev)
    dev.raw[32] = 3201; s = snapshot(dev); assert s.flags & 2
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    # Closing the GUI is not a keyboard kill switch.
    dev.peer.send(11); dev.peer.drain(); dev.output.clear(); dev.service(20); assert a(dev)
    dev.raw[32] = 3900; dev.service(10); assert not a(dev)
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    # Invalid sample releases immediately, held key cannot rearm.
    dev.raw[0] = 4097; dev.service(10); assert not a(dev)
    dev.raw[0] = 3900; dev.service(10); assert not a(dev)
    dev.raw[32] = 3900; dev.service(10)
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    # Reset + reconfigure between main loop services still requires neutral.
    dev.reset(True)
    dev.control_out(bytes.fromhex('00 05 07 00 00 00 00 00'))
    dev.control_out(bytes.fromhex('00 09 01 00 00 00 00 00'))
    dev.service(20); assert not a(dev)
    dev.raw[32] = 3900; dev.service(10)
    dev.raw[32] = 2900; dev.service(10); assert a(dev)
    dev.no_completion = True; dev.service(130); assert not a(dev)
    before = len(dev.requests); dev.service(100); assert len(dev.requests) == before
    assert not dev.reset_requests
    print('PASS ARM: auto NKRO without SysEx; Schmitt boundaries; GUI ACK/readback; disable/neutral guards; reset/invalid/timeout release; LED concurrency')

    for hs in (False,True):
        dev = MidiControlArm(args.elf,hs)
        dev.call('scan_stream_init'); dev.call('scan_stream_gui')
        def push(sequence):
            dev.cpu.mem_write(0x2003d000,packet(sequence=sequence))
            dev.call('scan_stream_gui_push',0x2003d000)
        push(0); dev.call('debug_service'); dev.call('debug_service')
        address,length = dev.packet(5); pending = bytes(dev.cpu.mem_read(address,length))
        for i in range(1,100): push(i)
        assert bytes(dev.cpu.mem_read(address,length)) == pending
        values = list(Decoder().feed(drain(dev, 5)))
        assert [v.sequence for v in values] == [0,99]
        push(100); dev.call('debug_service'); dev.call('debug_service')
        address,length = dev.packet(5); pending = bytes(dev.cpu.mem_read(address,length))
        dev.call('scan_stream_last_key',3600,77,5)
        key_push(dev,[3500]*61)
        assert bytes(dev.cpu.mem_read(address,length)) == pending
        assert list(KeyDecoder(3600,77).feed(drain(dev, 6))) == [3500]
        key_push(dev,[3400]*61); dev.call('debug_service'); dev.call('debug_service')
        dev.call('scan_stream_gui'); push(101)
        assert [v.sequence for v in Decoder().feed(drain(dev, 5))] == [101]
        print(f'PASS {"HS" if hs else "FS"} GUI: stable pending transfer, latest-only replacement, 1152-byte framing')


if __name__ == '__main__': main()
