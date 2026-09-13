#!/usr/bin/env python3
"""Fn menu through compiled scan/USB/I2C, plus original renderer comparisons."""
import argparse
from test_calibration_arm import Live, record, SLOTS
from test_keyboard_mode_arm import snapshot
from scan_bars import sensor_labels
from lighting_reference_tables import recover
from production_arm import ProductionArm


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf'); parser.add_argument('--reference',required=True)
    args=parser.parse_args()
    dev=Live(args.elf,args.reference,{SLOTS[0]:record(),SLOTS[1]:record(0)})
    dev.raw=[4000]*61
    dev.service(400)
    labels=sensor_labels()[61]; maps,_=recover(args.reference)
    def keys(**values):
        for label,value in values.items(): dev.raw[labels.index(label)]=value
        dev.service(20)
    def color(frame,label):
        c,r,g,b=maps[0][labels.index(label)]
        return tuple(frame[c*192+i] for i in (r,g,b))
    def draw():
        # A complete primary I2C upload, not a presumed C struct offset.
        dev.service(60)
        return next(p[2:] for _,p in reversed(dev.transactions) if len(p)==194)
    def word_cycle(word,hue=(255,255,255)):
        start=len(dev.transactions)
        dev.service((len(word)*200+500)+300)
        phases=[]
        for timestamp,packet in dev.transactions[start:]:
            if len(packet)!=194: continue
            frame=packet[2:]
            highlights=[label for label in set(word) if color(frame,label)==hue]
            assert len(highlights)<=1,highlights
            highlight=highlights[0] if highlights else None
            for label in labels:
                pwm=255 if label==highlight else 77 if label in word else 0
                expected_color=tuple((v*pwm+127)//255 for v in hue)
                assert color(frame,label)==expected_color,(word,label,color(frame,label))
            if not phases or phases[-1][0]!=highlight: phases.append((highlight,timestamp))
        expected=list(word)+[None]+list(word[:2])
        assert [p for p,_ in phases]==expected,(word,phases)
        for (phase,start_time),(_,end_time) in zip(phases[1:],phases[2:]):
            # Existing 40 ms scheduler quantizes LED edges, no word/letter FIFO.
            assert abs(end_time-start_time-(500 if phase is None else 200))<=40,phases
        return draw()
    s=snapshot(dev,'stream gui'); assert s.calibration_generation==1
    initial_pages={a:bytes(p) for a,p in dev.flash.pages.items()}
    keys(Fn=500)
    frame=draw()
    green=('Esc','1','2','3','4','5','6','7','8','9','0','-','=','BkS','Y','P','N','M','H','J')
    for label in labels:
        assert color(frame,label)==((0,0,255) if label=='Ent' else (255,255,255) if label in ('C','Tab','K','L','Cap','R') else (0,255,0) if label in green else (0,0,0)),label
    keys(K=500); keys(K=4000)
    assert color(draw(),'K')==(224,224,224)
    keys(K=500); keys(K=4000)
    assert color(draw(),'K')==(194,194,194)
    keys(L=500); keys(L=4000)
    assert color(draw(),'L')==(224,224,224)
    keys(L=500); keys(L=4000)
    assert color(draw(),'L')==(255,255,255)
    # Same-report Fn+Tab must not depend on ASIC sensor order.
    keys(Fn=4000); keys(Fn=500,Tab=500)
    assert snapshot(dev).mode==0
    keys(Fn=4000,Tab=4000)
    assert snapshot(dev).mode==1
    keys(**{'0':500}); keys(**{'0':4000})
    s=snapshot(dev); assert s.mode==1 and s.press==(3500,)*61
    assert not any(s.report)
    frame=draw()
    # Execute original renderer with only its unrelated background effect stubbed.
    ref=ProductionArm(args.reference); ref.stubs[0x200098b0]=lambda *a:0
    ref.write(0x04000901,[1]); ref.write(0x040008ec,[10]); ref.write(0x200290cd,[0])
    ref.call(0x2000a078)
    original=ref.read(0x2002b3a4,204)
    for label in '1234567890': assert color(frame,label)==color(original,label),label
    # Reject unrelated settings and mode/calibration changes inside the editor.
    assert snapshot(dev,'cfg all 702 3000 3200').result==2
    assert snapshot(dev,'cfg calibrate 703').result==2
    keys(Fn=500,Ent=500); assert snapshot(dev).performance_mode==0
    keys(Fn=4000,Ent=4000)
    keys(Esc=500)
    s=snapshot(dev); assert s.mode==0 and s.revision==1 and not any(s.report)
    assert all(1<=p<r<=4095 for p,r in zip(s.press,s.release))
    index=labels.index('A')
    assert s.press[index]<1500 and s.release[index]>s.press[index]
    keys(Esc=4000)
    p,r=s.press[index],s.release[index]
    keys(A=p); assert not snapshot(dev).down[index]
    keys(A=p-1); assert snapshot(dev).down[index]
    keys(A=r); assert snapshot(dev).down[index]
    keys(A=r+1); assert not snapshot(dev).down[index]
    keys(A=4000)
    keys(Fn=500,Ent=500); assert snapshot(dev).performance_mode==0
    word_cycle('MIDI',(0,0,255))
    keys(Ent=4000) # cancel with Fn still held, not at a word boundary
    assert snapshot(dev).performance_mode==1
    assert color(draw(),'M')!=(0,0,77)
    keys(Ent=500); assert color(draw(),'M')!=(77,)*3 # no restart until neutral
    keys(Fn=4000,Ent=4000); dev.service(200); dev.midi_packets.clear()
    keys(Fn=500,K=500); keys(K=4000)
    assert not any(p[1]&0xf0==0x90 for p in dev.midi_packets),dev.midi_packets
    frame=draw()
    # Calibration stays keyboard-only; Tab is now a MIDI-mode hint (raw trigger).
    assert color(frame,'C')==(0,0,0) and color(frame,'Tab')!=(0,0,0), (color(frame,'C'),color(frame,'Tab'))
    dev.command('stream off'); dev.service(20)
    assert b'MENU fn=1 mode=0' in dev.command('menu status')
    snapshot(dev,'stream gui')
    keys(Fn=4000); keys(Fn=500,Ent=500)
    word_cycle('KEYBOARD',(0,255,0)) # absolute 30/100% at reduced brightness
    keys(Fn=4000) # releasing either chord member interrupts
    assert snapshot(dev).performance_mode==0
    assert color(draw(),'Y')!=(0,77,0)
    keys(Fn=4000,Ent=4000)
    # The Fn menu mirrors persisted settings into the two authorized pages. With
    # every key released, let that settle and take the settled pages as the
    # write-boundary baseline for the checks below.
    for _ in range(200):
        dev.command('stream off'); dev.service(20)
        line=dev.command('menu status')
        if b'dirty=0' in line: break
        snapshot(dev,'stream gui')
    else: raise AssertionError(b'mirror never settled: '+line)
    initial_pages={a:bytes(p) for a,p in dev.flash.pages.items()}
    reads=len(dev.flash.commands)
    snapshot(dev,'stream gui')
    keys(Fn=500,C=500)
    assert snapshot(dev).calibration_state==0
    keys(Fn=4000)
    assert snapshot(dev).calibration_state==1
    assert snapshot(dev,'cfg calcancel 704').result==1
    assert {a:bytes(p) for a,p in dev.flash.pages.items()}==initial_pages
    # Calibration entry and cancel only read the pages: no mirror is pending.
    assert all(cmd==3 for cmd,_ in dev.flash.commands[reads:]),dev.flash.commands[reads:]
    assert not dev.reset_requests
    assert ref.read(0x2001b49c,20)==bytes(dev.cpu.mem_read(dev.symbols['brightness_steps'],20))
    keys(C=4000); dev.service(1600)
    keys(Fn=500,R=500)
    word_cycle('RESET')
    assert {a:bytes(p) for a,p in dev.flash.pages.items()}==initial_pages
    keys(Fn=4000) # confirmation, never erase just by releasing the chord
    assert {a:bytes(p) for a,p in dev.flash.pages.items()}==initial_pages
    frame=draw()
    assert color(frame,'Y')==(0,255,0) and color(frame,'N')==(255,0,0)
    assert color(frame,'/')==(77,77,77) # question mark on /?
    keys(R=4000); keys(N=500); keys(N=4000)
    assert {a:bytes(p) for a,p in dev.flash.pages.items()}==initial_pages
    keys(Fn=500,R=500,Y=500); keys(Fn=4000,R=4000)
    assert {a:bytes(p) for a,p in dev.flash.pages.items()}==initial_pages # preheld Y rejected
    keys(Y=4000); keys(Y=500)
    assert all(all(v==255 for v in p) for p in dev.flash.pages.values())
    assert [(cmd,addr) for cmd,addr in dev.flash.commands if cmd!=3][-2:]==[(4,SLOTS[1]),(4,SLOTS[0])]
    keys(Y=4000); dev.service(300)
    s=snapshot(dev)
    assert s.calibration_generation==0 and s.calibration_flags==4 and not s.calibration_error
    assert s.press==(3500,)*61 and s.release==(3600,)*61 and s.performance_mode==0 and s.octave==0
    assert color(draw(),'Ent')==(0,255,0)
    assert not dev.reset_requests
    print('PASS ARM Fn menu: colored text, release-only actions, repeated brightness taps, original editor colors, MIDI suppression, calibration entry, Y/N confirmed tail-only RESET and defaults; mirror settles inside the two authorized pages only')


if __name__=='__main__': main()
