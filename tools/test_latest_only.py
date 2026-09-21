"""Guard the current-only repository contract; no device access."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent.parent


class LatestOnlyTests(unittest.TestCase):
    def test_presets(self):
        presets = json.loads((ROOT/'CMakePresets.json').read_text())
        for group in ('configurePresets', 'buildPresets'):
            self.assertEqual({p['name'] for p in presets[group]},
                             {'huntsman', 'host-tests', 'simulator'})

    def test_obsolete_entry_points_absent(self):
        obsolete = ('decode_scan_stream', 'dump_flash', 'flash_application',
                    'last_key_stream', 'scan_bars', 'test_keyboard_console_arm')
        for name in obsolete:
            self.assertFalse((ROOT/'tools'/f'{name}.py').exists(), name)
        board = ROOT/'firmware/boards/huntsman_v3_pro_mini'
        for name in ('main', 'main_usb', 'optical_hw', 'optical_scan',
                     'lighting', 'keyboard_console', 'calibration_store'):
            self.assertFalse((board/'src'/f'{name}.c').exists(), name)
        platform = ROOT/'firmware/platform/nxp_lpc55'
        self.assertFalse((platform/'src/debug_rx.c').exists())
        for source in (ROOT/'firmware').rglob('*'):
            if source.suffix not in ('.c', '.h', '.cmake'):
                continue
            text = source.read_text()
            for obsolete in ('HUNTSMAN_USB_ONLY', 'HUNTSMAN_KEYBOARD_DIAGNOSTICS',
                             'HUNTSMAN_TRAVEL_LIGHTING', 'HUNTSMAN_KEYBOARD_MODE',
                             'calibration_record_valid', 'HKC1'):
                self.assertNotIn(obsolete, text, str(source))

    def test_capture_rejects_prefix_and_foreign_session(self):
        import struct
        from keyboard_capture import KeyDecoder, StreamError
        packet = struct.pack('<4sIIHBBH', b'HKL1', 42, 0, 3000, 5, 1, 3500)
        packet += struct.pack('<H', sum(struct.unpack('<9H', packet)) & 65535)
        self.assertEqual(list(KeyDecoder(3500, 42,65).feed(packet)), [3000])
        for data, session in ((b'junk'+packet, 42), (packet, 43)):
            with self.assertRaises(StreamError):
                list(KeyDecoder(3500, session,65).feed(data))

    def test_only_current_host_profile(self):
        from keyboard_gui_model import validate_profile
        for version in (0, 1, 2, 3, 5):
            with self.assertRaisesRegex(ValueError, 'version 4'):
                validate_profile({'version':version, 'layout':'ansi', 'keys':[]})


if __name__ == '__main__':
    unittest.main()
