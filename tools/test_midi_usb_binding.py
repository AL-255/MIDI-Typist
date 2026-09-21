"""Offline USB-to-ALSA control-port selection; no MIDI clients or USB opened."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import midi_backend as midi


class BindingTests(unittest.TestCase):
    def test_physical_parent_and_unique_control_port(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);sound=root/'sound';sound.mkdir()
            usb=root/'3-2.1';usb.mkdir();iface=usb/'3-2.1:1.2';iface.mkdir()
            other=root/'3-3';other.mkdir()
            for card,owner in ((1,iface),(2,other)):
                path=sound/f'card{card}';path.mkdir();(path/'device').symlink_to(owner)
            ports=['M1:MIDI-Typist Control 128:1','Other:MIDI-Typist Control 129:1',
                   'Virtual:MIDI-Typist Control 130:1']
            cards={128:1,129:2,130:-1}
            with patch.object(midi,'control_ports',return_value=ports), \
                 patch.object(midi,'alsa_client_card',side_effect=cards.__getitem__):
                self.assertEqual(midi.control_port_for_usb(usb,sound),ports[0])
                cards[128]=2
                with self.assertRaises(RuntimeError):midi.control_port_for_usb(usb,sound)
                cards[128]=cards[129]=1
                with self.assertRaises(RuntimeError):midi.control_port_for_usb(usb,sound)


if __name__=='__main__':unittest.main()
