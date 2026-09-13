#!/usr/bin/env python3
"""Compiled dumper/SysEx tests; flash controller modeled, hardware untouched."""
import argparse
import struct
from unicorn import UC_HOOK_MEM_WRITE
from test_midi_control_arm import MidiControlArm
from test_scan_stream_arm import drain
from test_dump_protocol import decode


class DumpArm(MidiControlArm):
    def __init__(self,elf,hs):
        self.reads=[]; self.read_error=None; self.flags=12; self.stuck=False; self.writes=[]
        super().__init__(elf,hs)
        self.put32(0x40000fe0,255)
        self.put32(self.symbols['SystemCoreClock'],96000000)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,self.flash_write,begin=0x40034000,end=0x40034fff)
        self.call('keyboard_live_init')
        # Command callback is the final field, following the isolated engine.
        self.call('midi_control_command_handler', self.symbols['keyboard_live_command'])

    def flash_write(self,cpu,access,address,size,value,user):
        self.writes.append((address,value))
        if address==0x40034fe8: self.put32(0x40034fe0,0)
        if address==0x40034000:
            assert value==3,'only READ_SINGLE_WORD allowed'
            start=self.u32(0x40034010)*16
            assert start%16==0 and start<0x7f400 and self.u32(0x40034080)==0
            self.reads.append(start)
            cpu.mem_write(0x40034080,bytes((start+i)%251 for i in range(16)))
            self.put32(0x40034fe0,0 if self.stuck else self.flags if start==self.read_error else 4)


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('elf'); p.add_argument('--reference'); args=p.parse_args()
    for hs in (False,True):
        dev=DumpArm(args.elf,hs)
        assert not dev.reads
        out=dev.command('dump read 1 0\n',True)
        data,statuses,total,page=decode(out,1,0)
        assert data==bytes(range(64)) and statuses==(0,)*4 and total==0x9de00 and page==512
        assert dev.reads==[0,16,32,48]
        dev.read_error=80
        data,statuses,_,_=decode(dev.command('dump read 2 64\n'),2,64)
        assert statuses==(0,116,0,0) and data[16:32]==bytes(16) and data[:16]==bytes(range(64,80))
        before=len(dev.reads)
        for address in (1,0x7f400,0xffffffff):
            out=dev.command(f'dump read 3 {address}\n')
            assert len(out)==128 and struct.unpack_from('<I',out,24)[0]==102
        for cmd in ('dump read 0 0','dump read 4 -1','dump read 4 4294967296','dump read 4 0 junk','dump read 4'):
            assert not dev.command(cmd+'\n')
        assert len(dev.reads)==before
        # Corruption and mismatched identities fail, not silent resynchronization.
        good=dev.command('dump read 5 128\n')
        for packet,ident,address in ((good,6,128),(good,5,192),(good[:-1],5,128),
                                    (good[:50]+bytes([good[50]^1])+good[51:],5,128)):
            try: decode(packet,ident,address)
            except ValueError: pass
            else: raise AssertionError('invalid response accepted')
        # Pending USB buffer immutable; busy request must not issue flash commands.
        dev.cpu.mem_write(0x2003d000,b'dump read 7 256\0')
        dev.call('keyboard_live_command',0x2003d000)

        before=len(dev.reads)
        dev.call('keyboard_live_command',0x2003d000)
        assert len(dev.reads)==before
        assert decode(drain(dev),7,256)[0]==bytes((256+i)%251 for i in range(64))
        assert not any(name in dev.symbols for name in ('FLASH_Init','FLASH_Read','FLASH_Erase','FLASH_Program','FFR_CustFactoryPageWrite'))
        assert not dev.reset_requests
        print(f'PASS {"HS" if hs else "FS"}: dumper framing/CRC, bounds, 16-byte errors, pending IN ownership, stream resume; no erase/program issued by dump commands or ROM wrappers linked')
    dev=DumpArm(args.elf,True); dev.stuck=True
    out=dev.command('dump read 9 0\n')
    assert decode(out,9,0)[1]==(0x10001,)*4 and len(dev.reads)==1
    dev.command('dump read 10 0\n'); assert len(dev.reads)==1
    for part in (5,3,0x100):
        dev=DumpArm(args.elf,True); dev.put32(0x40000fe0,part)
        out=dev.command('dump read 11 0\n')
        assert struct.unpack_from('<I',out,24)[0]==102 and not dev.reads
    print('PASS bounded timeout latches; no subsequent command; unknown/flashless die rejected')
    if args.reference:
        from production_arm import ProductionArm
        for flags,expected in ((4,0),(5,105),(6,111),(12,116),(15,105)):
            ref=ProductionArm(args.reference); ref.cpu.mem_map(0x40034000,0x1000)
            ref.write(0x200284c0,struct.pack('<5I',0,0x9de00,1,512,32768)+bytes(40))
            prod=[]
            def reference_write(cpu,access,address,size,value,user):
                prod.append((address,value))
                if address==0x40034000:
                    cpu.mem_write(0x40034fe0,struct.pack('<I',flags))
                    cpu.mem_write(0x40034080,bytes(range(16)))
            ref.cpu.hook_add(UC_HOOK_MEM_WRITE,reference_write,begin=0x40034000,end=0x40034fff)
            status=ref.call(0x20001f94,0x200284c0,0,0x2003d000)
            dev=DumpArm(args.elf,True); dev.read_error=0; dev.flags=flags
            out=dev.command('dump read 12 0\n')
            assert decode(out,12,0)[1][0]==status==expected
            assert dev.writes[:4]==prod,(dev.writes[:4],prod)
            if not status: assert out[48:64]==ref.read(0x2003d000,16)
        print('PASS reference ARM differential: exact READ_SINGLE_WORD register sequence, normal data and FAIL/ERR/ECC priority')


if __name__=='__main__': main()
