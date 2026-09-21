"""Huntsman-only identification, image validation and application update adapter."""
from dataclasses import replace
import hashlib
from pathlib import Path
import re
import struct
import secrets
import time
from flash_models import ConnectedDevice, FirmwareImage, FlashAction
import firmware_flasher as backend

INSTALL = FlashAction('install','Install MIDI-Typist','custom','Convert this keyboard to MIDI-Typist.')
REFLASH = FlashAction('reflash','Reflash MIDI-Typist','custom','Update or reinstall the custom application; keep compatible settings.')
RESTORE = FlashAction('restore','Restore Razer firmware','razer','Install a Razer application supplied by you. Custom features will no longer be available.')


class HuntsmanAdapter:
    id = 'razer-huntsman-v3-pro-mini'
    name = 'Razer Huntsman Pro Mini V3'
    inspection_modes = ('custom', 'razer')
    control_inspection_modes = ()
    default_image = str(backend.REPO_ROOT/'build-huntsman/huntsman_firmware.bin')
    filetypes = (('Application / Razer resources','*.bin *.hex *.resources'),('All files','*'))
    safety = ('Application only · 128 KiB. Bootloader, Razer settings/serial, security data '
              'and optical-controller firmware are not written. Compatible custom saves are retained. '
              'Razer firmware may reclaim custom storage when it runs.')

    def __init__(self, sysfs='/sys/bus/usb/devices'):
        self.sysfs = Path(sysfs)

    def discover(self):
        if not self.sysfs.is_dir():
            raise backend.FlasherError('Huntsman device discovery currently requires Linux sysfs.')
        result = []
        for path in sorted(self.sysfs.iterdir()):
            def read(name, fallback=''):
                try: return (path/name).read_text().strip()
                except OSError: return fallback
            if read('idVendor') != '1532' or read('idProduct') not in ('02b0','110e'): continue
            pid = read('idProduct'); product = read('product','Unknown product')
            manufacturer = read('manufacturer'); serial = read('serial')
            mode = 'bootloader' if pid == '110e' else (
                'custom' if manufacturer == 'OpenHuntsman' else
                'razer' if 'Razer' in manufacturer or 'RAZER' in manufacturer else 'unknown')
            identity = '|'.join((path.name,read('busnum'),read('devnum'),pid,serial))
            token = hashlib.sha256(identity.encode()).hexdigest()
            details = {'Manufacturer':manufacturer or 'Not reported',
                       'USB revision':read('bcdDevice','Not reported'),
                       'Interfaces':read('bNumInterfaces','Not reported')}
            if mode == 'custom':
                details['Serial note'] = 'Custom USB identifier; the factory serial is not exposed by this firmware.'
            if mode == 'bootloader': details['Recovery'] = 'Ready to flash directly; no bootloader-entry request is needed.'
            result.append(ConnectedDevice(self.id,path.name,token,mode,product,
                serial or 'Not reported in this mode',
                'Application version unavailable in bootloader mode' if mode=='bootloader' else 'Not queried','1532:'+pid,
                read('speed','Unknown')+' Mb/s',details))
        return result

    def actions(self, device):
        return {'custom':(REFLASH,RESTORE),'razer':(INSTALL,RESTORE),
                'bootloader':(INSTALL,RESTORE)}.get(device.mode,())

    def identify(self, device, build_hint=None, control_available=True):
        if device.mode!='custom':return device
        if build_hint:return replace(device,version=build_hint)
        if not control_available:return replace(device,version='Configuration connection is acquiring the build identity')
        from midi_backend import MidiBackend, find_midi_device
        import midi_sysex as sx
        from keyboard_gui_model import parse_build
        from firmware_defaults import DEFAULTS as D
        connection=None;session=secrets.randbelow(0xffffffff)+1
        try:
            self.selected(device.token)
            port=find_midi_device()
            if not port:raise ValueError('No unique control port available')
            connection=MidiBackend(port);connection.send(sx.encode(sx.HELLO,session))
            deadline=time.monotonic()+D['MIDI_CONTROL_COMMAND_TIMEOUT_MS']/1000
            while time.monotonic()<deadline:
                wire=connection.receive(.02)
                if not wire:continue
                kind,reply_session,_,payload=sx.decode(wire)
                if kind==sx.READY and reply_session==session:
                    build=parse_build(payload+b'\n')
                    if build:return replace(device,version=build[0])
            raise TimeoutError('SysEx build query timed out')
        except Exception as error:
            return replace(device,version='Unavailable',details={**device.details,'Build query':str(error)})
        finally:
            if connection:
                try:connection.send(sx.encode(sx.CLOSE,session))
                finally:connection.close()

    def selected(self, token):
        devices = self.discover()
        if len(devices) != 1:
            raise backend.FlasherError('Exactly one supported Huntsman must be connected; no target was selected automatically.')
        device = devices[0]
        if device.token != token:
            raise backend.FlasherError('The connected device changed. Refresh and confirm again.')
        return device

    def inspect(self, token):
        current = self.selected(token)
        if current.mode == 'bootloader': return current
        _, device, _, _ = backend.load_updater()
        info = device.query_device_info()
        details = dict(current.details)
        details.update({'HID version':'.'.join(str(v) for v in info.version),
                        'Extended version':info.extended_version.hex(' '),
                        'Capability':info.capability.hex(' '),'HID build':info.build.hex(' '),
                        'Device mode':str(info.mode)})
        serial = info.serial or current.serial
        # The custom updater deliberately supplies compatibility version 2.1,
        # not its software build version. Do not mislabel it as the latter.
        version = '2.1 (updater compatibility)' if current.mode == 'custom' else details['HID version']
        self.selected(token)
        return replace(current,serial=serial,version=version,details=details)

    def load_image(self, path, destination):
        if destination not in ('custom','razer'): raise backend.ImageError('Unknown firmware destination')
        file = Path(path)
        if file.stat().st_size > 16*1024*1024: raise backend.ImageError('Firmware input exceeds the 16 MiB safety limit')
        original = file.read_bytes()
        suffix = file.suffix.lower()
        description = 'Raw 128 KiB application; model must be confirmed by the user.'
        if suffix == '.resources':
            backend.load_updater()
            from huntsman_updater.resources import load_firmware_package
            package = load_firmware_package(file)
            if file.read_bytes() != original: raise backend.ImageError('Firmware file changed during validation')
            if (package.vid,package.pid,package.bootloader_vid,package.bootloader_pid) != (0x1532,0x02b0,0x1532,0x110e):
                raise backend.ImageError('Razer resource metadata targets a different keyboard')
            data = package.app_image
            description = 'Razer resources: matching VID/PID. Only the application is selected; secondary firmware is excluded.'
        elif suffix == '.hex':
            data = self._hex(original.decode('ascii'))
            description = 'Validated Intel HEX application at 0x20000000; confirm the source keyboard model.'
        elif suffix == '.bin': data = original
        else:
            raise backend.ImageError('Select an application .bin, .hex, or DeviceUpdater.resources file. Installer .exe files are not executed or accepted.')
        if len(data) != backend.APP_IMAGE_BYTES: raise backend.ImageError('Expected a complete 128 KiB application image')
        stack, reset = struct.unpack_from('<II',data)
        if stack % 8 or not (0x04000000 < stack <= 0x04008000 or 0x20000000 < stack <= 0x20040000):
            raise backend.ImageError('Image has an invalid LPC5528 initial stack pointer')
        if not reset & 1 or not 0x20000000 <= (reset & ~1) < 0x20020000:
            raise backend.ImageError('Image reset vector is outside the application execution region')
        custom = b'MIDI-Typist' in data or b'OpenHuntsman' in data or b'OPENHUNTSMAN' in data
        current = b'RZ03-0499' in data and b'MIDI-Typist SysEx control ready' in data
        if destination == 'custom' and not current:
            raise backend.ImageError('Select the current MIDI-Typist Huntsman SysEx application; older custom firmware is unsupported')
        if destination == 'razer' and custom:
            raise backend.ImageError('This is custom firmware, not a Razer restoration image')
        return FirmwareImage(str(file.resolve()),data,hashlib.sha256(data).hexdigest(),destination,description)

    @staticmethod
    def _hex(text):
        output = bytearray(); base = 0; eof = False
        for line in text.splitlines():
            if not line.strip(): continue
            if eof or not re.fullmatch(r':[0-9a-fA-F]+',line.strip()): raise backend.ImageError('Invalid Intel HEX record')
            record = bytes.fromhex(line.strip()[1:])
            if len(record)<5 or len(record)!=record[0]+5 or sum(record)&255: raise backend.ImageError('Intel HEX checksum/length mismatch')
            address = int.from_bytes(record[1:3],'big'); kind=record[3]; payload=record[4:-1]
            if kind == 4 and len(payload)==2 and address==0: base=int.from_bytes(payload,'big')<<16
            elif kind == 0:
                if base+address != 0x20000000+len(output): raise backend.ImageError('HEX addresses must cover the application contiguously from 0x20000000')
                output.extend(payload)
                if len(output)>backend.APP_IMAGE_BYTES: raise backend.ImageError('HEX exceeds application region')
            elif kind == 1 and not payload and address==0: eof=True
            elif kind == 5 and len(payload)==4 and address==0: pass
            else: raise backend.ImageError('Unsupported Intel HEX record')
        if not eof: raise backend.ImageError('Intel HEX EOF missing')
        return bytes(output)

    def flash(self, token, action, path, digest, progress, status):
        current = self.selected(token)
        option = next((a for a in self.actions(current) if a.id==action),None)
        if option is None: raise backend.FlasherError('This action is not available for the connected device')
        image = self.load_image(path,option.destination)
        if image.digest != digest: raise backend.ImageError('Firmware changed since confirmation; nothing was flashed')
        constants, _, updater, _ = backend.load_updater()
        from huntsman_updater import transport
        self.selected(token)
        original_open = transport.open_by_interface
        def bound_open(vid,pid,interface=None):
            devices = self.discover()
            if len(devices)!=1 or devices[0].location!=current.location:
                raise transport.TransportError('Confirmed keyboard is missing or ambiguous; refusing another USB device')
            return original_open(vid,pid,interface)
        # Private GUI worker owns this updater process. Every backend open is
        # pinned to the physical USB location, including after re-enumeration.
        transport.open_by_interface = bound_open
        try:
            status('Application only: '+option.title)
            updater.update(backend.ApplicationPackage(image.data,constants),
                           enter_boot=current.mode!='bootloader',flash_fw=False,progress=progress)
        finally: transport.open_by_interface = original_open
        devices = self.discover()
        if len(devices)!=1 or devices[0].location!=current.location or devices[0].mode!=option.destination:
            raise backend.FlasherError('Programming finished, but the requested application identity was not verified. Refresh device status.')
        status('Application returned as '+('MIDI-Typist' if option.destination=='custom' else 'Razer firmware'))
        return image.digest
