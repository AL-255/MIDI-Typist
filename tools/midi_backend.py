"""Process-isolated, bounded RtMidi transport for the GUI control cable.

Only the private child opens MIDI ports. RtMidi close can join a native callback
thread while holding Python's GIL; containing that operation in a child keeps
disconnect/reflash bounded without discarding capture errors or retrying sends.
"""
import multiprocessing
import queue
import re
import sys
import time
from pathlib import Path
from firmware_defaults import DEFAULTS as D
from midi_sysex import MAX_WIRE


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


class _NativeMidi:
    """Child-only native owner; never construct this in the GUI process."""
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
        if len(message)>MAX_WIRE:
            self.error = BufferError('Oversized MIDI control message; capture invalid')
            return
        try: self.messages.put_nowait(bytes(message))
        except queue.Full: self.error = BufferError('MIDI receive buffer overflow; capture invalid')

    def send(self, message):
        if self.error: raise self.error
        self.outgoing.send_message(message)
        if self.error: raise self.error

    def receive(self, timeout):
        if self.error: raise self.error
        try: return self.messages.get(timeout=timeout)
        except queue.Empty: return None

    def close(self):
        self.incoming.close_port(); self.outgoing.close_port()
        self.incoming.delete(); self.outgoing.delete()


def _midi_worker(name, commands, replies, received, stop, failed, detail, native_factory):
    """Private GUI child, not a command-line application or a device protocol."""
    for channel in (commands,replies,received):channel.cancel_join_thread()
    native=None
    try:
        native=native_factory(name)
        replies.put_nowait(0)  # opened, not a firmware/session acknowledgement
        while not stop.is_set():
            try: sequence,wire=commands.get_nowait()
            except queue.Empty: pass
            else:
                native.send(wire)
                replies.put_nowait(sequence)
            wire=native.receive(D['MIDI_CONTROL_HOST_POLL_MS']/1000)
            if wire is not None:
                if not 0<len(wire)<=MAX_WIRE:raise BufferError('Invalid MIDI control message size')
                try:received.put_nowait(wire)
                except queue.Full:raise BufferError('MIDI worker receive buffer overflow; capture invalid') from None
    except Exception as error:
        # Publish failure independently of the data queue: overflow must never
        # become a silently missing sample or be hidden behind queued packets.
        kind=b'B' if isinstance(error,(BufferError,queue.Full)) else b'E'
        detail.value=kind+str(error).encode('utf-8','replace')[:len(detail)-2]
        failed.set()
    finally:
        if native is not None:native.close()  # may deadlock; parent owns deadline


class MidiBackend:
    """Single-owner interface; no device command is resent after an IPC failure."""
    def __init__(self,name,*,native_factory=_NativeMidi):
        context=multiprocessing.get_context('spawn')
        self.commands=context.Queue(maxsize=1)
        self.replies=context.Queue(maxsize=1)
        self.received=context.Queue(maxsize=D['MIDI_CONTROL_HOST_MESSAGES'])
        self.channels=(self.commands,self.replies,self.received)
        for channel in self.channels:channel.cancel_join_thread()
        self.stop=context.Event();self.failed=context.Event()
        self.detail=context.Array('c',D['MIDI_CONTROL_HOST_ERROR_BYTES'],lock=False)
        self.closed=False;self.error=None;self.sequence=0
        self.process=context.Process(target=_midi_worker,
            args=(name,*self.channels,self.stop,self.failed,self.detail,native_factory),
            name='MIDI-Typist MIDI transport',daemon=True)
        try:
            self.process.start()
            self._reply(0)
        except Exception:
            self.close()
            raise

    def _check(self):
        if self.error:raise self.error
        if self.closed:raise OSError('MIDI connection is closed')
        if self.failed.is_set():
            detail=self.detail.value
            error=BufferError if detail[:1]==b'B' else OSError
            self.error=error(detail[1:].decode('utf-8','replace') or 'MIDI transport failed')
        elif self.process.exitcode is not None:
            self.error=OSError('MIDI transport exited; capture invalid')
        if self.error:raise self.error

    def _reply(self,sequence):
        deadline=time.monotonic()+D['MIDI_CONTROL_COMMAND_TIMEOUT_MS']/1000
        while True:
            self._check()
            remaining=deadline-time.monotonic()
            if remaining<=0:
                self.error=TimeoutError('MIDI transport did not complete; no retry')
                raise self.error
            try:reply=self.replies.get(timeout=min(remaining,D['MIDI_CONTROL_HOST_POLL_MS']/1000))
            except queue.Empty:continue
            self._check()
            if reply!=sequence:
                self.error=OSError('Unexpected MIDI transport completion; no retry')
                raise self.error
            return

    def send(self,message):
        self._check()
        wire=bytes(message)
        if not 0<len(wire)<=MAX_WIRE:raise ValueError('Invalid MIDI control message size')
        self.sequence+=1
        try:self.commands.put_nowait((self.sequence,wire))
        except queue.Full:
            self.error=BufferError('MIDI command buffer full; no retry')
            raise self.error from None
        self._reply(self.sequence)

    def receive(self,timeout):
        deadline=time.monotonic()+max(0,timeout)
        while True:
            self._check()
            remaining=max(0,deadline-time.monotonic())
            try:wire=self.received.get(timeout=min(remaining,D['MIDI_CONTROL_HOST_POLL_MS']/1000))
            except queue.Empty:
                if time.monotonic()>=deadline:
                    self._check();return None
                continue
            self._check()
            return wire

    def close(self):
        if self.closed:return
        self.closed=True;self.stop.set()
        # Terminate only our native MIDI child, never a flash worker or device.
        # Reaping it proves OS port handles are gone before a new owner opens.
        if self.process.pid is not None:
            self.process.join(D['MIDI_CONTROL_HOST_CLOSE_MS']/1000)
            if self.process.is_alive():
                self.process.terminate();self.process.join(D['MIDI_CONTROL_HOST_REAP_MS']/1000)
            if self.process.is_alive():
                self.process.kill();self.process.join(D['MIDI_CONTROL_HOST_REAP_MS']/1000)
            if self.process.is_alive():
                self.closed=False
                self.error=OSError('Cannot stop MIDI transport; do not start flashing')
                raise self.error
        for channel in self.channels:channel.close()
        self.process.close()
