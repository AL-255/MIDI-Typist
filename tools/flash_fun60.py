"""Read-only FUN60 inventory. No boot-entry or USB update binding is enabled.

The factory IAP erases the application before enumeration. Keep that operation
unreachable until a complete port has passed its application-boundary audits.
"""
from dataclasses import replace
import hashlib
from pathlib import Path
from flash_models import ConnectedDevice, matching_build


class Fun60Adapter:
    id='monsgeek-fun60-pro-wired'
    name='MonsGeek FUN60 PRO Wired'
    build_target='monsgeek_fun60_pro_wired'
    inspection_modes=()  # sysfs inventory needs no privileged USB query
    default_image=''
    filetypes=(('Application image','*.bin'),)
    unavailable=('Flashing disabled: the FUN60 application is not complete or physically validated. '
                 'Discovery does not enter the bootloader or write to the keyboard.')
    safety=('Factory bootloader entry erases the application before USB enumeration and may erase '
            'factory settings. Custom settings in the application tail are lost on a reflash. '
            'Never use another board\'s image or updater.')

    def __init__(self,sysfs='/sys/bus/usb/devices'):
        self.sysfs=Path(sysfs)

    def discover(self):
        if not self.sysfs.is_dir():
            raise RuntimeError('FUN60 discovery currently requires Linux sysfs.')
        result=[]
        for path in sorted(self.sysfs.iterdir()):
            def read(name,fallback=''):
                try:return (path/name).read_text().strip()
                except OSError:return fallback
            pid=read('idProduct').lower()
            if read('idVendor').lower()!='3151' or pid not in ('502d','502a'):continue
            product=read('product','Unknown product');manufacturer=read('manufacturer')
            serial=read('serial');boot=pid=='502a'
            mode='bootloader-unverified' if boot else 'custom' if (
                manufacturer=='MIDI-Typist' and product=='FUN60 PRO MIDI-Typist') else 'unknown'
            identity='|'.join((path.name,read('busnum'),read('devnum'),pid,serial))
            details={'Manufacturer':manufacturer or 'Not reported',
                'USB revision':read('bcdDevice','Not reported'),
                'Interfaces':read('bNumInterfaces','Not reported'),
                'Flashing':self.unavailable,
                'Notice':('Shared bootloader PID: the exact keyboard SKU is not verified.' if boot else
                          'ID2304 application PID; firmware origin is not established by USB descriptors.')}
            if mode=='custom':
                details['Notice']='MIDI-Typist USB descriptors; software provenance requires the configuration handshake.'
                details['Serial note']='MCU-derived custom identifier, not the factory serial.'
            result.append(ConnectedDevice(self.id,path.name,hashlib.sha256(identity.encode()).hexdigest(),
                mode,product,serial or 'Not reported in this mode',
                'No application version in bootloader mode' if boot else 'Not queried (read-only inventory)',
                '3151:'+pid,read('speed','Unknown')+' Mb/s',details))
        return result

    def identify(self,device,build_hint=None,control_available=True):
        # Do not open an arbitrary MIDI port or send factory feature reports.
        build=matching_build(build_hint,self.build_target)
        return replace(device,version=build) if device.mode=='custom' and build else device

    def actions(self,device):
        return ()

    def inspect(self,token):
        devices=self.discover()
        if len(devices)!=1:raise RuntimeError('Exactly one matching FUN60 candidate must be connected.')
        if devices[0].token!=token:raise RuntimeError('The connected device changed. Refresh again.')
        return devices[0]

    def load_image(self,path,destination):
        raise RuntimeError(self.unavailable)

    def flash(self,token,action,path,digest,progress,status):
        # This guard also applies to direct/private-worker calls, independent
        # of the GUI's disabled buttons. No files, USB devices or IAP are opened.
        raise RuntimeError(self.unavailable)
