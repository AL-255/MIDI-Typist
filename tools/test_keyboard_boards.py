"""Offline multi-board geometry/profile tests; no device access."""
from dataclasses import replace
import struct
import unittest
from keyboard_boards import boards, get_board, m1_records, M1_TARGET, DEFAULT_TARGET
from keyboard_gui_model import profile_from_snapshot, validate_profile, MIDI_CONTROLS, decode, frame_size
from keyboard_capture import KeyDecoder, StreamError, press_velocity
from test_keyboard_gui import packet


class Tests(unittest.TestCase):
    def test_variable_wire_sizes_and_board_binding(self):
        for count,hid in ((61,30),(62,30),(65,30),(82,30),(128,32)):
            wire=packet(count=count,hid_bytes=hid)
            snapshot=decode(wire)
            self.assertEqual(len(wire),frame_size(count,hid))
            self.assertEqual(len(snapshot.raw),count)
            self.assertEqual(len(snapshot.calibration_done),count)
            self.assertEqual(len(snapshot.report),hid)
            self.assertEqual(snapshot.sample_hz,8000)
        self.assertEqual(frame_size(128,32),2292)
        m1=decode(packet(count=82,hid_bytes=30))
        board=get_board(M1_TARGET)
        self.assertTrue(board.validates_wire(m1))
        for field,value in (('profile',2),('count',81),('report',bytes(16)),('sample_hz',2000)):
            self.assertFalse(board.validates_wire(replace(m1,**{field:value})))
        idle=decode(packet(count=0,profile=0,hid_bytes=30,sample_hz=0,flags=1))
        self.assertTrue(board.validates_wire(idle))
        wire=bytearray(packet(count=81,hid_bytes=30))
        wire[-5]=1  # final alignment pad, not a key field
        struct.pack_into('<I',wire,len(wire)-4,sum(struct.unpack_from(f'<{(len(wire)-4)//2}H',wire)))
        with self.assertRaisesRegex(ValueError,'padding'):decode(wire)

    def test_last_m1_key_capture_and_board_rate(self):
        wire=bytearray(struct.pack('<4sIIHBBHH',b'HKL1',42,0,3000,81,1,3500,0))
        struct.pack_into('<H',wire,18,sum(struct.unpack_from('<9H',wire)) & 65535)
        self.assertEqual(list(KeyDecoder(3500,42,82).feed(wire)),[3000])
        with self.assertRaisesRegex(StreamError,'key index'):
            list(KeyDecoder(3500,42,81).feed(wire))
        self.assertEqual(press_velocity((3500,3400,3300,3200,3100),2000),200000)
        with self.assertRaises(ValueError):press_velocity((3500,3400),0)

    def test_complete_nonoverlapping_geometry(self):
        for board in boards().values():
            with self.subTest(board=board.target):
                self.assertEqual(sorted(k.sensor for k in board.keys),list(range(board.count)))
                self.assertEqual(len(set(board.labels())),board.count)
                for i,a in enumerate(board.keys):
                    for b in board.keys[i+1:]:
                        self.assertFalse(a.x<b.x+b.width and b.x<a.x+a.width and a.y<b.y+1 and b.y<a.y+1)
        self.assertEqual(get_board(M1_TARGET).count,82)

    def test_m1_wiring_and_controls(self):
        records={r[-1]:r for r in m1_records()}
        for name,bank,rank,usage in [('Fn',5,10,0),('A',3,1,4),('LGu',5,1,0xe3),
                                    ('LAl',5,2,0xe2),('RAl',5,9,0xe6),('Ent',3,13,0x28),
                                    ('Up',4,13,0x52),('Right',5,14,0x4f)]:
            self.assertEqual(records[name][1:4],(bank,rank,usage))
        for i in range(1,13): self.assertEqual(records[f'F{i}'][3],0x39+i)
        self.assertEqual(set(range(6*15))-{r[2]*6+r[1] for r in records.values()},
                         {10,23,29,35,47,53,75,84})

    def test_profiles_bind_target_and_full_sensor_set(self):
        snapshot=decode(packet())
        for board in boards().values():
            labels=board.labels()
            current=replace(snapshot,profile=board.profile,count=board.count,
                            press=(3500,)*board.count,release=(3600,)*board.count,
                            keyboard_mapping=board.default_keycodes(),
                            midi_mapping=tuple(255 if s in MIDI_CONTROLS else 60 for s in labels))
            data=profile_from_snapshot(current,board.target)
            self.assertEqual(len(validate_profile(data,board.target)),board.count)
            self.assertEqual(data['target'],board.target)
            other=M1_TARGET if board.target==DEFAULT_TARGET else DEFAULT_TARGET
            with self.assertRaisesRegex(ValueError,'different keyboard'):
                validate_profile(data,other)
            with self.assertRaisesRegex(ValueError,'version 4'):
                validate_profile({**data,'version':2},board.target)
            with self.assertRaises(ValueError):
                validate_profile({**data,'keys':data['keys'][:-1]},board.target)
        with self.assertRaises(ValueError):get_board('unknown')


if __name__=='__main__':unittest.main()
