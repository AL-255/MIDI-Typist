"""Single-owner MIDI SysEx control and loss-detecting sensor capture."""
from collections import deque
import queue
import secrets
import threading
import time
from firmware_defaults import DEFAULTS as D
from keyboard_gui_model import decode, parse_build, MAX_KEYS
from keyboard_boards import get_board
from keyboard_capture import KeyDecoder
from midi_backend import MidiBackend, find_midi_device
import midi_sysex as sx

SAMPLE_CAPACITY = D['MIDI_CONTROL_CAPTURE_SAMPLES']

class Connection(threading.Thread):
    def __init__(self,path,backend_factory=MidiBackend):
        super().__init__(daemon=True)
        self.path = path
        self.backend_factory = backend_factory
        self.session = secrets.randbelow(0xffffffff)+1
        self.sequence = 0
        self.stop_event = threading.Event()
        # A profile queues thresholds plus MIDI and keyboard maps for every
        # protocol-supported sensor, with disable/enable around the batch.
        self.requests = queue.Queue(maxsize=3*MAX_KEYS+2)
        self.events = queue.Queue(maxsize=128)
        self.lock = threading.Lock()
        self.latest = None
        self.build = None        # build identity from `version`, e.g. v0.1.0-RZ03-0499
        self.build_target = None # its board target, e.g. RZ03-0499
        self.connected = False
        self.next_id = secrets.randbelow(0xfffffffe)+1
        self.stream_requests = deque(maxlen=1)  # latest requested display mode wins
        self.stream_mode = 'gui'  # 'gui' (MTG3 telemetry) or 'key' (HKL1 samples)
        self.key_threshold = self.key_sensor = self.key_session = None
        self.samples = deque(maxlen=SAMPLE_CAPACITY)
        self.samples_lock = threading.Lock()

    def notify(self,text):
        try: self.events.put_nowait(text)
        except queue.Full: pass  # bounded informational log, never command/state data

    def submit(self,action,*args):
        self.requests.put_nowait((action,args))

    def stream_key(self,threshold,sensor):
        """Switch the device to the full-rate per-key stream for one sensor."""
        self.stream_requests.append(('key',threshold,sensor,secrets.randbelow(0xfffffffe)+1))

    def stream_gui(self):
        """Switch the device back to the latest-only GUI telemetry stream."""
        self.stream_requests.append(('gui',))

    def drain_samples(self,sensor=None):
        with self.samples_lock:
            if sensor is not None and sensor != self.key_sensor: return []
            if not self.samples: return []
            data = list(self.samples)
            self.samples.clear()
        return data

    def clear_samples(self):
        with self.samples_lock: self.samples.clear()

    def push_sample(self,raw):
        with self.samples_lock:
            if len(self.samples) == SAMPLE_CAPACITY:
                self.samples.clear()
                raise BufferError('Host capture buffer overflow; waveform invalid, reconnect to re-arm')
            self.samples.append(raw)

    def snapshot(self):
        with self.lock: return self.latest

    def stop(self): self.stop_event.set()

    def confirm(self, snapshot, action, args):
        if action == 'set':
            index,press,release = args
            if index >= snapshot.count or (snapshot.press[index],snapshot.release[index]) != (press,release):
                raise ValueError('Threshold readback differs from requested values')
        if action == 'all':
            if not snapshot.count or any((p,r) != args for p,r in zip(snapshot.press,snapshot.release)):
                raise ValueError('All-key threshold readback differs from requested values')
        if action == 'enable' and bool(snapshot.flags & 1) != bool(args[0]):
            raise ValueError('Enable readback differs from requested state')
        if action == 'midi':
            index,note = args
            if index >= snapshot.count or snapshot.midi_mapping[index] != note:
                raise ValueError('MIDI mapping readback differs from requested values')
        if action == 'key':
            index,usage = args
            if index >= snapshot.count or snapshot.keyboard_mapping[index] != usage:
                raise ValueError('Keyboard mapping readback differs from requested value')
        # `clean` is confirmed by its ACK alone: the device
        # verified the erase by reading both pages back blank
        # before answering result 1.
        if action == 'velocity':
            level, = args
            if not 1 <= level <= 10 or snapshot.velocity_start != level:
                raise ValueError('Velocity start readback differs from requested value')

        self.connected = True
        self.notify(f'Confirmed {action} {args}' if action != 'get' else 'Connected: device telemetry acknowledged')

    def receive(self, timeout):
        wire = self.backend.receive(timeout)
        if wire is None: return None
        message = sx.decode(wire)
        return message if message[1] == self.session else None

    def command(self, command, action=None, args=()):
        payload = command.encode('ascii')
        if len(payload) > D['MIDI_CONTROL_COMMAND_MAX']: raise ValueError('Command too long')
        self.sequence += 1
        self.backend.send(sx.encode(sx.COMMAND, self.session, self.sequence, payload))
        deadline = time.monotonic()+D['MIDI_CONTROL_COMMAND_TIMEOUT_MS']/1000
        acknowledged, confirmed, ack_payload = False, None, b''
        while not self.stop_event.is_set():
            message = self.poll()
            if message:
                kind, _, sequence, payload = message
                if kind == sx.ERROR: raise ValueError('Device control error: '+payload.decode('ascii', 'replace'))
                if kind == sx.ACK and sequence == self.sequence:
                    acknowledged, ack_payload = True, payload
                if kind == sx.SNAPSHOT and action:
                    snapshot = decode(payload)
                    if snapshot.ack == self.next_id:
                        if snapshot.result != 1: raise ValueError('Device rejected configuration; remaining changes cancelled')
                        confirmed = snapshot
            if acknowledged and (action is None or confirmed is not None):
                if action: self.confirm(confirmed, action, args)
                return ack_payload
            if time.monotonic() >= deadline:
                raise TimeoutError('Command not acknowledged; no retry. Remaining changes cancelled.')

    def poll(self):
        now = time.monotonic()
        if now-self.heartbeat >= D['MIDI_CONTROL_HEARTBEAT_MS']/1000:
            self.backend.send(sx.encode(sx.KEEPALIVE, self.session)); self.heartbeat = now
        message = self.receive(.01)
        if message:
            kind, _, _, payload = message
            if kind == sx.SNAPSHOT and self.stream_mode == 'gui':
                snapshot = decode(payload)
                if not self.build_target or not get_board(self.build_target).validates_wire(snapshot):
                    raise ValueError('Snapshot layout/report/rate contradicts the identified board')
                self.last_rx = time.monotonic()
                with self.lock: self.latest = self.last_rx, snapshot
            elif kind == sx.SAMPLES and self.stream_mode == 'key':
                if not payload or len(payload) % 20: raise ValueError('Invalid sample batch')
                for raw in self.key_decoder.feed(payload):
                    self.last_rx = time.monotonic()
                    if raw is not None:
                        if self.key_decoder.key != self.key_sensor: raise ValueError('Unexpected capture sensor')
                        self.push_sample(raw)
            elif kind == sx.LOG:
                message_text=payload.decode('ascii', 'replace').strip()
                if message_text.startswith('Boot failed: '):
                    raise RuntimeError(message_text+'; configuration is unavailable. The control USB link remains available for recovery.')
                self.notify(message_text)
            elif kind == sx.ERROR:
                raise ValueError('Device control error: '+payload.decode('ascii', 'replace'))
        if time.monotonic()-self.last_rx > D['MIDI_CONTROL_LEASE_MS']/1000:
            raise TimeoutError('Telemetry stale/disconnected. No further configuration sent.')
        return message

    def run(self):
        self.backend = None
        self.key_decoder = None
        try:
            self.backend = self.backend_factory(self.path)
            self.backend.send(sx.encode(sx.HELLO, self.session))
            deadline = time.monotonic()+D['MIDI_CONTROL_COMMAND_TIMEOUT_MS']/1000
            while not self.stop_event.is_set():
                message = self.receive(.02)
                if message and message[0] == sx.READY and message[2] == 0:
                    found = parse_build(message[3]+b'\n')
                    if not found: raise ValueError('Invalid device build identity')
                    self.build, self.build_target = found[0], found[2]
                    get_board(self.build_target)  # reject an unknown board before configuration commands
                    self.notify(f'Device build {self.build}')
                    break
                if time.monotonic() >= deadline: raise TimeoutError('MIDI SysEx handshake timed out')
            if self.stop_event.is_set(): return
            self.heartbeat = self.last_rx = time.monotonic()
            self.command('stream gui')
            self.command(f'cfg get {self.next_id}', 'get')
            while not self.stop_event.is_set():
                if self.stream_requests and (self.stream_mode == 'key' or self.requests.empty()):
                    request = self.stream_requests.popleft()
                    if request[0] == 'key':
                        _, threshold, sensor, session = request
                        if self.stream_mode == 'key':
                            # End the previous capture explicitly. Its immutable
                            # in-flight packets precede this ACK on the same
                            # endpoint; none belong to the new capture.
                            self.stream_mode = 'gui'
                            self.key_decoder = None
                            self.command('stream off')
                        self.stream_mode = 'key'
                        with self.samples_lock:
                            self.key_threshold, self.key_sensor, self.key_session = threshold, sensor, session
                            self.samples.clear()
                        self.key_decoder = KeyDecoder(threshold, session, self.snapshot()[1].count)
                        self.command(f'stream key {threshold} {session} {sensor}')
                    else:
                        self.stream_mode = 'gui'
                        self.key_decoder = None
                        self.key_threshold = self.key_sensor = self.key_session = None
                        self.command('stream gui')
                elif self.stream_mode == 'gui':
                    try: action, args = self.requests.get_nowait()
                    except queue.Empty: pass
                    else:
                        self.next_id = self.next_id % 0xffffffff+1
                        self.command('cfg '+action+' '+str(self.next_id)+''.join(' '+str(v) for v in args), action, args)
                self.poll()
        except Exception as error:
            self.notify(f'ERROR: {error}')
        finally:
            self.connected = False
            if self.backend is not None:
                try: self.backend.send(sx.encode(sx.CLOSE, self.session))
                except Exception: pass
                self.backend.close()
            if self.stop_event.is_set():
                self.notify('Disconnected; keyboard operation does not depend on the GUI')
