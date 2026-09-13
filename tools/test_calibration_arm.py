#!/usr/bin/env python3
"""Offline execution of compiled calibration and original controller routines.

Models flash command completion and contents; never opens hardware. This proves
addresses/command flow and application integration, not silicon erase timing.
"""
import argparse
import struct
import zlib
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_PRIMASK
from test_flash_dump_arm import DumpArm
from test_lighting_arm import LightingArm
from test_usb_startup_arm import StartupArm
from test_keyboard_mode_arm import snapshot
from scan_bars import sensor_labels

SLOTS=(0x78000,0x78200)
# Regions the application must never erase or program. The flash model rejects
# any controller command outside the two authorized pages, so a stray address
# into the application image, the primary settings or the serial-number pages
# fails the test instead of silently corrupting the device.
PROTECTED=((0x00000,0x20000,'application image'),
           (0x49000,0x49400,'primary settings and serial number'))

def record(gen=1):
    p=bytearray(b'\xff'*512)
    struct.pack_into('<4s4BII',p,0,b'HKC1',1,1,61,0,gen,0x314c4143)
    struct.pack_into('<65H',p,16,*([1000]*61+[0]*4))
    struct.pack_into('<65H',p,146,*([4000]*61+[0]*4))
    struct.pack_into('<I',p,508,zlib.crc32(p[:508]))
    return bytes(p)

class FlashModel:
    def __init__(self,cpu):
        self.cpu=cpu
        self.pages={a:bytearray(b'\xff'*512) for a in SLOTS}
        self.touched=set()   # pages an erase/program command addressed
        self.buffer={}; self.trace=[]; self.commands=[]; self.fail=None
        cpu.hook_add(UC_HOOK_MEM_WRITE,self.write,begin=0x40034000,end=0x40034fff)
    def u32(self,a): return int.from_bytes(self.cpu.mem_read(a,4),'little')
    def write(self,cpu,access,a,size,v,user):
        self.trace.append((a,v))
        if a==0x40034fe8: cpu.mem_write(0x40034fe0,bytes(4))
        if a!=0x40034000: return
        address=self.u32(0x40034010)*16
        self.commands.append((v,address))
        base=address & ~511
        assert base in SLOTS, ('flash command outside the two authorized pages',hex(address))
        for start,end,what in PROTECTED:
            assert not start <= address < end, ('flash command inside '+what,hex(address))
        if v in (4,12): self.touched.add(base)
        assert v in (3,4,8,12),v
        status=5 if self.fail==v else 4
        if status==4:
            if v==3:
                cpu.mem_write(0x40034080,bytes(self.pages[base][address-base:address-base+16]))
            elif v==4:
                assert address==base and self.u32(0x40034014)*16==base
                self.pages[base][:]=b'\xff'*512; self.buffer.clear()
            elif v==8: self.buffer[address]=bytes(cpu.mem_read(0x40034080,16))
            elif v==12:
                assert sorted(self.buffer)==list(range(base,base+512,16))
                for a,data in self.buffer.items(): self.pages[base][a-base:a-base+16]=data
                self.buffer.clear()
        cpu.mem_write(0x40034fe0,struct.pack('<I',status))

class Low(DumpArm):
    def flash_write(self,*args): pass # replace the read-only dumper's model
    def __init__(self,elf):
        super().__init__(elf,True)
        self.flash=FlashModel(self.cpu); self.put32(0x40000fe0,2)

class Live(LightingArm):
    def peripheral_write(self,cpu,access,address,size,value,user):
        if address==0x40034000 and value!=2: return
        StartupArm.peripheral_write(self,cpu,access,address,size,value,user)
    def __init__(self,elf,reference,pages=None):
        super().__init__(elf,reference)
        self.flash=FlashModel(self.cpu); self.put32(0x40000fe0,2)
        # Seed (or replace) individual pages; unlisted slots stay blank.
        if pages:
            for address,page in pages.items(): self.flash.pages[address]=bytearray(page)

def low_tests(args):
    for slot in (0,1):
        dev=Low(args.elf); p=record(); dev.cpu.mem_write(0x2003d000,p)
        for primask in (0,1):
            dev.cpu.reg_write(UC_ARM_REG_PRIMASK,primask)
            assert dev.call('flash_calibration_write',slot,0x2003d000)==0
            assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==primask
            assert bytes(dev.flash.pages[SLOTS[slot]])==p
            assert dev.u32(0x4000041c)==1
        assert dev.call('flash_calibration_read',slot,0x2003d400)==0
        assert bytes(dev.cpu.mem_read(0x2003d400,512))==p
        # Every erase/program in this flow went to the one authorized page.
        assert dev.flash.touched=={SLOTS[slot]},dev.flash.touched
        before=len(dev.flash.commands)
        for invalid in (2,255,0xffffffff):
            assert dev.call('flash_calibration_erase',invalid)!=0
            assert dev.call('flash_calibration_write',invalid,0x2003d000)==102
            assert dev.call('flash_calibration_read',invalid,0x2003d400)==102
        dev.cpu.mem_write(0x2003d000,b'BAD!')
        assert dev.call('flash_calibration_write',slot,0x2003d000)==102
        assert len(dev.flash.commands)==before
    for command in (4,8,12):
        dev=Low(args.elf); dev.flash.fail=command; dev.cpu.mem_write(0x2003d000,record())
        assert dev.call('flash_calibration_write',0,0x2003d000)==105
        assert dev.flash.commands[-1][0]==command
        assert dev.cpu.reg_read(UC_ARM_REG_PRIMASK)==0
    for part in (0,1,3,5):
        dev=Low(args.elf); dev.put32(0x40000fe0,part); dev.cpu.mem_write(0x2003d000,record())
        assert dev.call('flash_calibration_write',0,0x2003d000)==102 and not dev.flash.commands
    print('PASS compiled tail-only erase/program/readback, bounds, malformed record, geometry, error stop, IRQ preservation')
    from production_arm import ProductionArm
    ref=ProductionArm(args.reference)
    ref.cpu.mem_map(0x40000000,0x1000); ref.cpu.mem_map(0x40034000,0x1000)
    ref.write(0x200284c0,struct.pack('<5I',0,0x80000,1,512,32768)+bytes(40))
    ref.write(0x2003d000,record()); model=FlashModel(ref.cpu)
    assert ref.call(0x2000edc4,SLOTS[0],512)==0
    assert ref.call(0x2000ee18,SLOTS[0],0x2003d000,512)==0
    dev=Low(args.elf); dev.cpu.mem_write(0x2003d000,record())
    assert dev.call('flash_calibration_write',0,0x2003d000)==0
    assert dev.flash.trace==model.trace, (dev.flash.trace[:10],model.trace[:10])
    assert dev.flash.pages==model.pages
    print('PASS original ARM differential: identical erase + 32 buffer loads + program register trace and page bytes')

def live_tests(args):
    dev=Live(args.elf,args.reference); dev.service(400)
    s=snapshot(dev,'stream gui'); assert s.calibration_flags==4
    assert 0x20000000 <= dev.symbols['s_cal'] < dev.symbols['__app_load_end__'] <= 0x2001fc00
    assert len(dev.flash.commands)==64 and all(cmd==3 for cmd,_ in dev.flash.commands)
    labels=sensor_labels()[61]
    dev.raw[labels.index('Fn')]=1000; dev.raw[labels.index('C')]=1000
    s=snapshot(dev); assert s.calibration_state==0 and not any(s.report)
    dev.raw[labels.index('Fn')]=4000
    s=snapshot(dev); assert s.calibration_state==1
    dev.raw=[4000]*61
    s=snapshot(dev); assert s.calibration_state==2
    dev.service(510)
    s=snapshot(dev); assert s.calibration_state==3
    s=snapshot(dev,'cfg all 700 2000 3000'); assert s.result==2 and s.press[0]==3500
    s=snapshot(dev,'cfg calcancel 701'); assert s.result==1 and s.calibration_state==7 and s.calibration_reason==3
    assert all(cmd==3 for cmd,_ in dev.flash.commands)
    s=snapshot(dev,'cfg calibrate 702'); assert s.result==1
    # Advance simulated elapsed time, never report scan stale: the next DMA
    # completion carries a fresh sample. No physical device participates.
    def elapsed(ms):
        dev.put32(dev.symbols['s_milliseconds'],dev.call_time()+ms)
        dev.service(4)
    elapsed(600); assert snapshot(dev).calibration_state==3
    elapsed(5000); s=snapshot(dev); assert s.calibration_state==7 and s.calibration_reason==1
    assert all(cmd==3 for cmd,_ in dev.flash.commands)
    s=snapshot(dev,'cfg calibrate 703'); assert s.result==1
    elapsed(600); assert snapshot(dev).calibration_state==3
    dev.reports.clear()
    for i in range(61):
        dev.raw[i]=1000+i
        dev.service(6)
        elapsed(1000)
        s=snapshot(dev)
        assert s.calibration_completed==i+1,(i,s)
        dev.raw[i]=4000; dev.service(6)
        assert all(not any(p) for p in dev.reports)
    assert s.calibration_state==6 and s.calibration_flags==6 and s.calibration_generation==1
    assert sum(cmd==4 for cmd,_ in dev.flash.commands)==1
    assert sum(cmd==12 for cmd,_ in dev.flash.commands)==1
    assert bytes(dev.flash.pages[SLOTS[1]])==b'\xff'*512
    assert dev.flash.touched=={SLOTS[0]},dev.flash.touched
    reboot=Live(args.elf,args.reference,dev.flash.pages); reboot.service(400)
    s=snapshot(reboot,'stream gui'); assert s.calibration_flags==6 and s.calibration_generation==1
    assert all(cmd==3 for cmd,_ in reboot.flash.commands)
    reboot.command('stream off'); reboot.service(10)
    output=reboot.command('scan sample 20'); assert b'lower=1032 upper=4000' in output,output
    print('PASS compiled Fn+C/GUI arm, release+settle, rejected edits, cancel/timeout no writes, 61-key save with HID isolation, reboot calibration readback')
    dev=reboot; previous_page=bytes(dev.flash.pages[SLOTS[0]]); snapshot(dev,'stream gui')
    s=snapshot(dev,'cfg calibrate 704'); assert s.result==1
    elapsed(600); assert snapshot(dev).calibration_state==3
    dev.raw[0]=1000; dev.raw[1]=1100; dev.raw[2]=1200; dev.service(6)
    s=snapshot(dev); assert [bool(v&8) for v in s.velocity_state[:4]]==[True,True,True,False]
    dev.raw[2]=3800; dev.service(6)
    elapsed(500); dev.raw[0]=1500; dev.service(6) # restart only key zero
    elapsed(500); s=snapshot(dev)
    assert s.calibration_done[1] and not s.calibration_done[0] and not s.calibration_done[2]
    assert s.velocity_state[0]&8 and not s.velocity_state[1]&8 and not s.velocity_state[2]&8
    elapsed(500); s=snapshot(dev); assert s.calibration_done[0]
    # Both registered keys stay held while all remaining keys calibrate together.
    dev.raw=[1500,1100]+[1000+i for i in range(2,61)]; dev.service(6)
    s=snapshot(dev); assert sum(bool(v&8) for v in s.velocity_state)==59
    dev.reports.clear(); elapsed(1000); s=snapshot(dev)
    assert s.calibration_completed==61 and s.calibration_state==6 and s.calibration_generation==2
    assert all(not any(p) for p in dev.reports) and not any(v&8 for v in s.velocity_state)
    assert bytes(dev.flash.pages[SLOTS[0]])==previous_page
    assert sum(cmd==4 for cmd,_ in dev.flash.commands)==1
    # The second save rotated to the other slot; this device wrote only that one.
    assert dev.flash.touched=={SLOTS[1]},dev.flash.touched
    print('PASS compiled parallel calibration: concurrent hold telemetry, isolated release/motion, completed keys held, final 59-key batch, second A/B save')

def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('elf'); p.add_argument('--reference',required=True)
    args=p.parse_args(); low_tests(args); live_tests(args)
if __name__=='__main__': main()
