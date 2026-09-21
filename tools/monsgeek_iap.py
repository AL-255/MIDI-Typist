"""FUN60 PRO Wired (ID2304) application-image and IAP contracts.

Independent host implementation from observed wire behavior, not recovered code.
No device discovery, command-line entry point, or implicit bootloader entry here.
See docs/MONSGEEK_FUN60_PRO.md for evidence and destructive side effects.
"""
from dataclasses import dataclass
import hashlib
import struct
import time
from typing import Protocol

VID = 0x3151
APP_PID = 0x502D
BOOT_PID = 0x502A
FLASH_BASE = 0x08000000
APP_BASE = 0x08005000
VECTOR_OFFSET = 0x200
APP_END = 0x08028000  # exclusive: bootloader erases 70 sectors from APP_BASE
APP_BYTES = APP_END - APP_BASE
HEADER_TAG = b'AT32F405 8KMKB  '
BOARD_ID = b'monsgeek_fun60_pro_wired'
CUSTOM_MARKER = b'MIDI-Typist:' + BOARD_ID + b'\0'
FACTORY_FULL_SHA256 = '237bbe74f2f718a08bf2b8bb9caf6ca026b86817b6baae6130e0a7794d72a6ac'
FACTORY_APP_SHA256 = '056985a009349ce82037d32cc7f602eec858837d5d0280914f0c03c2df2a1e66'
BLOCK_BYTES = 64
PREPARE, START, QUERY = 0xFFBA, 0xC0BA, 0xC2BA
COMMAND_SETTLE_S = 0.005
BLOCK_SETTLE_S = 0.001  # one RX slot, no block sequence/ack/retry on the wire


class IapError(ValueError):
    """Malformed image, ambiguous transfer or failed readback: stop, never retry."""


@dataclass(frozen=True)
class ApplicationImage:
    data: bytes
    stripped_bootloader: bool
    destination: str

    @property
    def padded(self):
        return self.data + b'\xff' * (-len(self.data) % BLOCK_BYTES)

    @property
    def checksum(self):
        return sum(self.padded) & 0xffffff


def validate_image(data: bytes, destination: str) -> ApplicationImage:
    """Only application bytes can escape this boundary; never return a bootloader.

    Factory headers are shared by different SKUs, so header matching alone must
    not authorize restoration. Accept only the independently identified ID2304
    full image. Custom builds carry our explicit model marker in their header.
    Neither the marker nor SHA identifies a trusted publisher: users still need
    trusted firmware. These checks prevent accidental wrong-model flashing.
    """
    if destination not in ('custom', 'factory'):
        raise IapError('Unknown FUN60 PRO firmware destination')
    if not isinstance(data, bytes) or not VECTOR_OFFSET + 8 <= len(data) <= 0x28000:
        raise IapError('Invalid FUN60 PRO image length')
    if destination == 'factory' and hashlib.sha256(data).hexdigest() != FACTORY_FULL_SHA256:
        raise IapError('Factory image is not the verified FUN60 PRO Wired ID2304/v309 full image')
    stripped = data[APP_BASE-FLASH_BASE:APP_BASE-FLASH_BASE+len(HEADER_TAG)] == HEADER_TAG
    if stripped:
        data = data[APP_BASE-FLASH_BASE:]
    if not VECTOR_OFFSET + 8 <= len(data) <= APP_BYTES or not data.startswith(HEADER_TAG):
        raise IapError('Expected application header at 0x08005000, within the IAP erase region')
    stack, reset = struct.unpack_from('<II', data, VECTOR_OFFSET)
    # Conservative 64 KiB RAM window, including the vendor stack at 0x2000BEC8.
    # Do not infer usable capacity solely from the size of the downloaded image.
    if stack % 8 or not 0x20000000 < stack <= 0x20010000:
        raise IapError('Invalid FUN60 PRO initial stack pointer')
    if not reset & 1 or not APP_BASE+VECTOR_OFFSET+8 <= (reset & ~1) < APP_BASE+len(data):
        raise IapError('Reset handler is not inside the supplied application')
    if destination == 'custom' and data[0x20:0x20+len(CUSTOM_MARKER)] != CUSTOM_MARKER:
        raise IapError('Missing current MIDI-Typist FUN60 PRO Wired model marker')
    return ApplicationImage(data, stripped, destination)


def factory_boot_request() -> bytes:
    """DESTRUCTIVE factory command: erases settings and enters IAP.

    IAP itself erases the entire application before USB enumeration. Merely
    asking to enter it is already destructive, even before START is sent.
    """
    data = bytearray(BLOCK_BYTES)
    data[:5] = bytes((0x7f, 0x55, 0xaa, 0x55, 0xaa))
    data[7] = (0xff - sum(data[:7])) & 0xff
    return bytes(data)


def command(opcode: int, count=0, checksum=0) -> bytes:
    if opcode not in (PREPARE, START, QUERY):
        raise IapError('Unsupported IAP opcode')
    if not 0 <= count <= APP_BYTES // BLOCK_BYTES or not 0 <= checksum <= 0xffffff:
        raise IapError('IAP count/checksum out of range')
    if opcode == START and not count:
        raise IapError('An empty transfer would leave the bootloader armed')
    if opcode == PREPARE and (count or checksum):
        raise IapError('PREPARE does not take fields')
    data = bytearray(BLOCK_BYTES)
    struct.pack_into('<HH', data, 0, opcode, count)
    data[4:7] = checksum.to_bytes(3, 'little')
    return bytes(data)


def check_reply(data: bytes, opcode: int, count=0, checksum=None):
    if len(data) != BLOCK_BYTES:
        raise IapError('Short IAP reply; transfer state is unknown')
    if data[:2] != bytes((0xab, opcode >> 8)) or int.from_bytes(data[2:4], 'little') != count:
        raise IapError('Stale or mismatched IAP reply')
    if opcode == QUERY:
        if checksum is None or data[4] != 0x55 or int.from_bytes(data[5:8], 'little') != checksum:
            raise IapError('IAP checksum/readback failed; do not retry QUERY')


class FeatureTransport(Protocol):
    """64 report bytes, WITHOUT a hidraw report-ID prefix.

    SET_REPORT: 21/09/0300/interface0, GET_REPORT: a1/01/0300/interface0.
    The binding must reject short writes and pin every open to the confirmed
    physical USB location. Timeouts must not trigger automatic retransmission.
    """
    def set_feature(self, data: bytes) -> None: ...
    def get_feature(self) -> bytes: ...


class IapTransfer:
    """One-shot transaction on an already confirmed, freshly enumerated IAP.

    No retries: a lost completion may mean a block was written, and retransmit
    shifts all subsequent flash data. QUERY is destructive to error evidence:
    matching-checksum failure clears the device's readback-error counter.
    """
    def __init__(self, transport: FeatureTransport, sleep=time.sleep):
        self.transport = transport
        self.sleep = sleep
        self.used = False

    def write(self, image: ApplicationImage, progress=lambda done, total: None):
        if self.used:
            raise IapError('IAP transaction has already been used; refresh device state')
        # Recheck even when an ApplicationImage was manually constructed.
        if image.destination == 'custom':
            if validate_image(image.data, 'custom').data != image.data:
                raise IapError('Transfer payload must already exclude the bootloader')
        elif (image.destination != 'factory' or not image.stripped_bootloader or
              hashlib.sha256(image.data).hexdigest() != FACTORY_APP_SHA256):
            raise IapError('Invalid application transfer')
        if not image.data or len(image.padded) > APP_BYTES:
            raise IapError('Image exceeds the IAP application region')
        self.used = True  # includes failures; the target might have consumed it
        padded = image.padded
        blocks = len(padded) // BLOCK_BYTES
        checksum = image.checksum
        for opcode, count, field in ((PREPARE, 0, 0), (START, blocks, checksum)):
            self.transport.set_feature(command(opcode, count, field))
            self.sleep(COMMAND_SETTLE_S)
            check_reply(self.transport.get_feature(), opcode, count)
        for index in range(blocks):
            self.transport.set_feature(padded[index*BLOCK_BYTES:(index+1)*BLOCK_BYTES])
            self.sleep(BLOCK_SETTLE_S)
            progress(index+1, blocks)
        self.transport.set_feature(command(QUERY, checksum=checksum))
        self.sleep(COMMAND_SETTLE_S)
        check_reply(self.transport.get_feature(), QUERY, checksum=checksum)
        # GET_REPORT releases the bootloader's result latch. Success erases its
        # boot-request flag and resets; caller must verify application return.
        return checksum
