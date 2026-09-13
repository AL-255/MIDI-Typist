#!/usr/bin/env python3
"""Offline tests for the integrated flasher. They never touch hardware.

The updater call itself is replaced, so these tests prove the wiring the GUI
and the CLI share - image validation, submodule lookup, options, progress and
status reporting - and never flash or reset a device.
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import firmware_flasher as flasher


def image_file(directory, size=flasher.APP_IMAGE_BYTES):
    path = Path(directory)/'huntsman_firmware.bin'
    path.write_bytes(bytes((index*7) & 0xff for index in range(size)))
    return path


class Tests(unittest.TestCase):
    def test_image_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = image_file(directory, 1024)
            with self.assertRaises(flasher.ImageError): flasher.image_digest(path)
            path = image_file(directory)
            size,digest = flasher.image_digest(path)
            self.assertEqual((size,len(digest)),(flasher.APP_IMAGE_BYTES,64))
            self.assertEqual(digest,__import__('hashlib').sha256(path.read_bytes()).hexdigest())
            with self.assertRaises(flasher.ImageError): flasher.image_digest(Path(directory)/'missing.bin')

    def test_updater_location(self):
        # The vendored submodule is the default source, and the documented
        # override lets a missing checkout be reported instead of crashing.
        self.assertTrue(flasher.updater_available())
        self.assertTrue((flasher.updater_src()/'huntsman_updater'/'updater.py').is_file())
        with patch.dict(os.environ,{'HUNTSMAN_UPDATER_SRC':'/nonexistent/updater'}):
            self.assertFalse(flasher.updater_available())
            with self.assertRaises(flasher.FlasherError) as caught: flasher.load_updater()
            self.assertIn(flasher.INIT_HINT,str(caught.exception))

    def test_flash_flow_without_hardware(self):
        with tempfile.TemporaryDirectory() as directory:
            path = image_file(directory)
            calls,events = [],[]
            def update(package,**options):
                calls.append((package,options))
                options['progress'](64,flasher.APP_IMAGE_BYTES)   # the updater drives progress
            fake = (SimpleNamespace(APP_PID=1,BOOTLOADER_PID=2),
                    SimpleNamespace(query_version=lambda: b'\x02\x01'),
                    SimpleNamespace(update=update),
                    lambda data: data)
            with patch.object(flasher,'load_updater',return_value=fake):
                result = flasher.flash_image(path,progress=lambda done,total: events.append(('progress',done,total)),
                                             status=events.append,cold_boot_after=False)
            package,options = calls[0]
            self.assertEqual(len(calls),1)
            # Application region only: never the secondary controller images.
            self.assertFalse(options['flash_fw'])
            self.assertTrue(options['enter_boot'])
            self.assertIsNone(package.flash_image)
            self.assertEqual(package.app_image,path.read_bytes())
            self.assertTrue(result.cold_boot_skipped and result.cold_boot is None)
            self.assertEqual(result.digest,flasher.image_digest(path)[1])
            self.assertIn(('progress',64,flasher.APP_IMAGE_BYTES),events)
            self.assertTrue(any(isinstance(event,str) and 'flashing' in event for event in events))
            # An image the updater would reject never reaches it.
            calls.clear()
            short = image_file(directory, 1024)
            with patch.object(flasher,'load_updater',return_value=fake):
                with self.assertRaises(flasher.ImageError): flasher.flash_image(short)
            self.assertFalse(calls)

    def test_cold_boot_requires_a_device(self):
        # With no CDC device present the cold boot reports instead of hanging.
        clock = iter(range(1000))
        with patch.object(flasher.time,'monotonic',side_effect=lambda: next(clock)), \
             patch.object(flasher.time,'sleep'), \
             patch('keyboard_gui_transport.find_cdc_device',return_value=None):
            with self.assertRaises(flasher.FlasherError) as caught: flasher.cold_boot(timeout=1.0,attempts=2)
            self.assertIn('no CDC device',str(caught.exception))


if __name__ == '__main__':
    unittest.main()
