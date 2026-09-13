"""Private application-only package and pinned updater dependency loader."""
from pathlib import Path
import os
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent
SUBMODULE = REPO_ROOT / 'third_party' / 'huntsman_updater'
INIT_HINT = 'git submodule update --init third_party/huntsman_updater'
APP_IMAGE_BYTES = 0x20000


class FlasherError(RuntimeError):
    """Flashing cannot proceed; the message is safe to show to a user."""


class ImageError(FlasherError):
    """The chosen file is not a usable application image."""


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


class ApplicationPackage:
    """Raw application-only image; no secondary FlashFW content."""

    def __init__(self, image, constants):
        self.pid = constants.APP_PID
        self.bootloader_pid = constants.BOOTLOADER_PID
        self.app_image = image
        self.flash_image = None
