"""Focused application-only IAP checks; never opens USB hardware.

Optional --reference executes the user's private bootloader instructions, with
only the physical flash-program primitive modeled. No vendor bytes are bundled.
"""
import argparse
from dataclasses import replace
from hashlib import sha256
from pathlib import Path
import struct
import tempfile
import unittest

from flash_models import FirmwareImage
import monsgeek_iap as iap


def fixture(size=1024):
    data=bytearray(b'\xff'*size)
    data[:len(iap.IDENTITY)]=iap.IDENTITY
    struct.pack_into('<II',data,0x200,iap.RAM_END,iap.APP_BASE+0x209)
    data[0x220:0x22a]=b'MG-M1V5TMR'
    return bytes(data)


def image(data=None):
    data=iap.validate_application(fixture() if data is None else data,'custom')
    return FirmwareImage('',data,sha256(data).hexdigest(),'custom','test')


class Link:
    def __init__(self):
        self.writes=[]; self.reply=bytes(64); self.time=0; self.count=0
        self.data=bytearray(); self.reject=False; self.stale=False

    def sleep(self,seconds): self.time+=seconds
    def read(self): return self.reply
    def write(self,data):
        self.writes.append(data)
        if self.count:
            self.data.extend(data);self.count-=1
        elif data[:2]==b'\xba\xc0':
            self.count=int.from_bytes(data[2:4],'little')
            self.reply=(b'\xab\xc0'+data[2:4]).ljust(64,b'\0')
        elif data[:2]==b'\xba\xc2':
            if not self.stale:
                self.reply=(b'\xab\xc2'+data[2:4]+bytes([0xaa if self.reject else 0x55])+
                    (sum(self.data)&0xffffff).to_bytes(3,'little')).ljust(64,b'\0')
        else: raise AssertionError('Unexpected command')

    def run(self,firmware=None):
        return iap.program_application(self,firmware or image(),sleep=self.sleep,clock=lambda:self.time)


class Tests(unittest.TestCase):
    def test_exact_stream(self):
        link=Link();firmware=image(fixture(1025));self.assertEqual(link.run(firmware),firmware.digest)
        self.assertEqual(link.data,firmware.data)
        self.assertEqual(len(link.writes),len(firmware.data)//64+2)
        self.assertTrue(all(len(report)==64 for report in link.writes))

    def test_reject_stale_finish_bad_verdict_and_short_reply(self):
        for mode in ('reject','stale'):
            link=Link();setattr(link,mode,True)
            with self.assertRaises(iap.FlasherError): link.run()
            self.assertEqual(len(link.writes),18)  # no automatic retries
        link=Link();link.reply=b''
        with self.assertRaises(iap.FlasherError):link.run()
        self.assertEqual(link.writes,[])

    def test_changed_image_fails_before_io(self):
        link=Link()
        with self.assertRaises(iap.ImageError):link.run(replace(image(),digest='0'*64))
        self.assertEqual(link.writes,[])

    def test_protected_boundaries(self):
        for data in (fixture(0x22001),bytes(1024),fixture()[:512]):
            with self.assertRaises(iap.ImageError):iap.validate_application(data,'custom')
        for stack,reset in ((iap.RAM_END+8,iap.APP_BASE+0x209),
                            (iap.RAM_END,iap.APP_BASE-1),(iap.RAM_END,iap.APP_BASE+0x208)):
            data=bytearray(fixture());struct.pack_into('<II',data,0x200,stack,reset)
            with self.assertRaises(iap.ImageError):iap.validate_application(data,'custom')
        with tempfile.TemporaryDirectory() as directory:
            file=Path(directory)/'factory.bin'
            full=bytearray(b'\xa5'*0x40000);full[0x5000:0x28000]=fixture(0x23000)
            file.write_bytes(full)
            loaded=iap.load_image(file,'monsgeek')
            self.assertEqual(loaded.data,full[0x5000:0x28000])
            with self.assertRaises(iap.ImageError):iap.load_image(file,'custom')


def reference_check(path):
    from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2
    data=Path(path).read_bytes()
    if not 0x5000<=len(data)<=0x40000 or data[0x35f4:0x3602]!=iap.IDENTITY:
        raise ValueError('Expected the private ID2949/v410 full reference image')
    data=data.ljust(0x40000,b'\xff')
    class Reference(Link):
        def __init__(self,corrupt=False):
            super().__init__();self.corrupt=corrupt;self.targets=[]
            self.cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
            self.cpu.mem_map(0x08000000,0x40000);self.cpu.mem_write(0x08000000,data)
            self.cpu.mem_write(iap.APP_BASE,b'\xff'*(iap.ERASE_END-iap.APP_BASE))
            self.cpu.mem_map(iap.RAM_BASE,0x18000)
            self.cpu.hook_add(UC_HOOK_CODE,self.code)
        def code(self,cpu,address,size,user):
            # SDK lock/unlock and byte program are the only replaced routines.
            if address==0x0800109c:
                dst,src,n=(cpu.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
                assert n==64 and iap.APP_BASE<=dst<dst+n<=iap.PROFILE_BASE
                self.targets.append(dst);block=bytes(cpu.mem_read(src,n))
                if self.corrupt:block=bytes([block[0]^1])+block[1:]
                cpu.mem_write(dst,block)
            elif address not in (0x08001084,0x08000fc0):return
            cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        def call(self,address):
            self.cpu.reg_write(UC_ARM_REG_SP,iap.RAM_END-16)
            self.cpu.reg_write(UC_ARM_REG_LR,0x08004001)
            self.cpu.emu_start(address|1,0x08004000,count=100000)
            assert self.cpu.reg_read(UC_ARM_REG_PC)==0x08004000
        def write(self,report):
            self.writes.append(report);self.cpu.mem_write(0x20000554,report)
            active=self.cpu.mem_read(0x200004f8,1)[0]
            self.call(0x080006d0 if active else 0x0800048c)
        def read(self):
            self.cpu.mem_write(0x200004f7,b'\0')
            return bytes(self.cpu.mem_read(0x20000514,64))
    for corrupt in (False,True):
        link=Reference(corrupt)
        if corrupt:
            try:link.run()
            except iap.FlasherError:pass
            else:raise AssertionError('Original bootloader accepted corrupt flash readback')
        else:
            link.run();assert bytes(link.cpu.mem_read(iap.APP_BASE,1024))==fixture()
        assert link.targets==list(range(iap.APP_BASE,iap.APP_BASE+1024,64))
        assert bytes(link.cpu.mem_read(0x08000000,0x5000))==data[:0x5000]
        assert bytes(link.cpu.mem_read(iap.ERASE_END,0x18000))==data[0x28000:]
    print('PASS private bootloader instructions: start/data/finish, sequential bounds, checksum and corrupt-readback rejection (flash primitive modeled)')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--reference')
    args,rest=parser.parse_known_args()
    if args.reference:reference_check(args.reference)
    unittest.main(argv=[__file__]+rest)
