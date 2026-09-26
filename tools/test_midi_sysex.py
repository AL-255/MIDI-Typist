"""Cross-language framing tests; no physical device access."""
import ctypes as c
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
import midi_sysex as sx

ROOT = Path(__file__).resolve().parents[1]

class Info(c.Structure):
    _fields_ = [('session',c.c_uint32),('sequence',c.c_uint32),('length',c.c_uint16),('kind',c.c_uint8)]

class Tests(unittest.TestCase):
    def test_cross_language_and_corruption(self):
        with tempfile.TemporaryDirectory() as folder:
            library = Path(folder)/'codec.so'
            subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'firmware/app/include'),str(ROOT/'firmware/app/src/midi_sysex.c'),
                            '-o',str(library)],check=True)
            codec = c.CDLL(str(library))
            codec.midi_sysex_encode.argtypes = [c.c_uint8,c.c_uint32,c.c_uint32,c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t]
            codec.midi_sysex_encode.restype = c.c_size_t
            codec.midi_sysex_decode.argtypes = [c.c_void_p,c.c_size_t,c.POINTER(Info),c.c_void_p,c.c_size_t]
            codec.midi_sysex_decode.restype = c.c_bool
            rng = random.Random(42)
            for length in range(sx.MAX_PAYLOAD+1):
                payload = rng.randbytes(length)
                wire = sx.encode(sx.SNAPSHOT,0xfedcba98,0x87654321,payload)
                output = c.create_string_buffer(sx.MAX_WIRE)
                size = codec.midi_sysex_encode(sx.SNAPSHOT,0xfedcba98,0x87654321,payload,length,output,len(output))
                self.assertEqual(output.raw[:size],wire)
                info = Info(); decoded = c.create_string_buffer(sx.MAX_PAYLOAD)
                self.assertTrue(codec.midi_sysex_decode(wire,len(wire),c.byref(info),decoded,len(decoded)))
                self.assertEqual((info.kind,info.session,info.sequence,decoded.raw[:info.length]),sx.decode(wire))
                damaged = bytearray(wire); damaged[-2] ^= 1
                self.assertFalse(codec.midi_sysex_decode(bytes(damaged),len(damaged),c.byref(info),decoded,len(decoded)))
                with self.assertRaises(ValueError): sx.decode(damaged)
            for length in range(23):
                with self.assertRaises(ValueError): sx.decode(wire[:length])

if __name__ == '__main__': unittest.main()
