"""Offline wire-to-original-code IAP audit; never touches physical USB.

Supply the private ID2304/v309 full image as the sole argument. No reference
bytes are copied into this repository. Flash busy/erase and USB transport are
modeled; command parsing, block programming/readback and the result latch run
the original ARM instructions. This does not validate real USB/flash timing.
"""
import hashlib
from pathlib import Path
import struct
import sys
import re
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                              UC_CPU_ARM_CORTEX_M4)
import monsgeek_iap as iap
from test_monsgeek_iap import custom_image

STATE = 0x200004f4
DEVICE = 0x20001000
CLASS = 0x20002000
SETUP = 0x20003000
RETURN = 0x2001f000


class OriginalPeer:
    def __init__(self, reference):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for base, size in ((0x08000000, 0x40000), (0x20000000, 0x20000),
                           (0x40023000, 0x1000)):
            self.cpu.mem_map(base, size)
        self.cpu.mem_write(iap.FLASH_BASE, reference)
        self.cpu.mem_write(DEVICE+4, struct.pack('<I', DEVICE+0x100))
        self.cpu.mem_write(DEVICE+0x100+0x24, struct.pack('<I', CLASS))
        self.erased = []
        self.reply = None
        self.rx_address = None
        self.corrupt_verify = False
        self.sent = []
        for address in (0x08001044, 0x08002506, 0x08002542, 0x08002d94, 0x08000f4c):
            self.cpu.hook_add(UC_HOOK_CODE, self.stub, begin=address, end=address)

    def stub(self, cpu, address, size, _):
        r0, r1, r2 = (cpu.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
        if address == 0x08001044:
            self.erased.append(r0)
            cpu.mem_write(r0, b'\xff'*0x800)
        elif address == 0x08002506:
            assert r2 == 64
            self.rx_address = r1
        elif address == 0x08002542:
            assert r2 == 64
            self.reply = bytes(cpu.mem_read(r1, r2))
        cpu.reg_write(UC_ARM_REG_R0, 64 if address == 0x08002d94 else 0)
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def call(self, address, r0=0, r1=0):
        for reg, value in ((UC_ARM_REG_SP, 0x2001e000), (UC_ARM_REG_LR, RETURN|1),
                           (UC_ARM_REG_R0, r0), (UC_ARM_REG_R1, r1)):
            self.cpu.reg_write(reg, value)
        self.cpu.emu_start(address|1, RETURN, count=100000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == RETURN, hex(address)

    def set_feature(self, data):
        assert len(data) == 64
        self.sent.append(data)
        self.cpu.mem_write(SETUP, struct.pack('<BBHHH', 0x21, 9, 0x0300, 0, 64))
        self.rx_address = None
        self.call(0x0800090c, DEVICE, SETUP)
        assert self.rx_address is not None
        self.cpu.mem_write(self.rx_address, data)
        self.call(0x08000878, DEVICE)
        if self.corrupt_verify and data[:2] == b'\xba\xc2':
            self.cpu.mem_write(STATE+0x1c, struct.pack('<I', 1))
        self.call(0x080007f0)

    def get_feature(self):
        self.cpu.mem_write(SETUP, struct.pack('<BBHHH', 0xa1, 1, 0x0300, 0, 64))
        self.reply = None
        self.call(0x0800090c, DEVICE, SETUP)
        assert self.reply is not None
        assert self.cpu.mem_read(STATE+3, 1) == b'\0'  # GET releases result latch
        return self.reply


def main(path):
    reference = Path(path).read_bytes()
    assert hashlib.sha256(reference).hexdigest() == iap.FACTORY_FULL_SHA256
    # Independently check wiring facts, not a copied stock table. GUI geometry
    # and firmware share these entries; a wrong sensor/LED would mislabel keys.
    table = Path(__file__).resolve().parents[1]/'firmware/boards/monsgeek_fun60_pro_wired/include/fun60_keys.def'
    entries = re.findall(r'^FUN60_KEY\((\d+), ("(?:\\.|[^"\\])*"), (0x[0-9a-f]+), (\d+), (\d+), (\d+),', table.read_text(), re.M)
    assert len(entries) == 61
    for index, label, usage, row, channel, led in entries:
        row, channel = int(row), int(channel)
        word = struct.unpack_from('<I', reference, 0x1c360+row*24+channel*4)[0]
        assert word == (int(usage, 16)<<16 if int(usage, 16) else 0x010a), label
        grid_row, grid_col = reference[0x1e1a0+42*channel+2*row:0x1e1a0+42*channel+2*row+2]
        assert grid_row < 6 and grid_col < 16, label
        assert reference[0x1e140+16*grid_row+grid_col] == int(led), label
    factory = iap.validate_image(reference, 'factory')
    assert hashlib.sha256(factory.data).hexdigest() == iap.FACTORY_APP_SHA256
    for image in (iap.validate_image(custom_image(), 'custom'), factory):
        peer = OriginalPeer(reference)
        peer.call(0x08000420)  # actual erase loop, only individual erase is modeled
        assert peer.erased == list(range(iap.APP_BASE, iap.APP_END, 0x800))
        assert peer.cpu.mem_read(iap.FLASH_BASE, 0x5000) == reference[:0x5000]
        iap.IapTransfer(peer, sleep=lambda _: None).write(image)
        assert bytes(peer.cpu.mem_read(iap.APP_BASE, len(image.padded))) == image.padded
        assert peer.cpu.mem_read(STATE+4, 1) == b'\0'  # all blocks consumed
        peer.call(0x080007f0)
        assert peer.cpu.mem_read(STATE, 2) == b'\x01\0'  # success/reset request
    peer = OriginalPeer(reference)
    peer.corrupt_verify = True
    transfer = iap.IapTransfer(peer, sleep=lambda _: None)
    try:
        transfer.write(iap.validate_image(custom_image(), 'custom'))
        raise AssertionError('Readback failure accepted')
    except iap.IapError:
        assert peer.cpu.mem_read(STATE+0x1c, 4) == bytes(4)  # failure consumes evidence!
    peer.call(0x080007f0)
    assert peer.cpu.mem_read(STATE, 2) == b'\0\x01'  # failure/reboot-to-IAP request
    print('PASS: original ARM IAP/control handlers, full factory/custom transfer, erase bounds, failure latch')


if __name__ == '__main__':
    main(sys.argv[1])
