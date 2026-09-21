"""Offline M1 identity/safety checks. No real HID or USB access."""
from dataclasses import replace
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import flash_monsgeek as m1
from flash_models import adapters


def reply(model=m1.MODEL_ID, version=0x0408):
    data = bytearray(m1.FEATURE_BYTES)
    data[1] = m1.IDENTITY_COMMAND
    struct.pack_into('<H', data, 2, model)
    struct.pack_into('<H', data, 8, version)
    return data


class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.usb = self.root / 'usb'
        self.usb.mkdir()
        self.hidraw = self.root / 'hidraw'
        self.hidraw.mkdir()
        self.dev = self.root / 'dev'
        self.adapter = m1.MonsGeekAdapter(self.usb, self.hidraw, self.dev)

    def device(self, location='3-2.1', usb_id='3151:5030', interface=2):
        path = self.usb / location
        path.mkdir(exist_ok=True)
        values = {'idVendor':usb_id[:4], 'idProduct':usb_id[5:], 'busnum':'3',
                  'devnum':'22', 'speed':'480', 'bcdDevice':'0408', 'product':'MonsGeek Keyboard'}
        for key, value in values.items():
            (path / key).write_text(value)
        iface = path / f'{location}:1.{interface}'
        iface.mkdir(exist_ok=True)
        (iface / 'bInterfaceNumber').write_text(f'{interface:02x}')
        (iface / 'bInterfaceClass').write_text('03')
        hid = iface / '0003:3151:5030.0001'
        hid.mkdir(exist_ok=True)
        entry = self.hidraw / f'hidraw{len(list(self.hidraw.iterdir()))}'
        entry.mkdir()
        (entry / 'device').symlink_to(hid)
        return path

    def test_registry_has_no_fun60(self):
        self.assertEqual(set(adapters()), {'razer-huntsman-v3-pro-mini', self.adapter.id})

    def test_request_only_read_identity(self):
        request = m1.identity_request()
        self.assertEqual(len(request), 65)
        self.assertEqual(request[0], 0)
        self.assertEqual(request[1], 0x8f)
        self.assertEqual(sum(request[1:9]) & 255, 255)
        self.assertEqual([i for i, v in enumerate(request) if v], [1, 8])
        self.assertEqual(m1.parse_identity(reply()), 'v4.08')
        self.assertEqual(m1.parse_identity(reply(version=0x0410)), 'v4.10')

    def test_bad_replies(self):
        for data in (reply()[:-1], reply() + b'\0', bytes(65),
                     reply(model=2950), reply(version=0x04af), reply(version=0)):
            with self.subTest(data=bytes(data[:10])), self.assertRaises(ValueError):
                m1.parse_identity(data)
        data = reply(); data[0] = 1
        with self.assertRaises(ValueError): m1.parse_identity(data)

    def test_passive_discovery_no_assumed_model_or_firmware(self):
        self.device()
        self.device('3-3', '3151:5038')  # wireless receiver is not the wired keyboard
        device, = self.adapter.discover()
        self.assertEqual(device.mode, 'candidate')
        self.assertEqual(device.version, 'Not queried')
        self.assertIn('Not exposed', device.serial)
        with patch('os.open', side_effect=AssertionError('Passive refresh opened hardware')):
            self.assertEqual(self.adapter.identify(device, 'wrong-board-build'), device)

    def test_changed_and_multiple_targets(self):
        path = self.device()
        device, = self.adapter.discover()
        (path / 'devnum').write_text('23')
        with self.assertRaisesRegex(RuntimeError, 'changed'): self.adapter.selected(device.token)
        self.device('3-3')
        with self.assertRaisesRegex(RuntimeError, 'Exactly one'): self.adapter.selected(device.token)

    def test_vendor_interface_only_and_ambiguity(self):
        self.device(interface=0)
        device, = self.adapter.discover()
        with self.assertRaisesRegex(RuntimeError, 'vendor HID'): self.adapter.vendor_node(device)
        self.device(interface=2)
        self.assertEqual(self.adapter.vendor_node(device), self.dev / 'hidraw1')
        self.device(interface=2)
        with self.assertRaisesRegex(RuntimeError, 'vendor HID'): self.adapter.vendor_node(device)

    def transact(self, data=None, short=None, bus=3, vid=0x3151, pid=0x5030, error=None):
        device, = self.adapter.discover()
        calls = []
        def ioctl(fd, command, buffer, mutate):
            calls.append(command)
            self.assertEqual(fd, 91)
            self.assertTrue(mutate)
            if command == m1.HIDIOCGRAWINFO:
                buffer[:] = struct.pack('=IHH', bus, vid, pid)
                return 0
            if command == m1.HIDIOCSFEATURE:
                self.assertEqual(buffer, m1.identity_request())
            elif command == m1.HIDIOCGFEATURE:
                if error: raise error
                buffer[:] = reply() if data is None else data
            else:
                self.fail('Unexpected hardware command')
            return 64 if short == command else 65
        with patch('os.open', return_value=91) as opened, patch('os.close') as closed, \
                patch('fcntl.ioctl', side_effect=ioctl), patch('flash_monsgeek.time.sleep'):
            try:
                result = self.adapter.inspect(device.token)
                self.assertEqual(result.mode, 'factory')
                self.assertEqual(result.version, 'v4.08')
                self.assertEqual(result.token, device.token)
                return result
            finally:
                opened.assert_called_once_with(self.dev / 'hidraw0',
                    m1.os.O_RDWR | m1.os.O_CLOEXEC | m1.os.O_NOFOLLOW)
                closed.assert_called_once_with(91)
                self.assertEqual(calls, [m1.HIDIOCGRAWINFO, m1.HIDIOCSFEATURE, m1.HIDIOCGFEATURE][:len(calls)])
                self.assertLessEqual(len(calls), 3)  # no retries

    def test_identity_transaction(self):
        self.device()
        self.transact()

    def test_reference_alternate_identity(self):
        self.device(usb_id='38ee:0033')
        self.transact(vid=0x38ee, pid=0x0033)

    def test_reenumeration_during_query_rejects_reply(self):
        self.device()
        selected = self.adapter.selected
        count = 0
        def checked(token):
            nonlocal count
            count += 1
            if count == 3:
                (self.usb / '3-2.1' / 'devnum').write_text('23')
            return selected(token)
        with patch.object(self.adapter, 'selected', side_effect=checked):
            with self.assertRaisesRegex(RuntimeError, 'changed'):
                self.transact()

    def test_short_transfers_and_io_errors_close_fd(self):
        self.device()
        for command in (m1.HIDIOCSFEATURE, m1.HIDIOCGFEATURE):
            with self.assertRaisesRegex(RuntimeError, 'Incomplete'):
                self.transact(short=command)
        with self.assertRaises(OSError): self.transact(error=OSError('disconnected'))
        with self.assertRaisesRegex(ValueError, 'unsupported'): self.transact(data=reply(model=1))

    def test_mismatched_open_device_cannot_receive_query(self):
        self.device()
        for args in ({'bus':5}, {'pid':0x5029}, {'vid':0x1532}):
            with self.assertRaisesRegex(RuntimeError, 'does not match'):
                self.transact(**args)

    def test_bootloader_never_receives_query(self):
        self.device(usb_id='3151:502a')
        device, = self.adapter.discover()
        self.assertEqual(device.mode, 'unverified_bootloader')
        with patch('os.open', side_effect=AssertionError('Opened bootloader')):
            with self.assertRaisesRegex(RuntimeError, 'no query'):
                self.adapter.inspect(device.token)

    def test_every_flash_entry_point_rejects_without_io(self):
        self.device()
        device, = self.adapter.discover()
        with patch('os.open', side_effect=AssertionError('Opened hardware')):
            for mode in ('candidate', 'factory', 'custom', 'bootloader', 'unverified_bootloader'):
                self.assertEqual(self.adapter.actions(replace(device, mode=mode)), ())
            with self.assertRaisesRegex(RuntimeError, 'no file'):
                self.adapter.load_image('/missing/firmware.bin', 'custom')
            with self.assertRaisesRegex(RuntimeError, 'disabled'):
                self.adapter.flash(device.token, 'install', '/missing/firmware.bin', '', None, None)


if __name__ == '__main__':
    unittest.main()
