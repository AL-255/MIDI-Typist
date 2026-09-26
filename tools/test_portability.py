#!/usr/bin/env python3
"""Keep hardware headers and address-space assumptions out of the application."""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'firmware/app'
SERVICES=ROOT/'firmware/services'

class PortabilityTests(unittest.TestCase):
    def test_includes_are_self_contained(self):
        standard={'stdbool.h','stdint.h','stddef.h','string.h'}
        for file in (*APP.glob('src/*.c'),*APP.glob('include/*.h'),*SERVICES.glob('src/*.c'),*SERVICES.glob('include/*.h')):
            for delimiter,name in re.findall(r'^\s*#include\s+([<"])([^>"]+)',file.read_text(),re.M):
                with self.subTest(file=file.name,header=name):
                    if delimiter=='<': self.assertIn(name,standard)
                    else: self.assertTrue((APP/'include'/name).is_file() or
                                          file.is_relative_to(SERVICES) and (SERVICES/'include'/name).is_file(),name)

    def test_no_hardware_dependencies_in_shared_code(self):
        forbidden=r'\b(?:KEY_ID_[A-Z_]+|g_lighting_channels|g_keyboard_grid|OPT_SCAN_READ|CAL_SLOT_[AB]|__WFI|__disable_irq|NVIC_SystemReset|USB_DeviceInit|FSL_[A-Z_]+)\b'
        for file in (*APP.glob('src/*.c'),*APP.glob('include/*.h'),*SERVICES.glob('src/*.c'),*SERVICES.glob('include/*.h')):
            source=re.sub(r'/\*.*?\*/|//[^\n]*','',file.read_text(),flags=re.S)
            self.assertIsNone(re.search(forbidden,source),str(file))

    def test_selectable_ports(self):
        cmake=(ROOT/'CMakeLists.txt').read_text()
        self.assertIn('MT_BOARD',cmake)
        self.assertNotIn('third_party/nxp',cmake)
        self.assertTrue((ROOT/'firmware/boards/synthetic/board.cmake').is_file())
        self.assertTrue((ROOT/'firmware/boards/huntsman_v3_pro_mini/board.cmake').is_file())

    def test_board_keyboard_mapping_configs(self):
        # Every supported physical namespace has an explicit config, separate
        # from sensor wiring, physical labels and fixed Fn actions.
        for board,fn,count in (('huntsman_v3_pro_mini',0x3b,135),
                               ('monsgeek_m1_v5_tmr',78,82),('synthetic',20,104)):
            directory=ROOT/'firmware/boards'/board
            source=(directory/'config/keymap.def').read_text()
            records=[]
            for line in source.splitlines():
                if not line.startswith('KEYMAP('):continue
                match=re.fullmatch(r'KEYMAP\((0x[0-9a-f]+), (0x[0-9a-f]+)\)',line)
                self.assertIsNotNone(match,line)
                records.append(tuple(int(x,16) for x in match.groups()))
            self.assertEqual(len(records),count,board)
            self.assertEqual(len(dict(records)),count,board)
            self.assertEqual(dict(records)[fn],0,board)
            self.assertTrue(all(0<=key<256 and (usage==0 or 4<=usage<=0xe7)
                                for key,usage in records),board)
            self.assertTrue(any('../config/keymap.def' in f.read_text()
                                for f in (directory/'src').glob('*.c')),board)

if __name__=='__main__': unittest.main()
