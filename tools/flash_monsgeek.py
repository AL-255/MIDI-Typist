"""Read-only M1 V5 TMR identification for the GUI; no update commands.

USB PIDs are shared across products. Only the normal application's vendor
identity reply can establish ID2949. A bootloader candidate is never a verified
M1, and entering that bootloader is destructive even without sending an image.
"""
from dataclasses import replace
import hashlib
import os
from pathlib import Path
import struct
import time

from flash_models import ConnectedDevice


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
    name = 'MonsGeek M1 V5 TMR (identity only)'
    inspection_modes = ('candidate', 'factory')
    default_image = ''
    filetypes = ()
    safety = ('Read-only identity support. M1 flashing and custom firmware are not available. '
              'Its factory bootloader-entry command erases settings and the application; '
              'this adapter never sends it. USB VID/PID alone does not establish the model.')

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
        return ()

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

    def inspect(self, token):
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
            request = identity_request()
            if fcntl.ioctl(fd, HIDIOCSFEATURE, request, True) != FEATURE_BYTES:
                raise RuntimeError('Incomplete identity request; no automatic retry')
            time.sleep(IDENTITY_SETTLE_SECONDS)
            reply = bytearray(FEATURE_BYTES)
            if fcntl.ioctl(fd, HIDIOCGFEATURE, reply, True) != FEATURE_BYTES:
                raise RuntimeError('Incomplete identity response; no automatic retry')
            version = parse_identity(reply)
            self.selected(token)
        finally:
            os.close(fd)
        return replace(current, mode='factory', product='MonsGeek M1 V5 TMR', version=version,
            details={**current.details, 'Model verification': f'ID{MODEL_ID} confirmed by vendor identity query',
                     'Recovery': 'Identity verified. M1 flashing is disabled; factory firmware remains running.'})

    def load_image(self, path, destination):
        raise RuntimeError('M1 image validation and flashing are not implemented; no file was opened')

    def flash(self, token, action, path, digest, progress, status):
        raise RuntimeError('M1 flashing is disabled; no bootloader or flash command was sent')
