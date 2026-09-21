"""Offline device-adapter and firmware-plan tests; never opens real USB."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from flash_huntsman import HuntsmanAdapter
from flash_fun60 import Fun60Adapter
from flash_models import adapters, matching_build
from firmware_flasher import FlasherError, ImageError


def usb(root, mode='custom', name='1-2'):
    path=root/name;path.mkdir(exist_ok=True)
    values={'idVendor':'1532','idProduct':'110e' if mode=='bootloader' else '02b0',
            'manufacturer':'OpenHuntsman' if mode=='custom' else 'Razer',
            'product':'Test keyboard','busnum':'1','devnum':'2','serial':'TEST-SERIAL',
            'speed':'480','bcdDevice':'0201','bNumInterfaces':'4'}
    for key,value in values.items():(path/key).write_text(value)
    return path


def image(path, custom=True):
    data=bytearray(b'\xff'*131072);struct.pack_into('<II',data,0,0x04008000,0x20000101)
    if custom:data[64:64+len(b'MIDI-Typist SysEx control ready RZ03-0499')]=b'MIDI-Typist SysEx control ready RZ03-0499'
    path.write_bytes(data);return bytes(data)


class Tests(unittest.TestCase):
    def test_multi_board_build_identity(self):
        registry=adapters()
        self.assertEqual(set(registry),{HuntsmanAdapter.id,Fun60Adapter.id})
        for target in (HuntsmanAdapter.build_target,Fun60Adapter.build_target):
            build='v0.1.0-'+target+' git='+'a'*40+' state=dirty'
            self.assertEqual(matching_build(build,target),build)
            self.assertIsNone(matching_build(build,'wrong_board'))
        for bad in ('v0.1.0','unknown',None,'v0.1.0-RZ03-0499 git=unknown state=clean'):
            self.assertIsNone(matching_build(bad,HuntsmanAdapter.build_target))

    def test_fun60_read_only_inventory_and_gates(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);adapter=Fun60Adapter(root)
            path=usb(root)
            (path/'idVendor').write_text('3151')
            for pid in ('5029','502f','5038'):
                (path/'idProduct').write_text(pid)
                self.assertEqual(adapter.discover(),[])
            (path/'idProduct').write_text('502d')
            device=adapter.discover()[0]
            self.assertEqual(device.mode,'unknown')
            self.assertIn('Not queried',device.version)
            self.assertEqual(adapter.actions(device),())
            self.assertEqual(adapter.inspect(device.token),device)
            (path/'manufacturer').write_text('MIDI-Typist')
            (path/'product').write_text('FUN60 PRO MIDI-Typist')
            device=adapter.discover()[0]
            self.assertEqual(device.mode,'custom')
            wrong='v0.1.0-RZ03-0499 git='+'b'*40+' state=clean'
            self.assertEqual(adapter.identify(device,wrong),device)
            build='v0.1.0-'+adapter.build_target+' git='+'a'*40+' state=dirty'
            self.assertEqual(adapter.identify(device,build).version,build)
            (path/'devnum').write_text('4')
            with self.assertRaisesRegex(RuntimeError,'changed'):adapter.inspect(device.token)
            (path/'idProduct').write_text('502a')
            device=adapter.discover()[0]
            self.assertEqual(device.mode,'bootloader-unverified')
            self.assertIn('Shared bootloader PID',device.details['Notice'])
            self.assertEqual(adapter.actions(device),())
            self.assertEqual(adapter.inspection_modes,())
            with patch.object(adapter,'discover',side_effect=AssertionError('flash tried device access')):
                with self.assertRaisesRegex(RuntimeError,'Flashing disabled'):
                    adapter.flash('token','install','absent.bin','hash',None,None)
                with self.assertRaisesRegex(RuntimeError,'Flashing disabled'):
                    adapter.load_image('absent.bin','custom')
        worker=Path(__file__).with_name('device_flash_service.py')
        result=subprocess.run([sys.executable,str(worker),'--gui-worker','flash','--model',adapter.id,
            '--token','unused','--action','install','--image','absent.bin','--sha256','unused'],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn('Flashing disabled',result.stdout)

    def test_huntsman_does_not_borrow_fun60_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);usb(root);adapter=HuntsmanAdapter(root)
            device=adapter.discover()[0]
            wrong='v0.1.0-'+Fun60Adapter.build_target+' git='+'b'*40+' state=clean'
            result=adapter.identify(device,wrong,control_available=False)
            self.assertNotEqual(result.version,wrong)
            # An unrelated MIDI peer must not pass the direct READY query.
            import midi_sysex as sx
            peer=SimpleNamespace(send=lambda _:None,close=lambda:None,
                receive=lambda _:sx.encode(sx.READY,7,payload=('build='+wrong).encode()))
            with patch('midi_backend.find_midi_device',return_value='mock'),patch('midi_backend.MidiBackend',return_value=peer),patch('flash_huntsman.secrets.randbelow',return_value=6):
                result=adapter.identify(device)
            self.assertEqual(result.version,'Unavailable')
            self.assertIn('selected Huntsman build target',result.details['Build query'])

    def test_updater_dependency(self):
        import firmware_flasher as backend
        with patch.dict('os.environ',{'HUNTSMAN_UPDATER_SRC':'/nonexistent/updater'}):
            self.assertFalse(backend.updater_available())
            with self.assertRaisesRegex(FlasherError,'git submodule'):backend.load_updater()

    def test_identification_actions_and_stale_target(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);adapter=HuntsmanAdapter(root)
            self.assertEqual(adapter.discover(),[])
            for mode,actions in (('custom',['reflash','restore']),('razer',['install','restore']),('bootloader',['install','restore'])):
                path=usb(root,mode);device=adapter.discover()[0]
                self.assertEqual(device.mode,mode)
                self.assertEqual([a.id for a in adapter.actions(device)],actions)
                self.assertEqual(adapter.selected(device.token),device)
                (path/'devnum').write_text('3')
                with self.assertRaisesRegex(FlasherError,'changed'):adapter.selected(device.token)
            usb(root,'custom','1-3')
            with self.assertRaisesRegex(FlasherError,'Exactly one'):adapter.selected(device.token)

    def test_image_identity_vectors_and_digests(self):
        adapter=HuntsmanAdapter()
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'app.bin'
            for custom,destination in ((True,'custom'),(False,'razer')):
                data=image(path,custom);loaded=adapter.load_image(path,destination)
                self.assertEqual(loaded.data,data)
                self.assertEqual(loaded.digest,hashlib.sha256(data).hexdigest())
                with self.assertRaises(ImageError):adapter.load_image(path,'razer' if custom else 'custom')
            old=bytearray(image(path));old[64:96]=b'OpenHuntsman'.ljust(32,b' ');path.write_bytes(old)
            for destination in ('custom','razer'):
                with self.assertRaises(ImageError):adapter.load_image(path,destination)
            data=bytearray(data);data[:8]=bytes(8);path.write_bytes(data)
            with self.assertRaisesRegex(ImageError,'stack'):adapter.load_image(path,'razer')
            path.write_bytes(b'123')
            with self.assertRaisesRegex(ImageError,'128 KiB'):adapter.load_image(path,'razer')
            path=path.with_suffix('.exe');path.write_bytes(b'not executable')
            with self.assertRaisesRegex(ImageError,'not executed'):adapter.load_image(path,'razer')

    def test_hex_bounds_eof_and_checksum(self):
        def record(kind,address,payload):
            body=bytes([len(payload)])+address.to_bytes(2,'big')+bytes([kind])+payload
            return ':'+(body+bytes([-sum(body)&255])).hex()
        text='\n'.join((record(4,0,b'\x20\x00'),record(0,0,b'abcd'),record(1,0,b'')))
        self.assertEqual(HuntsmanAdapter._hex(text),b'abcd')
        for bad in (text.replace('2000','2100'),text+'\n'+record(0,4,b'a'),
                    '\n'.join(text.splitlines()[:-1]),record(0,0,b'abcd')+'\n'+record(1,0,b'')):
            with self.assertRaises((ImageError,ValueError)):HuntsmanAdapter._hex(bad)

    def test_all_flash_paths_and_excluded_regions(self):
        for mode,action,destination in (('razer','install','custom'),('razer','restore','razer'),
                ('custom','reflash','custom'),('custom','restore','razer'),
                ('bootloader','install','custom'),('bootloader','restore','razer')):
            with self.subTest(mode=mode,action=action),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);sysfs=root/'usb';sysfs.mkdir();usb(sysfs,mode)
                adapter=HuntsmanAdapter(sysfs);device=adapter.discover()[0]
                file=root/'image.bin';image(file,destination=='custom')
                loaded=adapter.load_image(file,destination);calls=[]
                transport=SimpleNamespace(open_by_interface=lambda *args:None,TransportError=FlasherError)
                original=transport.open_by_interface
                def update(package,**kwargs):
                    calls.append((package,kwargs));usb(sysfs,destination)
                fake=(SimpleNamespace(APP_PID=0x02b0,BOOTLOADER_PID=0x110e),None,SimpleNamespace(update=update),None)
                with patch('flash_huntsman.backend.load_updater',return_value=fake),patch.dict(sys.modules,{'huntsman_updater':SimpleNamespace(transport=transport)}):
                    with self.assertRaisesRegex(ImageError,'changed'):adapter.flash(device.token,action,file,'0'*64,None,lambda _:None)
                    self.assertFalse(calls)
                    adapter.flash(device.token,action,file,loaded.digest,None,lambda _:None)
                package,options=calls[0]
                self.assertEqual(options['enter_boot'],mode!='bootloader')
                self.assertFalse(options['flash_fw']);self.assertIsNone(package.flash_image)
                self.assertEqual(package.app_image,loaded.data)
                self.assertIs(transport.open_by_interface,original)

if __name__=='__main__':unittest.main()
