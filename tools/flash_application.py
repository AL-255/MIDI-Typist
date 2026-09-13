#!/usr/bin/env python3
"""Flash a built 128 KiB application image with the vendored updater.

Uses the reviewed application-only path from the updater submodule
(`third_party/huntsman_updater`): enter the bootloader, stream the image over
the channel-0x10 DFU interface and wait for the device to re-enumerate in
application mode. The bootloader, factory/security data, primary settings and
secondary-controller regions are never written. Requires root for
/dev/bus/usb access:

    printf 'PASSWORD\\n' | sudo -S -p '' python3 tools/flash_application.py \\
        build-keyboard-fn-menu/huntsman_firmware.bin

After the flash the script performs the **cold boot**: it opens the
application's CDC port and sends `cfg clean`, the host equivalent of Fn+R. A
freshly flashed build therefore starts from its own defaults instead of
inheriting the stored calibration of the previous one. Pass
--keep-calibration to skip that step and keep the stored calibration.

The same steps are available from the configuration GUI's **Flash
application...** control, which runs this script elevated when it has to.
Record the printed sha256; the device is queried before and after the flash.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from firmware_flasher import FlasherError, INIT_HINT, flash_image, image_digest, updater_available


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='128 KiB raw application image (huntsman_firmware.bin)')
    parser.add_argument('--no-enter-boot', action='store_true',
                        help='device is already in the bootloader (1532:110E)')
    parser.add_argument('--keep-calibration', action='store_true',
                        help='skip the cold boot and keep the stored calibration')
    args = parser.parse_args()
    try:
        size, digest = image_digest(args.image)
    except FlasherError as error:
        print(f'ERROR: {error}')
        return 1
    print(f'Application image: {size} bytes, sha256 {digest}')
    if not updater_available():
        print(f'ERROR: updater unavailable; run: {INIT_HINT}')
        return 1

    def progress(done, total):
        print(f'\r  program {done}/{total} ({done*100//total}%)', end='', flush=True)

    def status(text):
        print(f'  {text}')

    try:
        result = flash_image(args.image, progress=progress, status=status,
                             cold_boot_after=not args.keep_calibration,
                             enter_boot=not args.no_enter_boot)
    except FlasherError as error:
        print(f'\nERROR: {error}')
        return 1
    print('\nFlash complete; device re-enumerated in application mode.')
    if result.cold_boot_skipped:
        print('Cold boot skipped (--keep-calibration): the stored calibration is kept.')
    else:
        state = result.cold_boot
        detail = (f'velocity start {state.velocity_start}, press {state.press[0]}, '
                  f'{"MIDI" if state.performance_mode else "keyboard"} mode, '
                  f'{state.count} sensors') if state else 'no telemetry read back'
        print(f'Cold boot: stored calibration erased ({detail}); '
              'the device is at its defaults.')
    print(f'SUCCESS: application flashed, sha256 {digest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
