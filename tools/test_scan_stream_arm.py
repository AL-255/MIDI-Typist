#!/usr/bin/env python3
"""Offline SysEx capture tests; synthetic input rate, not a physical measurement."""
import argparse
import struct
from test_midi_control_arm import MidiControlArm
from test_optical_bus_arm import ScanArm
from keyboard_capture import KeyDecoder, StreamError
from keyboard_gui_model import Decoder
from test_keyboard_gui import packet

def drain(dev, kind=None):
    start = len(dev.peer.messages)
    data = dev.drain()
    # The outer SysEx kind separates immutable in-flight snapshots/captures.
    # Do not ask payload decoders to resynchronize over another message kind.
    return data if kind is None else b''.join(m[3] for m in dev.peer.messages[start:] if m[0] == kind)

def key_push(dev, values, profile=1):
    dev.cpu.mem_write(0x2003d000, struct.pack('<'+'H'*len(values), *values))
    dev.call('scan_stream_push', 0x2003d000, len(values), profile, 0)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    for speed in (False, True):
        dev = MidiControlArm(args.elf, speed)
        dev.call('scan_stream_init')
        dev.call('scan_stream_last_key', 3800, 123, 5)
        decoder = KeyDecoder(3800, 123,65)
        for batch in range(250):
            for index in range(32):
                raw = [3900]*61; raw[5] = 1000+index
                key_push(dev, raw)
            assert list(decoder.feed(drain(dev))) == list(range(1000,1032))
        decoder.finish()
        dev.call('scan_stream_last_key', 3800, 124, 5)
        for _ in range(257): key_push(dev, [3700]*61)
        try: list(KeyDecoder(3800,124,65).feed(drain(dev)))
        except StreamError as error: assert 'overflow' in str(error)
        else: raise AssertionError('silent capture loss')
        dev.call('scan_stream_last_key', 3800, 125, 64)
        key_push(dev, [3700]*61)
        try: list(KeyDecoder(3800,125,65).feed(drain(dev)))
        except StreamError as error: assert 'invalid' in str(error)
        else: raise AssertionError('invalid sensor accepted')
        dev.call('scan_stream_gui')
        for sequence in range(100):
            dev.cpu.mem_write(0x2003d000, packet(sequence=sequence))
            dev.call('scan_stream_gui_push', 0x2003d000,len(packet(sequence=sequence)))
        assert [v.sequence for v in Decoder().feed(drain(dev))] == [99]
        print(f'PASS {"HS" if speed else "FS"}: 8000 lossless samples, explicit overflow, invalid sensor, latest-only snapshots')
    live = ScanArm(args.elf,args.reference)
    live.service(400)
    data = live.command('stream key 3800 456 4')
    live.output.clear(); live.service(30); data += bytes(live.output)
    values = list(KeyDecoder(3800,456,65).feed(data))
    assert values and all(value == live.raw[4] for value in values)
    print('PASS SysEx command -> optical DMA -> pinned sample readback')

if __name__ == '__main__': main()
