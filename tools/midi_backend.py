"""Bounded callback-based RtMidi backend for the dedicated control cable."""
import queue
import re
import sys
from pathlib import Path
from firmware_defaults import DEFAULTS as D


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
    return 'MIDI-Typist Control' in name or bool(re.fullmatch(
        r'Huntsman V3 Pro Mini MIDI:Huntsman V3 Pro Mini MIDI MIDI- \d+:1', name))


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


def alsa_client_card(client):
    """Read the kernel sound-card association, not the editable MIDI port name."""
    import ctypes as c
    from ctypes.util import find_library
    if not sys.platform.startswith('linux'):
        raise RuntimeError('Physical USB/MIDI binding currently requires Linux ALSA')
    library=find_library('asound')
    if not library:raise RuntimeError('libasound is required to bind the selected MIDI device')
    api=c.CDLL(library)
    signatures={
        'snd_seq_open':([c.POINTER(c.c_void_p),c.c_char_p,c.c_int,c.c_int],c.c_int),
        'snd_seq_close':([c.c_void_p],c.c_int),
        'snd_seq_client_info_malloc':([c.POINTER(c.c_void_p)],c.c_int),
        'snd_seq_client_info_free':([c.c_void_p],None),
        'snd_seq_get_any_client_info':([c.c_void_p,c.c_int,c.c_void_p],c.c_int),
        'snd_seq_client_info_get_card':([c.c_void_p],c.c_int),
    }
    for name,(args,result) in signatures.items():
        function=getattr(api,name);function.argtypes=args;function.restype=result
    sequence,info=c.c_void_p(),c.c_void_p()
    def checked(result):
        if result<0:raise RuntimeError(f'ALSA device identity query failed ({result})')
    checked(api.snd_seq_open(c.byref(sequence),b'hw',2,1)) # input, nonblocking
    try:
        checked(api.snd_seq_client_info_malloc(c.byref(info)))
        checked(api.snd_seq_get_any_client_info(sequence,client,info))
        return api.snd_seq_client_info_get_card(info)
    finally:
        if info:api.snd_seq_client_info_free(info)
        api.snd_seq_close(sequence)


def control_port_for_usb(usb_path, sound_sysfs='/sys/class/sound'):
    """Require exactly one control port belonging to this physical USB device.

    Virtual clients, another keyboard and ambiguous control ports are rejected;
    never fall back to a friendly-name match before a destructive command.
    """
    usb=Path(usb_path).resolve(strict=True);matches=[]
    for port in control_ports():
        address=re.search(r' (\d+):(\d+)$',port)
        if not address:continue
        card=alsa_client_card(int(address[1]))
        if card<0:continue
        try:owner=(Path(sound_sysfs)/f'card{card}'/'device').resolve(strict=True)
        except OSError:continue
        if owner==usb or usb in owner.parents:matches.append(port)
    if len(matches)!=1:
        raise RuntimeError('No unique MIDI control port belongs to the selected USB device')
    return matches[0]


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
