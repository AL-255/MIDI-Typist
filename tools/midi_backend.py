"""Bounded callback-based RtMidi backend for the dedicated control cable."""
import queue
import re
import sys
from firmware_defaults import DEFAULTS as D
from keyboard_boards import BOARDS


def clients():
    try:
        import rtmidi
    except ImportError as error:
        raise RuntimeError('Install GUI dependencies: pip install -r tools/requirements-gui.txt') from error
    api = rtmidi.API_LINUX_ALSA if sys.platform.startswith('linux') else rtmidi.API_UNSPECIFIED
    return rtmidi.MidiIn(rtapi=api, name='MIDI-Typist GUI'), rtmidi.MidiOut(rtapi=api, name='MIDI-Typist GUI')


def is_control_port(name):
    # ALSA limits raw-MIDI subdevice names to 31 bytes: the product prefix
    # truncates the jack string to "MIDI-". Match this board's second cable,
    # never its performance cable. The versioned handshake verifies the peer.
    return 'MIDI-Typist Control' in name or any(re.fullmatch(
        re.escape(board.midi_product)+':'+re.escape(board.midi_product)+r' MIDI-[^:]* \d+:1',name)
        for board in BOARDS.values())


def control_ports():
    incoming, outgoing = clients()
    try:
        outputs = outgoing.get_ports()
        return [name for name in incoming.get_ports() if is_control_port(name) and name in outputs]
    finally:
        incoming.delete(); outgoing.delete()


def find_midi_device():
    ports = control_ports()
    return ports[0] if len(ports) == 1 else None


class MidiBackend:
    def __init__(self, name):
        self.incoming, self.outgoing = clients()
        self.messages = queue.Queue(maxsize=D['MIDI_CONTROL_HOST_MESSAGES'])
        self.error = None
        try:
            self.incoming.ignore_types(sysex=False)
            self.incoming.set_callback(self._receive)
            self.incoming.set_error_callback(self._error)
            self.outgoing.set_error_callback(self._error)
            self.incoming.open_port(self.incoming.get_ports().index(name))
            self.outgoing.open_port(self.outgoing.get_ports().index(name))
        except Exception:
            self.close()
            raise

    def _error(self, kind, message, data=None):
        self.error = OSError(message)

    def _receive(self, event, data=None):
        message, _ = event
        if not message or message[0] != 0xf0: return
        try: self.messages.put_nowait(bytes(message))
        except queue.Full: self.error = BufferError('MIDI receive buffer overflow; capture invalid')

    def send(self, message):
        if self.error: raise self.error
        self.outgoing.send_message(message)

    def receive(self, timeout):
        if self.error: raise self.error
        try: return self.messages.get(timeout=timeout)
        except queue.Empty: return None

    def close(self):
        self.incoming.close_port(); self.outgoing.close_port()
        self.incoming.delete(); self.outgoing.delete()
