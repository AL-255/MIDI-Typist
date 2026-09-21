"""Identity-bound M1 V5 TMR conversion and custom reflash for the GUI.

USB PIDs are shared across products. Factory identity requires its ID2949 reply;
custom identity requires the build target on the same physical USB MIDI port.
A bootloader candidate is never a verified M1, and entering that bootloader
is destructive even without sending an image.
"""
from dataclasses import replace
from contextlib import contextmanager
import hashlib
import os
import secrets
from pathlib import Path
import struct
import time

from flash_models import ConnectedDevice, FlashAction
import monsgeek_iap as iap


MODEL_ID = 2949
APPLICATION_IDS = frozenset(('3151:5030', '38ee:0033'))
BOOTLOADER_ID = '3151:502a'
VENDOR_INTERFACE = 2
IDENTITY_COMMAND = 0x8f
REPORT_BYTES = 64
# Linux hidraw includes the zero report-ID byte for unnumbered feature reports.
FEATURE_BYTES = REPORT_BYTES + 1
HIDIOCGRAWINFO = 0x80084803
HIDIOCSFEATURE = 0xc0414806
HIDIOCGFEATURE = 0xc0414807
IDENTITY_SETTLE_SECONDS = .05
BOOT_TIMEOUT_SECONDS = 15
ENUMERATION_POLL_SECONDS = .1
INSTALL = FlashAction('install', 'Install experimental MIDI-Typist', 'custom',
    'Reset factory user settings and install the M1 trial application. '
    'Factory sensor calibration is preserved. The next reset enters IAP and erases the trial and custom saves.')
RESTORE = FlashAction('restore', 'Install MonsGeek factory application', 'monsgeek',
    'Use your own ID2949 factory image. Resets factory user settings; preserves sensor calibration and bootloader code.')
REFLASH = FlashAction('reflash', 'Reflash experimental MIDI-Typist', 'custom',
    'Replace the M1 application through its armed recovery path. Custom profiles are erased.')
CUSTOM_TARGET = 'MG-M1V5TMR'


def boot_request():
    report = bytearray(FEATURE_BYTES)
    report[1:6] = b'\x7f\x55\xaa\x55\xaa'
    report[6] = (0xff-sum(report[1:9])) & 0xff
    return report


def identity_request():
    report = bytearray(FEATURE_BYTES)
    report[1] = IDENTITY_COMMAND
    report[8] = (0xff - IDENTITY_COMMAND) & 0xff
    return report


def parse_identity(report):
    if (len(report) != FEATURE_BYTES or report[0] != 0 or
            report[1] != IDENTITY_COMMAND):
        raise ValueError('Invalid M1 identity reply; no device model was confirmed')
    model = int.from_bytes(report[2:4], 'little')
    if model != MODEL_ID:
        raise ValueError(f'Expected M1 ID{MODEL_ID}, received ID{model}; unsupported keyboard')
    version = int.from_bytes(report[8:10], 'little')
    digits = [(version >> shift) & 15 for shift in (12, 8, 4, 0)]
    if not version or any(digit > 9 for digit in digits):
        raise ValueError('Invalid BCD firmware version in M1 identity reply')
    major, minor = digits[0] * 10 + digits[1], digits[2] * 10 + digits[3]
    return f'v{major}.{minor:02d}'


class MonsGeekAdapter:
    id = 'monsgeek-m1-v5-tmr'
    name = 'MonsGeek M1 V5 TMR (experimental)'
    inspection_modes = ('candidate', 'factory', 'custom_candidate', 'custom')
    control_inspection_modes = ('custom_candidate', 'custom')
    default_image = str(Path(__file__).resolve().parents[1]/'build-m1-hal/m1_development.bin')
    filetypes = (('M1 application or factory dump', '*.bin'),)
    safety = ('M1 trial keyboard/GUI operation uses provisional calibration when factory bounds cannot be imported; not for daily use. '
              'Experimental conversion: factory entry resets stock user settings. '
              'Bootloader code and factory sensor calibration are preserved. '
              'Every update erases custom saves. Trial startup arms reset-to-IAP recovery; '
              'power cycling erases the trial application. '
              'A shared bootloader PID alone '
              'cannot authorize an update. No automatic retries.')

    def __init__(self, sysfs='/sys/bus/usb/devices', hidraw='/sys/class/hidraw', dev='/dev'):
        self.sysfs, self.hidraw, self.dev = map(Path, (sysfs, hidraw, dev))

    def discover(self):
        if not self.sysfs.is_dir():
            raise RuntimeError('M1 identity discovery currently requires Linux sysfs')
        result = []
        for path in sorted(self.sysfs.iterdir()):
            def read(name, fallback=''):
                try:
                    return (path / name).read_text().strip()
                except OSError:
                    return fallback
            usb_id = read('idVendor') + ':' + read('idProduct')
            if usb_id not in APPLICATION_IDS and usb_id != BOOTLOADER_ID:
                continue
            bus,address,serial=read('busnum'),read('devnum'),read('serial')
            if not bus.isdecimal() or not address.isdecimal():continue
            mode = 'unverified_bootloader' if usb_id == BOOTLOADER_ID else 'candidate'
            if (usb_id in APPLICATION_IDS and read('manufacturer')=='MIDI-Typist' and
                    read('product')=='M1 V5 TMR'):
                mode='custom_candidate'
            identity = '|'.join((path.name,bus,address,usb_id,serial))
            details = {'USB revision': read('bcdDevice', 'Not reported'),
                       'Model verification': 'Not queried; USB identity is shared between products',
                       'Recovery': ('Shared bootloader PID; cannot establish the model or flash.'
                                    if mode == 'unverified_bootloader' else
                                    'Choose Read firmware details to verify internal ID2949. No settings are changed.')}
            if mode=='custom_candidate':
                details['Recovery']='Read firmware details to verify the custom build over its USB-bound MIDI control port.'
            # sysfs files disappear separately during USB reset. Never turn a
            # torn snapshot into a new identity (or authorize a partial IAP).
            if (read('idVendor')+':'+read('idProduct'),read('busnum'),read('devnum'),read('serial'))!=(usb_id,bus,address,serial):
                continue
            result.append(ConnectedDevice(self.id, path.name,
                hashlib.sha256(identity.encode()).hexdigest(), mode,
                read('product', 'Unknown product'), read('serial', 'Not exposed by USB'),
                'Not queried', usb_id, read('speed', 'Unknown') + ' Mb/s', details))
        return result

    def identify(self, device, build_hint=None, control_available=True):
        # A connected Huntsman's build hint is not this device's identity.
        # Refresh is passive; only the explicit inspection sends a feature query.
        return device

    def actions(self, device):
        return {'factory':(INSTALL, RESTORE),'custom':(REFLASH, RESTORE)}.get(device.mode,())

    def selected(self, token):
        devices = self.discover()
        if len(devices) != 1:
            raise RuntimeError('Exactly one M1 USB candidate must be connected; selection is ambiguous or missing')
        if devices[0].token != token:
            raise RuntimeError('The connected device changed. Refresh and confirm again.')
        return devices[0]

    def vendor_node(self, device):
        usb_path = (self.sysfs / device.location).resolve(strict=True)
        nodes = []
        for entry in sorted(self.hidraw.glob('hidraw*')):
            try:
                hid = (entry / 'device').resolve(strict=True)
                interface = hid.parent
                if (interface.parent == usb_path and
                        (interface / 'bInterfaceNumber').read_text().strip() == f'{VENDOR_INTERFACE:02x}' and
                        (interface / 'bInterfaceClass').read_text().strip() == '03'):
                    nodes.append(self.dev / entry.name)
            except (OSError, ValueError):
                continue
        if len(nodes) != 1:
            raise RuntimeError('No unique M1 vendor HID interface; keyboard interfaces will not be opened')
        return nodes[0]

    @contextmanager
    def feature_device(self, token):
        import fcntl
        current = self.selected(token)
        if current.mode not in ('candidate','factory'):
            raise RuntimeError('Shared bootloader identity cannot verify an M1; no query was sent')
        node = self.vendor_node(current)
        fd = os.open(node, os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW)
        try:
            # Bind both the open kernel HID and its physical USB enumeration.
            info = bytearray(8)
            fcntl.ioctl(fd, HIDIOCGRAWINFO, info, True)
            bus, vid, pid = struct.unpack('=IHH', info)
            if bus != 3 or f'{vid:04x}:{pid:04x}' != current.usb_id:
                raise RuntimeError('Opened HID does not match the selected USB device')
            self.selected(token)
            if self.vendor_node(current) != node:
                raise RuntimeError('Vendor interface changed before query')
            yield current, fd
        finally:
            os.close(fd)

    def inspect(self, token):
        import fcntl
        if self.selected(token).mode=='custom_candidate':
            with self.custom_session(token) as device:return device
        with self.feature_device(token) as (current, fd):
            request = identity_request()
            if fcntl.ioctl(fd, HIDIOCSFEATURE, request, True) != FEATURE_BYTES:
                raise RuntimeError('Incomplete identity request; no automatic retry')
            time.sleep(IDENTITY_SETTLE_SECONDS)
            reply = bytearray(FEATURE_BYTES)
            if fcntl.ioctl(fd, HIDIOCGFEATURE, reply, True) != FEATURE_BYTES:
                raise RuntimeError('Incomplete identity response; no automatic retry')
            version = parse_identity(reply)
            self.selected(token)
        return replace(current, mode='factory', product='MonsGeek M1 V5 TMR', version=version,
            details={**current.details, 'Model verification': f'ID{MODEL_ID} confirmed by vendor identity query',
                     'Recovery': self.safety})

    @contextmanager
    def custom_session(self, token, *, enter_boot=False, status=lambda message:None):
        from midi_backend import MidiBackend, control_port_for_usb
        from keyboard_gui_model import parse_build
        from firmware_defaults import DEFAULTS as D
        import midi_sysex as sx
        current=self.selected(token)
        if current.mode!='custom_candidate':raise RuntimeError('Selected device is not a custom M1 candidate')
        usb_path=self.sysfs/current.location
        port=control_port_for_usb(usb_path)
        connection=None;boot_requested=False
        session=secrets.randbelow(0xffffffff)+1
        try:
            connection=MidiBackend(port)
            self.selected(token)
            if control_port_for_usb(usb_path)!=port:raise RuntimeError('MIDI port changed before identity query')
            connection.send(sx.encode(sx.HELLO,session))
            deadline=time.monotonic()+D['MIDI_CONTROL_COMMAND_TIMEOUT_MS']/1000
            while True:
                wire=connection.receive(.02)
                if wire:
                    kind,reply_session,sequence,payload=sx.decode(wire)
                    if reply_session==session and kind==sx.READY and sequence==0:
                        build=parse_build(payload+b'\n')
                        if not build or build[2]!=CUSTOM_TARGET:
                            raise RuntimeError('Control port did not identify the current M1 firmware target')
                        break
                    if reply_session==session and kind==sx.ERROR:
                        raise RuntimeError('M1 identity query was rejected')
                if time.monotonic()>=deadline:raise TimeoutError('M1 custom build query timed out')
            self.selected(token)
            if control_port_for_usb(usb_path)!=port:raise RuntimeError('MIDI port changed after identity query')
            confirmed=replace(current,mode='custom',version=build[0],details={**current.details,
                'Model verification':CUSTOM_TARGET+' confirmed by USB-bound SysEx',
                'Control port':port,'Recovery':self.safety})
            if enter_boot:
                status('Custom M1 build verified: '+build[0]+'. Requesting armed recovery once.')
                boot_requested=True # send failure has uncertain acceptance; never resend or cancel
                connection.send(sx.encode(sx.COMMAND,session,1,b'bootloader'))
            yield confirmed
        finally:
            if connection:
                if not boot_requested:
                    try:connection.send(sx.encode(sx.CLOSE,session))
                    except Exception:pass  # preserve original identity/USB error
                try:connection.close()
                except Exception:
                    if not boot_requested:raise
                    status('MIDI port closed during boot entry; checking USB transition.')

    def enter_factory(self, token, status):
        import fcntl
        with self.feature_device(token) as (current, fd):
            request = identity_request()
            if fcntl.ioctl(fd, HIDIOCSFEATURE, request, True) != FEATURE_BYTES:
                raise RuntimeError('Incomplete pre-flash identity request')
            time.sleep(IDENTITY_SETTLE_SECONDS)
            reply = bytearray(FEATURE_BYTES)
            if fcntl.ioctl(fd, HIDIOCGFEATURE, reply, True) != FEATURE_BYTES:
                raise RuntimeError('Incomplete pre-flash identity response')
            version = parse_identity(reply)
            self.selected(token)
            status(f'ID{MODEL_ID} {version} verified; entering IAP once (stock settings will be reset).')
            if fcntl.ioctl(fd, HIDIOCSFEATURE, boot_request(), True) != FEATURE_BYTES:
                raise RuntimeError('Boot-entry acceptance uncertain; no retry was sent')
        return current

    def load_image(self, path, destination):
        return iap.load_image(path, destination)

    def flash(self, token, action, path, digest, progress, status):
        # Resolve everything that can fail locally before destructive entry.
        choices = {item.id:item for item in (INSTALL, RESTORE, REFLASH)}
        if action not in choices: raise ValueError('Unsupported M1 flash action')
        image = self.load_image(path, choices[action].destination)
        if image.digest != digest: raise iap.ImageError('Firmware changed after confirmation')
        import usb.core
        import usb.util
        import usb.backend.libusb1
        backend = usb.backend.libusb1.get_backend()
        if backend is None: raise RuntimeError('Install libusb before converting the keyboard')
        current=self.selected(token)
        if current.mode=='custom_candidate':
            if action not in ('reflash','restore'):raise ValueError('Use Reflash for a custom M1')
            with self.custom_session(token,enter_boot=True,status=status) as current:pass
        elif current.mode=='candidate':
            if action not in ('install','restore'):raise ValueError('Use Install for a factory M1')
            current=self.enter_factory(token,status)
        else:raise RuntimeError('No verified application entry path for this M1 candidate')
        deadline = time.monotonic()+BOOT_TIMEOUT_SECONDS
        while True:
            candidates = self.discover()
            if len(candidates)>1: raise RuntimeError('Ambiguous devices after M1 boot entry')
            if candidates:
                boot = candidates[0]
                if boot.location != current.location:
                    raise RuntimeError('Keyboard physical port changed; refusing IAP')
                if boot.mode == 'unverified_bootloader': break
                if boot.token != token:
                    raise RuntimeError('Unexpected application after boot entry; stopped')
            if time.monotonic()>=deadline: raise TimeoutError('M1 did not enter IAP; no entry retry was sent')
            time.sleep(ENUMERATION_POLL_SECONDS)
        # This process witnessed verified application -> guarded entry -> IAP on the
        # same port. Never offer this path to a pre-existing, unverified PID.
        return self._program_entered_bootloader(boot, image, progress, status, backend)

    def _program_entered_bootloader(self, boot, image, progress, status, backend=None):
        """Private continuation for a witnessed, freshly erased IAP session.

        Not an action for a discovered shared PID. The caller must have just
        established the model and persistent boot flag through guarded entry.
        """
        import usb.core
        import usb.util
        self.selected(boot.token)
        usb_path = self.sysfs/boot.location
        bus, address = (int((usb_path/name).read_text()) for name in ('busnum','devnum'))
        deadline=time.monotonic()+BOOT_TIMEOUT_SECONDS
        while True:
            self.selected(boot.token)
            matches = list(usb.core.find(find_all=True, idVendor=0x3151, idProduct=0x502a,
                backend=backend, custom_match=lambda dev:dev.bus==bus and dev.address==address))
            if len(matches)==1:break
            if len(matches)>1 or time.monotonic()>=deadline:
                raise RuntimeError('No unique port-bound IAP device')
            # sysfs identity can precede libusb's device-node enumeration.
            time.sleep(ENUMERATION_POLL_SECONDS)
        device = matches[0]; claimed = False
        try:
            configuration = device.get_active_configuration()
            if configuration.bNumInterfaces!=1 or configuration[(0,0)].bInterfaceClass!=3:
                raise RuntimeError('Unexpected M1 bootloader USB interfaces')
            if device.is_kernel_driver_active(0): device.detach_kernel_driver(0)
            usb.util.claim_interface(device,0); claimed=True
            self.selected(boot.token)
            status('Programming application only; requiring bootloader checksum and flash-readback verdict.')
            result = iap.program_application(iap.IapLink(device), image,
                lambda done,total:progress(done*iap.BLOCK_BYTES,total*iap.BLOCK_BYTES))
            status('Bootloader accepted the complete application. Runtime operation must be checked separately.')
            return result
        finally:
            if claimed:
                try: usb.util.release_interface(device,0)
                except usb.core.USBError: pass  # successful finish resets USB
            usb.util.dispose_resources(device)
