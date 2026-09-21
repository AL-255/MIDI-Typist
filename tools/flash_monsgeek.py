"""Identity-bound M1 V5 TMR factory conversion for the GUI.

USB PIDs are shared across products. Only the normal application's vendor
identity reply can establish ID2949. A bootloader candidate is never a verified
M1, and entering that bootloader is destructive even without sending an image.
"""
from dataclasses import replace
from contextlib import contextmanager
import hashlib
import os
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
    inspection_modes = ('candidate', 'factory')
    default_image = str(Path(__file__).resolve().parents[1]/'build-m1-hal/m1_development.bin')
    filetypes = (('M1 application or factory dump', '*.bin'),)
    safety = ('M1 trial USB startup is not working yet; not for daily use. '
              'Experimental conversion: factory entry resets stock user settings. '
              'Bootloader code and factory sensor calibration are preserved. '
              'Every update erases custom saves. Trial startup attempts to arm reset-to-IAP recovery; '
              'if armed, power cycling erases the trial application. Physical recovery is unconfirmed. '
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
            mode = 'unverified_bootloader' if usb_id == BOOTLOADER_ID else 'candidate'
            identity = '|'.join((path.name, read('busnum'), read('devnum'), usb_id, read('serial')))
            details = {'USB revision': read('bcdDevice', 'Not reported'),
                       'Model verification': 'Not queried; USB identity is shared between products',
                       'Recovery': ('Shared bootloader PID; cannot establish the model or flash.'
                                    if mode == 'unverified_bootloader' else
                                    'Choose Read firmware details to verify internal ID2949. No settings are changed.')}
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
        return (INSTALL, RESTORE) if device.mode == 'factory' else ()

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
        if current.mode not in self.inspection_modes:
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

    def load_image(self, path, destination):
        return iap.load_image(path, destination)

    def flash(self, token, action, path, digest, progress, status):
        import fcntl
        # Resolve everything that can fail locally before destructive entry.
        choices = {item.id:item for item in (INSTALL, RESTORE)}
        if action not in choices: raise ValueError('Unsupported M1 flash action')
        image = self.load_image(path, choices[action].destination)
        if image.digest != digest: raise iap.ImageError('Firmware changed after confirmation')
        import usb.core
        import usb.util
        import usb.backend.libusb1
        backend = usb.backend.libusb1.get_backend()
        if backend is None: raise RuntimeError('Install libusb before converting the keyboard')
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
            # A transfer exception means unknown acceptance, never a reason to resend.
            if fcntl.ioctl(fd, HIDIOCSFEATURE, boot_request(), True) != FEATURE_BYTES:
                raise RuntimeError('Boot-entry acceptance uncertain; no retry was sent')
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
        # This process witnessed ID2949 -> guarded factory entry -> IAP on the
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
