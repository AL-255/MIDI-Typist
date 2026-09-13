"""HBD1 decoder for offline flash-controller tests; no device transport."""
import struct
import zlib
SIZE = 128
CHUNK = 64

def decode(packet, request_id, address):
    if len(packet) != SIZE or packet[:4] != b'HBD1':
        raise ValueError('Invalid dump frame')
    if zlib.crc32(packet[:124]) != struct.unpack_from('<I',packet,124)[0]:
        raise ValueError('Dump CRC mismatch')
    ident,addr,length,total,page,status = struct.unpack_from('<6I',packet,4)
    if (ident,addr,length) != (request_id,address,CHUNK):
        raise ValueError('Stale, misaddressed or wrong-length dump response')
    if status:
        part,die=struct.unpack_from('<2I',packet,112)
        raise ValueError(f'Flash request failure: status {status}, size=0x{total:x}, part=0x{part:x}, die=0x{die:x}')
    if page != 512 or total < addr+CHUNK:
        raise ValueError('Unexpected flash geometry')
    statuses = struct.unpack_from('<4I',packet,32)
    data = packet[48:112]
    for i,status in enumerate(statuses):
        if status and any(data[i*16:i*16+16]):
            raise ValueError('Nonzero error placeholder')
    return data,statuses,total,page
