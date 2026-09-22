#!/usr/bin/env python3
"""Factory defaults stay shared, validated, and consumed by real initializers."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

from firmware_defaults import DEFAULTS as D, HEADER, initializer, parse_defaults

ROOT = HEADER.parents[3]


class DefaultsTests(unittest.TestCase):
    def test_host_consumers(self):
        import keyboard_capture as capture
        import keyboard_gui_model as model
        self.assertEqual(capture.BOTTOM_OUT, D['RAW_BOTTOM_OUT'])
        self.assertEqual(capture.VELOCITY_WINDOW, D['RAW_VELOCITY_WINDOW'])
        self.assertEqual(capture.ASSUMED_SCAN_HZ, D['HUNTSMAN_ASSUMED_SCAN_HZ'])
        self.assertEqual(model.CAPTURE_POINTS, D['CAPTURE_DEFAULT_POINTS'])


    def test_parser_and_tables(self):
        self.assertEqual(parse_defaults('#define A 12u\n#define B -2\n#define C 0x40UL\n'),
                         {'A':12, 'B':-2, 'C':64})
        self.assertEqual(parse_defaults('#define A (2+3)\n'), {})
        with self.assertRaises(ValueError):
            parse_defaults('#define A 1\n#define A 2\n')
        self.assertEqual(len(initializer('DEFAULT_BRIGHTNESS_STEPS')), 20)
        self.assertEqual(len(initializer('DEFAULT_ACTUATION_LEVELS')), 11)
        self.assertEqual(len(initializer('DEFAULT_RAPID_LEVELS')), 11)
        for name in ('DEFAULT_MIDI_NOTE_MAP', 'DEFAULT_JANKO_NOTE_MAP'):
            table = initializer(name)
            self.assertEqual(len({usage for usage, note in table}), len(table))
            self.assertTrue(all(0 <= note < 128 for usage, note in table))

    def compile(self, changes, run=True):
        # Workspace-local scratch also works on machines with a full /tmp.
        scratch = ROOT/'build-defaults-tests'
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            tmp = Path(directory)
            includes = tmp/'include'
            shutil.copytree(HEADER.parent, includes)
            header = (includes/'defaults.h').read_text()
            for name, value in changes.items():
                header, count = re.subn(r'(?m)^(#define '+name+r')[ \t]+[^\n]+',
                                        lambda m: m[1]+' '+str(value), header)
                self.assertEqual(count, 1, name)
            (includes/'defaults.h').write_text(header)
            sources = ['keyboard_raw', 'keyboard_engine', 'keyboard_config', 'keyboard_midi', 'keyboard_menu']
            result = subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-I'+str(includes),
                str(ROOT/'tests/test_defaults.c'),
                *(str(ROOT/'firmware/app/src'/f'{name}.c') for name in sources), '-o', str(tmp/'test')],
                capture_output=True, text=True, env={**os.environ, 'TMPDIR': str(tmp)})
            if run:
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(tmp/'test')], check=True)
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('#error', result.stderr)

    def test_current_initializers(self):
        self.compile({})

    def test_alternate_defaults_reach_initializers(self):
        self.compile({'RAW_DEFAULT_PRESS':2800, 'RAW_DEFAULT_RELEASE':3200,
            'RAW_BOTTOM_OUT':1200, 'RAW_VELOCITY_WINDOW':8, 'DEFAULT_KEYBOARD_ENABLED':0,
            'DEFAULT_ACTUATION_LEVEL':2, 'DEFAULT_RAPID_LEVEL':7, 'DEFAULT_RAPID_ENABLED':0,
            'DEFAULT_PROFILE_LOCKED':1, 'DEFAULT_MIDI_MODE':1, 'DEFAULT_MIDI_JANKO':1,
            'DEFAULT_MIDI_LOWER_MUTED':1, 'DEFAULT_MIDI_ROOT':3, 'DEFAULT_MIDI_SCALE':0,
            'DEFAULT_MIDI_OCTAVE':-2, 'DEFAULT_MIDI_VELOCITY_START':4, 'DEFAULT_BRIGHTNESS_LEVEL':8})

    def test_invalid_defaults_fail_compilation(self):
        for changes in ({'RAW_DEFAULT_PRESS':3600}, {'RAW_DEFAULT_RELEASE':4096},
                        {'RAW_VELOCITY_WINDOW':1}, {'VELOCITY_MAX_COUNTS_PER_SECOND':0},
                        {'MIDI_CONTROL_HOST_MESSAGES':0}, {'MIDI_CONTROL_HOST_ERROR_BYTES':1},
                        {'MIDI_CONTROL_HOST_POLL_MS':0}, {'MIDI_CONTROL_HOST_POLL_MS':500},
                        {'MIDI_CONTROL_HOST_CLOSE_MS':0}, {'MIDI_CONTROL_HOST_REAP_MS':3001},
                        {'GUI_POWER_POLL_MS':0}, {'GUI_POWER_STALE_MS':1000},
                        {'MIDI_WHEEL_RELEASE_RAW':999}, {'CALIBRATION_HOLD_MS':5000},
                        {'M1_LED_LATCH_US':0}, {'M1_LED_TRANSFER_TIMEOUT_US':0},
                        {'M1_MAIN_STACK_BYTES':8191}, {'M1_MAIN_STACK_BYTES':8193},
                        {'M1_MAIN_STACK_BYTES':32776}, {'M1_DEFAULT_WIRELESS_TRANSPORT':3},
                        {'M1_DEFAULT_WIRELESS_TRANSPORT':6},
                        {'M1_FLASH_ERASE_WAIT_LOOPS':0}, {'M1_FLASH_ERASE_WAIT_LOOPS':1000001},
                        {'M1_FLASH_PROGRAM_WAIT_LOOPS':0}, {'M1_FLASH_PROGRAM_WAIT_LOOPS':1000001},
                        {'M1_USB_PHY_SETTLE_US':999}, {'M1_USB_INIT_DELAY_LIMIT_MS':24},
                        {'M1_USB_INIT_DELAY_LIMIT_MS':1001}, {'M1_USB_PHY_SETTLE_US':25001},
                        {'M1_RADIO_START_PULSE_US':0}, {'M1_RADIO_TRANSFER_TIMEOUT_US':0},
                        {'M1_PAIR_HOLD_MS':0}, {'M1_PAIR_HOLD_MS':0x80000000},
                        {'M1_PAIR_SWITCH_TIMEOUT_MS':2999}, {'M1_PAIR_SWITCH_TIMEOUT_MS':0x80000000},
                        {'M1_RADIO_START_PULSE_US':0x80000000},
                        {'M1_RADIO_TRANSFER_TIMEOUT_US':0x80000000},
                        {'M1_WAKE_SCAN_TIMEOUT_US':0}, {'M1_WAKE_SCAN_TIMEOUT_US':0x80000000},
                        {'M1_WAKE_ACQUIRE_FRAMES':0}, {'M1_WAKE_ACQUIRE_FRAMES':256},
                        {'M1_WAKE_REFRESH_FRAMES':0}, {'M1_WAKE_REFRESH_FRAMES':256},
                        {'M1_WAKE_DROP_COUNTS':0}, {'M1_WAKE_DROP_COUNTS':4096},
                        {'M1_RUNTIME_POWER_PERIOD_MS':0}, {'M1_RUNTIME_BT_IDLE_STEPS':65536},
                        {'M1_RUNTIME_HANDOFF_MS':0}, {'M1_RUNTIME_SLEEP_SETTLE_MS':3000},
                        {'M1_RUNTIME_SLEEP_TICKS':65537}, {'M1_RUNTIME_SCAN_SETTLE_US':0},
                        {'M1_RUNTIME_BT_RETAIN_MS':0}, {'M1_RUNTIME_RESTORE_STAGE_MS':3000},
                        {'M1_RUNTIME_RESTORE_SETTLE_MS':0}, {'M1_SOURCE_DEBOUNCE_MS':0},
                        {'M1_SOURCE_DEBOUNCE_MS':5000}, {'M1_SOURCE_TRANSITION_MS':0x80000000}):
            with self.subTest(changes=changes):
                self.compile(changes, run=False)


if __name__ == '__main__':
    unittest.main()
