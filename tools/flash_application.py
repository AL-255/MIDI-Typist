#!/usr/bin/env python3
"""Flash a built 128 KiB application image with the sibling updater project.

Uses the reviewed application-only path (`huntsman_updater.updater.update`):
enter the bootloader, stream the image over the channel-0x10 DFU interface and
wait for the device to re-enumerate in application mode. The bootloader,
factory/security data, primary settings and secondary-controller regions are
never written. Requires root for /dev/bus/usb access:

    printf 'PASSWORD\n' | sudo -S -p '' python3 tools/flash_application.py \
        build-keyboard-fn-menu/huntsman_firmware.bin

After the flash the script performs the **cold boot**: it opens the
application's CDC port and sends `cfg clean`, the host equivalent of Fn+R. A
freshly flashed build therefore starts from its own defaults instead of
inheriting the stored calibration and Fn-menu settings of the previous one.
Pass --keep-settings to skip that step and keep the stored state.

Record the printed sha256; the device is queried before and after the flash.
"""
import argparse
import hashlib
import queue
import sys
import time
from pathlib import Path

sys.path.insert(0, '/home/yukidama/Downloads/HuntsmanV3ProMini_02B0_FirmwareUpdater_v2.01.00_r1/updater/src')
sys.path.insert(0, str(Path(__file__).resolve().parent))

from huntsman_updater import constants as C
from huntsman_updater import device, updater
from huntsman_updater.firmware import validate_app_image
from keyboard_gui_transport import Connection, find_cdc_device


class ApplicationPackage:
    """Raw application-only image; no secondary FlashFW content."""

    def __init__(self, image):
        self.pid = C.APP_PID
        self.bootloader_pid = C.BOOTLOADER_PID
        self.app_image = image
        self.flash_image = None


def cold_boot(timeout=40.0, attempts=12, wait=1.0):
    """Clear the configuration this build would otherwise inherit.

    The device erases both authorized pages (saved calibration and Fn-menu
    settings) and reports result 1 only after reading the pages back blank,
    then applies defaults on the first neutral frame. It refuses the command
    until its own scan is valid, and a rejected attempt ends the connection,
    so each attempt reconnects and waits for valid telemetry first. Returns the
    state the device reported afterwards.
    """
    deadline = time.monotonic()+timeout
    path = None
    while not path and time.monotonic() < deadline:
        path = find_cdc_device()
        if not path: time.sleep(0.5)
    if not path:
        raise RuntimeError('no CDC device found after flashing; reconnect and send `cfg clean` manually')
    last = 'no attempt completed'
    for attempt in range(attempts):
        if time.monotonic() >= deadline: break
        connection = Connection(path)
        connection.start()
        try:
            confirmed = False
            while time.monotonic() < deadline:
                try: event = connection.events.get(timeout=0.1)
                except queue.Empty: event = ''
                if event.startswith('Confirmed clean'):
                    latest = connection.snapshot()
                    return latest[1] if latest else None
                if event.startswith('ERROR'):
                    last = event
                    break
                if not connection.is_alive():
                    last = 'CDC worker stopped before the cold boot was acknowledged'
                    break
                latest = connection.snapshot()
                # The device refuses the cold boot until its scan is valid.
                if not confirmed and connection.connected and latest and latest[1].flags & 4:
                    connection.submit('clean'); confirmed = True
            else:
                last = 'no confirmation before the timeout'
        finally:
            connection.stop(); connection.join(timeout=2)
        if attempt+1 < attempts:
            print(f'  cold boot attempt {attempt+1} not accepted yet ({last}); retrying')
            time.sleep(wait)
    raise RuntimeError(f'cold boot not confirmed after {attempts} attempts ({last})')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='128 KiB raw application image (huntsman_firmware.bin)')
    parser.add_argument('--no-enter-boot', action='store_true',
                        help='device is already in the bootloader (1532:110E)')
    parser.add_argument('--keep-settings', action='store_true',
                        help='skip the cold boot and keep the stored calibration and settings')
    args = parser.parse_args()
    image = validate_app_image(Path(args.image).read_bytes())
    digest = hashlib.sha256(image).hexdigest()
    print(f'Application image: {len(image)} bytes, sha256 {digest}')
    try:
        print(f'Before flash: device version {device.query_version().hex(" ")}')
    except Exception as error:  # noqa: BLE001 - device may be absent/unreadable
        print(f'Before flash: version unavailable ({error})')

    def progress(done, total):
        print(f'\r  program {done}/{total} ({done*100//total}%)', end='', flush=True)
    updater.update(ApplicationPackage(image), enter_boot=not args.no_enter_boot,
                   flash_fw=False, progress=progress)
    print('\nFlash complete; device re-enumerated in application mode.')
    print(f'After flash: device version {device.query_version().hex(" ")}')

    if args.keep_settings:
        print('Cold boot skipped (--keep-settings): stored calibration and settings are kept.')
    else:
        try:
            state = cold_boot()
        except Exception as error:  # noqa: BLE001 - report, never hide a partial result
            print(f'COLD BOOT FAILED: {error}')
            print(f'Application flashed, sha256 {digest}')
            return 1
        detail = (f'velocity start {state.velocity_start}, press {state.press[0]}, '
                  f'{"MIDI" if state.performance_mode else "keyboard"} mode, '
                  f'{state.count} sensors') if state else 'no telemetry read back'
        print(f'Cold boot: stored calibration and Fn-menu settings erased ({detail}); '
              'the device is at its defaults.')
    print(f'SUCCESS: application flashed, sha256 {digest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
