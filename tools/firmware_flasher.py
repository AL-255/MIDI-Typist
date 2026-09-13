#!/usr/bin/env python3
"""Application flashing shared by the CLI wrapper and the configuration GUI.

The device is programmed through the vendored updater submodule
(`third_party/huntsman_updater`, project `Huntsman-V3-Pro-Mini-Flasher`) using
its reviewed application-only channel-0x10 DFU path: the bootloader,
factory/security data, primary settings and secondary-controller regions are
never written. After the flash the helper performs the cold boot described in
[docs/DEVICE_CONFIG_STORAGE.md](../docs/DEVICE_CONFIG_STORAGE.md): it sends
`cfg clean` so the new build starts from defaults instead of inheriting the
previous build's stored calibration and Fn-menu settings.

Flashing needs raw USB access, so it runs as root or with suitable udev rules.
Nothing here flashes implicitly: every entry point is an explicit caller
request, and no test invokes it.
"""
from dataclasses import dataclass, field
from pathlib import Path
import hashlib
import os
import sys
import time

REPO_ROOT = Path(__file__).resolve().parent.parent
SUBMODULE = REPO_ROOT / 'third_party' / 'huntsman_updater'
INIT_HINT = 'git submodule update --init third_party/huntsman_updater'
APP_IMAGE_BYTES = 0x20000


class FlasherError(RuntimeError):
    """Flashing cannot proceed; the message is safe to show to a user."""


class ImageError(FlasherError):
    """The chosen file is not a usable application image."""


@dataclass
class FlashResult:
    size: int
    digest: str
    cold_boot: object = None   # Snapshot after the cold boot, when one ran
    cold_boot_skipped: bool = False
    notes: list = field(default_factory=list)


def updater_src():
    """Directory holding the `huntsman_updater` package, or None when absent.

    `HUNTSMAN_UPDATER_SRC` overrides the submodule path, which also lets tests
    exercise the missing-submodule path without touching the checkout.
    """
    override = os.environ.get('HUNTSMAN_UPDATER_SRC')
    src = Path(override).expanduser() if override else SUBMODULE / 'src'
    return src if (src / 'huntsman_updater' / '__init__.py').is_file() else None


def updater_available():
    return updater_src() is not None


def load_updater():
    """Import the vendored updater package, or explain how to get it."""
    src = updater_src()
    if src is None:
        raise FlasherError('the updater submodule is missing; run: ' + INIT_HINT)
    if str(src) not in sys.path:
        sys.path.insert(0, str(src))
    from huntsman_updater import constants, device, updater
    from huntsman_updater.firmware import validate_app_image
    return constants, device, updater, validate_app_image


def image_digest(path):
    """(size, sha256) of a local 128 KiB application image."""
    image = Path(path)
    try:
        data = image.read_bytes()
    except OSError as error:
        raise ImageError(f'{image}: {error}') from error
    if len(data) != APP_IMAGE_BYTES:
        raise ImageError(f'{image}: {len(data)} bytes, expected a 128 KiB application image')
    return len(data), hashlib.sha256(data).hexdigest()


class ApplicationPackage:
    """Raw application-only image; no secondary FlashFW content."""

    def __init__(self, image, constants):
        self.pid = constants.APP_PID
        self.bootloader_pid = constants.BOOTLOADER_PID
        self.app_image = image
        self.flash_image = None


def cold_boot(timeout=40.0, attempts=12, wait=1.0, status=None):
    """Clear the configuration the freshly flashed build would inherit.

    The device erases both authorized pages and reports result 1 only after
    reading them back blank, then applies defaults on the first neutral frame.
    It refuses the command until its scan is valid, and a rejected attempt ends
    the connection, so each attempt reconnects and waits for valid telemetry.
    """
    from keyboard_gui_transport import Connection, find_cdc_device

    deadline = time.monotonic() + timeout
    path = None
    while not path and time.monotonic() < deadline:
        path = find_cdc_device()
        if not path:
            time.sleep(0.5)
    if not path:
        raise FlasherError('no CDC device found after flashing; reconnect and send `cfg clean` manually')
    last = 'no attempt completed'
    for attempt in range(attempts):
        if time.monotonic() >= deadline:
            break
        connection = Connection(path)
        connection.start()
        try:
            confirmed = False
            while time.monotonic() < deadline:
                try:
                    event = connection.events.get(timeout=0.1)
                except Exception:  # queue.Empty
                    event = ''
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
                if not confirmed and connection.connected and latest and latest[1].flags & 4:
                    connection.submit('clean')
                    confirmed = True
            else:
                last = 'no confirmation before the timeout'
        finally:
            connection.stop(); connection.join(timeout=2)
        if attempt + 1 < attempts:
            if status:
                status(f'cold boot attempt {attempt+1} not accepted yet ({last}); retrying')
            time.sleep(wait)
    raise FlasherError(f'cold boot not confirmed after {attempts} attempts ({last})')


def flash_image(path, progress=None, status=None, cold_boot_after=True, enter_boot=True):
    """Flash one application image, then cold-boot unless told otherwise.

    `progress(done, total)` reports programming progress; `status(text)`
    reports human-readable steps. Both are called from the calling thread.
    """
    constants, device, updater, validate_app_image = load_updater()
    size, digest = image_digest(path)
    try:
        image = validate_app_image(Path(path).read_bytes())
    except Exception as error:  # noqa: BLE001 - the updater raises its own types
        raise ImageError(f'{path}: the updater rejected this image ({error})') from error
    if status:
        status(f'flashing {size} bytes, sha256 {digest}')
    try:
        before = device.query_version().hex(' ')
    except Exception:  # noqa: BLE001 - device may be absent/unreadable
        before = 'unavailable'
    if status:
        status(f'device version before flash: {before}')
    updater.update(ApplicationPackage(image, constants), enter_boot=enter_boot,
                   flash_fw=False, progress=progress)
    result = FlashResult(size=size, digest=digest)
    if not cold_boot_after:
        result.cold_boot_skipped = True
        return result
    if status:
        status('flash complete; clearing stored configuration (cold boot)')
    result.cold_boot = cold_boot(status=status)
    return result
