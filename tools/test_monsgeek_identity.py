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

    def test_discovery_skips_incomplete_or_torn_enumeration(self):
        path=self.device()
        (path/'devnum').unlink()
        self.assertEqual(self.adapter.discover(),[])
        (path/'devnum').write_text('22')
        read=Path.read_text
        count=0
        def changing(file,*args,**kwargs):
            nonlocal count
            if file==path/'devnum':
                count+=1
                if count==2:return '23'
            return read(file,*args,**kwargs)
        with patch.object(Path,'read_text',changing):
            self.assertEqual(self.adapter.discover(),[])

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
            if count == 4:
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

    def test_only_verified_applications_have_conversion_actions(self):
        self.device()
        device, = self.adapter.discover()
        with patch('os.open', side_effect=AssertionError('Opened hardware')):
            for mode in ('candidate', 'custom_candidate', 'bootloader', 'unverified_bootloader'):
                self.assertEqual(self.adapter.actions(replace(device, mode=mode)), ())
            self.assertEqual([a.id for a in self.adapter.actions(replace(device,mode='factory'))],
                             ['install','restore'])
            self.assertEqual([a.id for a in self.adapter.actions(replace(device,mode='custom'))],
                             ['reflash','restore'])
            with self.assertRaises(FileNotFoundError):
                self.adapter.flash(device.token, 'install', '/missing/firmware.bin', '', None, None)

    def test_guarded_boot_entry_packet(self):
        request=m1.boot_request()
        self.assertEqual(len(request),65)
        self.assertEqual(request[:6],b'\0\x7f\x55\xaa\x55\xaa')
        self.assertEqual(sum(request[1:9])&255,255)
        self.assertEqual(request[9:],bytes(56))

    def custom_device(self):
        path=self.device()
        (path/'manufacturer').write_text('MIDI-Typist')
        (path/'product').write_text('M1 V5 TMR')
        device,=self.adapter.discover()
        self.assertEqual(device.mode,'custom_candidate')
        return device

    def test_custom_identity_and_single_boot_request(self):
        import midi_sysex as sx
        device=self.custom_device()
        build='v0.1.0-MG-M1V5TMR git='+'a'*40+' state=clean'
        class Peer:
            def __init__(self,port):self.sent=[];self.closed=False
            def send(self,wire):self.sent.append(sx.decode(wire))
            def receive(self,timeout):
                return sx.encode(sx.READY,self.sent[0][1],0,('build='+build).encode())
            def close(self):self.closed=True
        for boot in (False,True):
            peer=Peer('control')
            with patch('midi_backend.control_port_for_usb',return_value='control'), \
                 patch('midi_backend.MidiBackend',return_value=peer), \
                 patch('os.open',side_effect=AssertionError('Custom M1 opened HID')):
                with self.adapter.custom_session(device.token,enter_boot=boot) as confirmed:
                    self.assertEqual(confirmed.mode,'custom')
                    self.assertEqual(confirmed.version,build)
            self.assertTrue(peer.closed)
            self.assertEqual([entry[0] for entry in peer.sent],
                             [sx.HELLO,sx.COMMAND if boot else sx.CLOSE])
            if boot:self.assertEqual(peer.sent[-1][2:],(1,b'bootloader'))

    def test_native_owner_release_failure_is_not_ignored_after_boot_request(self):
        import midi_sysex as sx
        device=self.custom_device();sent=[]
        class Peer:
            def send(self,wire):sent.append(sx.decode(wire))
            def receive(self,timeout):
                return sx.encode(sx.READY,sent[0][1],0,
                    ('build=v0.1.0-MG-M1V5TMR git='+'a'*40+' state=clean').encode())
            def close(self):raise OSError('native owner not released')
        with patch('midi_backend.control_port_for_usb',return_value='control'), \
             patch('midi_backend.MidiBackend',return_value=Peer()):
            with self.assertRaisesRegex(OSError,'not released'):
                with self.adapter.custom_session(device.token,enter_boot=True):pass
        self.assertEqual([entry[0] for entry in sent],[sx.HELLO,sx.COMMAND])
        self.assertEqual(sent[-1][3],b'bootloader') # one request, no cancellation/retry

    def test_wrong_build_or_changed_midi_binding_never_enters_bootloader(self):
        import midi_sysex as sx
        device=self.custom_device()
        for wrong_target,ports in ((True,['control']*3),(False,['control','other']),
                                   (False,['control','control','other'])):
            messages=[]
            class Peer:
                def send(self,wire):messages.append(sx.decode(wire))
                def receive(self,timeout):
                    target='RZ03-0499' if wrong_target else 'MG-M1V5TMR'
                    return sx.encode(sx.READY,messages[0][1],0,
                        ('build=v0.1.0-'+target+' git='+'a'*40+' state=clean').encode())
                def close(self):pass
            with patch('midi_backend.control_port_for_usb',side_effect=ports), \
                 patch('midi_backend.MidiBackend',return_value=Peer()):
                with self.assertRaises(RuntimeError):
                    with self.adapter.custom_session(device.token,enter_boot=True):pass
            self.assertFalse(any(m[0]==sx.COMMAND for m in messages))


if __name__ == '__main__':
    unittest.main()
