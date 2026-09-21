"""Decode production C telemetry from both boards with the actual GUI model.

Offline native fixtures only; never opens a device or changes firmware.
"""
import argparse
from pathlib import Path
import subprocess
import unittest

from firmware_defaults import DEFAULTS as D
from keyboard_boards import BOARDS
from keyboard_gui_model import decode

ROOT=Path(__file__).resolve().parents[1]


class Tests(unittest.TestCase):
    def test_c_encoder_python_decoder(self):
        for name,board in BOARDS.items():
            binary=BINARIES[name]
            for profile,count in board.layouts:
                for scenario in range(4):
                    with self.subTest(board=name,profile=profile,scenario=scenario):
                        payload=subprocess.check_output([binary,str(profile),str(scenario)])
                        snapshot=decode(payload,board.target)
                        self.assertEqual(len(payload),1152)
                        self.assertEqual(snapshot.velocity_start,D['DEFAULT_MIDI_VELOCITY_START'])
                        if not scenario:
                            self.assertEqual((snapshot.profile,snapshot.count,snapshot.sequence),(0,0,0))
                            self.assertEqual(snapshot.calibration_selected,255)
                            self.assertEqual(snapshot.storage_slot,255)
                            self.assertEqual(snapshot.calibration_flags,4)
                            continue
                        self.assertEqual((snapshot.profile,snapshot.count),(profile,count))
                        self.assertEqual(snapshot.flags,125)  # enabled, valid, faults, Fn, Janko
                        self.assertEqual((snapshot.sequence,snapshot.revision,snapshot.ack),
                                         (0xffffffff,0x10203040,0x11223344))
                        self.assertEqual((snapshot.scan_errors,snapshot.light_errors),(42,43))
                        self.assertEqual((snapshot.result,snapshot.mode),(2,2))
                        self.assertEqual(snapshot.raw,tuple(3000+i for i in range(count)))
                        self.assertEqual(snapshot.press,tuple(2100+i for i in range(count)))
                        self.assertEqual(snapshot.release,tuple(3700+i for i in range(count)))
                        self.assertEqual(snapshot.velocity,tuple((i%5)/4 for i in range(count)))
                        self.assertEqual(snapshot.captures,tuple(1234+i for i in range(count)))
                        self.assertEqual(snapshot.velocity_state,
                                         tuple(1+6*(i%2)+(8 if i==count-1 else 0) for i in range(count)))
                        self.assertEqual(snapshot.report,b'\x01\x00\x01'+bytes(13))
                        self.assertEqual((snapshot.performance_mode,snapshot.octave,snapshot.midi_cleanup),(1,-3,True))
                        self.assertEqual((snapshot.midi_errors,snapshot.mode_changes),(0x12345678,0x87654321))
                        self.assertEqual((snapshot.calibration_state,snapshot.calibration_completed,
                                          snapshot.calibration_selected),(3,1,count-1))
                        self.assertEqual(snapshot.calibration_done,(True,)+(False,)*(count-1))
                        self.assertEqual((snapshot.calibration_upper,snapshot.calibration_lower),(4000,1000))
                        self.assertEqual(snapshot.calibration_hold,D['CALIBRATION_HOLD_MS'] if scenario==2 else 41)
                        self.assertEqual(snapshot.calibration_idle,0 if scenario==2 else D['CALIBRATION_IDLE_MS']-31)
                        self.assertEqual(snapshot.calibration_flags,1 if scenario==3 else 7)
                        self.assertEqual(snapshot.storage_flags,0 if scenario==3 else 7)
                        self.assertEqual(snapshot.storage_slot,255 if scenario==3 else 1)
                        self.assertEqual(snapshot.storage_generation,0 if scenario==3 else 0xabcd)
                        self.assertEqual(snapshot.calibration_generation,0 if scenario==3 else 0xfedcba98)
                        self.assertEqual(snapshot.calibration_error,0 if scenario==3 else 73)
                        if profile==board.graphical_profile:
                            fn=board.labels().index('Fn')
                            self.assertEqual(snapshot.down,tuple(i in (fn,count-1) for i in range(count)))
                            self.assertEqual(snapshot.midi_mapping[fn],255)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--huntsman',default=ROOT/'build-host/telemetry_huntsman')
    parser.add_argument('--fun60',default=ROOT/'build-host/telemetry_fun60')
    args,remaining=parser.parse_known_args()
    BINARIES={'RZ03-0499':args.huntsman,'monsgeek_fun60_pro_wired':args.fun60}
    unittest.main(argv=[__file__,*remaining])
