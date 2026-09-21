"""Private M1 application-image validator and factory-IAP transfer engine.

The GUI adapter owns identity, physical-port binding and guarded boot entry.
This module never discovers a device, enters IAP, retries a write or modifies
the bootloader. No vendor image or recovered implementation is included.
"""
from hashlib import sha256
from pathlib import Path
import struct
import time

from flash_models import FirmwareImage
from firmware_flasher import ImageError, FlasherError

APP_BASE = 0x08005000
VECTOR_OFFSET = 0x200
PROFILE_BASE = 0x08027000
ERASE_END = 0x08028000
RAM_BASE, RAM_END = 0x20000000, 0x20018000
IDENTITY = b'AT32F405 8KMKB'
BLOCK_BYTES = 64
REPORT_TYPE = 0x0300
TRANSFER_TIMEOUT_MS = 2000
# No per-data-block completion counter is exposed by the factory protocol.
# Serialize OUTs with conservative pacing, then require the bootloader's
# whole-image checksum AND per-byte flash-readback verdict. No timed delay is
# advertised as a per-block acknowledgement or a physical timing guarantee.
BLOCK_SETTLE_SECONDS = .010
REPLY_TIMEOUT_SECONDS = 1.0


def validate_application(data, destination):
    if destination not in ('custom', 'monsgeek'):
        raise ImageError('Unknown M1 firmware destination')
    limit = (PROFILE_BASE if destination == 'custom' else ERASE_END) - APP_BASE
    if not VECTOR_OFFSET+8 <= len(data) <= limit or not data.startswith(IDENTITY):
        raise ImageError('Expected a bounded M1 application starting at 0x08005000')
    stack, reset = struct.unpack_from('<II', data, VECTOR_OFFSET)
    if stack & 7 or not RAM_BASE < stack <= RAM_END:
        raise ImageError('M1 initial stack is outside aligned MCU SRAM')
    if not reset & 1 or not APP_BASE+VECTOR_OFFSET <= (reset & ~1) < APP_BASE+len(data):
        raise ImageError('M1 reset vector is outside the supplied application')
    if destination == 'custom' and b'MG-M1V5TMR' not in data:
        raise ImageError('M1 custom image lacks the matching board build identity')
    padded = data + b'\xff' * (-len(data) % BLOCK_BYTES)
    if len(padded) > limit:
        raise ImageError('Padded application crosses its permitted flash boundary')
    return padded


def load_image(path, destination):
    file = Path(path)
    if file.suffix.lower() != '.bin' or file.stat().st_size > 0x40000:
        raise ImageError('Select an M1 application .bin or a boot-prefixed factory image (at most 256 KiB)')
    data = file.read_bytes()
    source = 'Application-only binary'
    if destination == 'monsgeek' and data[0x5000:0x5000+len(IDENTITY)] == IDENTITY:
        data = data[0x5000:0x28000]
        source = 'Application slice only; bootloader and factory data excluded'
    data = validate_application(data, destination)
    return FirmwareImage(str(file), data, sha256(data).hexdigest(), destination,
        source+'; factory IAP erases custom profile slots on every update')


class IapLink:
    """An already selected/claimed bootloader; never select by shared PID alone."""
    def __init__(self, device):
        self.device = device

    def write(self, report):
        result = self.device.ctrl_transfer(0x21, 9, 0, 0, report,
                                           timeout=TRANSFER_TIMEOUT_MS)
        if result != BLOCK_BYTES:
            raise FlasherError('Incomplete M1 IAP write; do not resend this block')

    def read(self):
        result = bytes(self.device.ctrl_transfer(0xa1, 1, REPORT_TYPE, 0, BLOCK_BYTES,
                                                 timeout=TRANSFER_TIMEOUT_MS))
        if len(result) != BLOCK_BYTES:
            raise FlasherError('Incomplete M1 IAP status reply')
        return result


def program_application(link, image, progress=lambda done,total: None, *,
                        sleep=time.sleep, clock=time.monotonic):
    """One non-retrying attempt on a freshly erased, identity-bound IAP session.

    The caller must establish boot-flag-based recovery before writing: IAP
    entered solely because of an invalid header does NOT prove a persistent
    recovery flag. An interrupted header-first update otherwise risks booting
    an incomplete application. This engine does not clear or erase that flag;
    the factory bootloader clears it only after its successful finish verdict.
    """
    # Revalidate the transmitted bytes independently of GUI metadata/hash.
    data = validate_application(image.data, image.destination)
    if data != image.data or sha256(data).hexdigest() != image.digest:
        raise ImageError('M1 image changed after confirmation')
    total = len(data)//BLOCK_BYTES
    count = struct.pack('<H', total)
    checksum = (sum(data) & 0xffffff).to_bytes(3, 'little')

    def send(report):
        if len(report) != BLOCK_BYTES:
            raise ImageError('IAP requires exactly one 64-byte block')
        link.write(bytes(report))
        sleep(BLOCK_SETTLE_SECONDS)

    def response(prefix):
        deadline = clock()+REPLY_TIMEOUT_SECONDS
        while True:
            result = link.read()
            if len(result) != BLOCK_BYTES:
                raise FlasherError('Incomplete M1 IAP status reply')
            if result[:len(prefix)] == prefix:
                return result
            if clock() >= deadline:
                raise FlasherError('M1 bootloader did not acknowledge the command; no automatic retry')
            sleep(BLOCK_SETTLE_SECONDS)

    # Reading status clears the bootloader's busy latch; it is not read-only
    # discovery and must never be used on an unconfirmed bootloader candidate.
    if len(link.read()) != BLOCK_BYTES:
        raise FlasherError('Incomplete initial M1 IAP status')
    start = b'\xba\xc0'+count+len(data).to_bytes(3, 'little')
    send(start.ljust(BLOCK_BYTES, b'\0'))
    response(b'\xab\xc0'+count)
    progress(0, total)
    for block in range(total):
        send(data[block*BLOCK_BYTES:(block+1)*BLOCK_BYTES])
        # The returned window may still echo START. Do not invent an ACK or
        # send QUERY command pages while raw-data mode is active.
        if len(link.read()) != BLOCK_BYTES:
            raise FlasherError('Incomplete M1 IAP data status; transfer stopped')
        progress(block+1, total)
    finish = b'\xba\xc2'+count+checksum
    send(finish.ljust(BLOCK_BYTES, b'\0'))
    result = response(b'\xab\xc2'+count)
    if result[4] != 0x55 or result[5:8] != checksum:
        raise FlasherError('M1 bootloader rejected flash readback/checksum; recovery remains required')
    return image.digest
