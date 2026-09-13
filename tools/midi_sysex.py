"""Version-one MIDI-Typist experimental SysEx envelope (matching midi_sysex.h)."""
import struct
import zlib

HELLO, READY, COMMAND, ACK, SNAPSHOT, SAMPLES, LOG, ERROR, DUMP, KEEPALIVE, CLOSE = range(1, 12)
PREFIX = b'\xf0\x7dMT\x01'
MAX_PAYLOAD = 1152


def encode(kind, session, sequence=0, payload=b''):
    if not 1 <= kind <= CLOSE or not 1 <= session <= 0xffffffff or len(payload) > MAX_PAYLOAD:
        raise ValueError('invalid SysEx envelope')
    prefix = PREFIX + bytes([kind])
    body = struct.pack('<IIH', session, sequence, len(payload)) + payload
    body += struct.pack('<I', zlib.crc32(prefix[1:] + body))
    result = bytearray(prefix)
    for at in range(0, len(body), 7):
        group = body[at:at+7]
        result.append(sum((byte >> 7) << i for i, byte in enumerate(group)))
        result.extend(byte & 127 for byte in group)
    return bytes(result) + b'\xf7'


def decode(wire):
    wire = bytes(wire)
    if len(wire) < 23 or len(wire) > 1340 or wire[:5] != PREFIX or wire[-1] != 0xf7:
        raise ValueError('invalid SysEx framing')
    if any(byte & 128 for byte in wire[1:-1]) or not 1 <= wire[5] <= CLOSE:
        raise ValueError('invalid SysEx data')
    body = bytearray()
    for at in range(6, len(wire)-1, 8):
        group = wire[at:min(at+8, len(wire)-1)]
        if len(group) < 2 or group[0] >> (len(group)-1):
            raise ValueError('noncanonical SysEx packing')
        body.extend(byte | (((group[0] >> i) & 1) << 7) for i, byte in enumerate(group[1:]))
    if len(body) < 14:
        raise ValueError('short SysEx body')
    session, sequence, length = struct.unpack_from('<IIH', body)
    if not session or length > MAX_PAYLOAD or len(body) != length+14:
        raise ValueError('invalid SysEx length/session')
    if zlib.crc32(wire[1:6]+body[:-4]) != struct.unpack_from('<I', body, len(body)-4)[0]:
        raise ValueError('SysEx CRC mismatch')
    return wire[5], session, sequence, bytes(body[10:-4])


def usb_events(wire, cable=1):
    result = bytearray()
    for at in range(0, len(wire), 3):
        group = wire[at:at+3]
        cin = 4+len(group) if at+3 >= len(wire) else 4
        result.extend(bytes([(cable << 4) | cin])+group+bytes(3-len(group)))
    return bytes(result)
