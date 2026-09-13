"""Single-owner POSIX CDC worker; acknowledged writes and stream multiplexing.

The device serves exactly one CDC stream at a time: latest-only GUI
telemetry, or the full-rate (8 ksps) per-key HKL1 stream. The worker keeps the
acknowledged GUI configuration channel and switches decoders when the GUI
requests the keystroke-capture stream, delivering every raw sample through a
bounded drop-oldest buffer the GUI drains per frame.
"""
from collections import deque
import fcntl
import glob
import os
import queue
import secrets
import select
import termios
import threading
import time
import tty
from keyboard_gui_model import Decoder, parse_build
from last_key_stream import KeyDecoder

USB_VENDOR_ID = 0x1532
USB_PRODUCT_ID = 0x02b0

SAMPLE_CAPACITY = 16384  # 2 s of 8 ksps samples; oldest drop silently


def usb_identity(path):
    """Read the (vendor, product) pair of a sysfs USB device, or None."""
    try:
        with open(os.path.join(path,'idVendor'),encoding='ascii') as vendor_file:
            vendor = vendor_file.read().strip()
        with open(os.path.join(path,'idProduct'),encoding='ascii') as product_file:
            product = product_file.read().strip()
        return int(vendor,16), int(product,16)
    except (OSError,ValueError):
        return None


def find_cdc_device(vendor=USB_VENDOR_ID, product=USB_PRODUCT_ID, sysfs='/sys', dev='/dev'):
    """First connectable CDC-ACM node of a USB device vendor:product, else None.

    Each /sys/class/tty/ttyACM* entry links through its `device` entry to the
    USB interface, whose parent is the physical USB device carrying idVendor
    and idProduct. The device node must also exist under `dev` (created by
    udev on a normal desktop). Ports are scanned in name order; with several
    matching boards connected, pass an explicit --device instead.
    """
    for tty in sorted(glob.glob(os.path.join(sysfs,'class','tty','ttyACM*'))):
        node = os.path.realpath(os.path.join(tty,'device'))
        while node and node != '/':
            identity = usb_identity(node)
            if identity is not None:
                if identity == (vendor,product) and os.path.exists(os.path.join(dev,os.path.basename(tty))):
                    return os.path.join(dev,os.path.basename(tty))
                break  # first USB ancestor decides; try the next port
            parent = os.path.dirname(node)
            if parent == node: break
            node = parent
    return None


class Connection(threading.Thread):
    def __init__(self,path):
        super().__init__(daemon=True)
        self.path = path
        self.stop_event = threading.Event()
        self.requests = queue.Queue(maxsize=128)
        self.events = queue.Queue(maxsize=128)
        self.lock = threading.Lock()
        self.latest = None
        self.build = None        # build identity from `version`, e.g. v0.1.0-RZ03-0499
        self.build_target = None # its board target, e.g. RZ03-0499
        self.connected = False
        self.next_id = secrets.randbelow(0xfffffffe)+1
        self.stream_requests = queue.Queue()
        self.stream_mode = 'gui'  # 'gui' (HKG telemetry) or 'key' (HKL1 8 ksps)
        self.key_threshold = self.key_sensor = self.key_session = None
        self.samples = deque(maxlen=SAMPLE_CAPACITY)
        self.samples_lock = threading.Lock()

    def notify(self,text):
        try: self.events.put_nowait(text)
        except queue.Full: pass  # bounded informational log, never command/state data

    def submit(self,action,*args):
        self.requests.put_nowait((action,args))

    def stream_key(self,threshold,sensor):
        """Switch the device to the 8 ksps per-key stream for one sensor."""
        self.stream_requests.put(('key',threshold,sensor,secrets.randbelow(0xfffffffe)+1))

    def stream_gui(self):
        """Switch the device back to the latest-only GUI telemetry stream."""
        self.stream_requests.put(('gui',))

    def drain_samples(self):
        with self.samples_lock:
            if not self.samples: return []
            data = list(self.samples)
            self.samples.clear()
        return data

    def clear_samples(self):
        with self.samples_lock: self.samples.clear()

    def push_sample(self,raw):
        with self.samples_lock: self.samples.append(raw)

    def snapshot(self):
        with self.lock: return self.latest

    def stop(self): self.stop_event.set()

    def run(self):
        fd = None
        original = None
        pending = None
        gui_decoder = Decoder()
        self.key_decoder = None
        try:
            fd = os.open(self.path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
            fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
            original = termios.tcgetattr(fd)
            tty.setraw(fd,termios.TCSANOW)
            # The build identity is a text reply, so it is read before the
            # binary stream takes over the port: afterwards telemetry frames
            # own the CDC endpoint and text replies are unavailable. Any stream
            # left running by a previous owner is stopped first, otherwise the
            # reply is arbitrated away.
            tx = bytearray(b'\nstream off\nversion\n')
            text = b''
            deadline = time.monotonic()+1.5
            while self.build is None and time.monotonic() < deadline and not self.stop_event.is_set():
                readable,writable,_ = select.select([fd],[fd] if tx else [],[],.05)
                if writable:
                    try: sent = os.write(fd,tx)
                    except BlockingIOError: sent = 0
                    del tx[:sent]
                if readable:
                    try: data = os.read(fd,4096)
                    except BlockingIOError: continue
                    if not data: break
                    text = (text+data)[-256:]
                    found = parse_build(text)
                    if found:
                        self.build,self.build_target = found[0],found[2]
                        self.notify(f'Device build {found[0]} (version {found[1]}, target {found[2]})')
            tx = bytearray(b'\nstream gui\n')
            pending = ('get',(),self.next_id)
            tx.extend(f'cfg get {self.next_id}\n'.encode())
            deadline = time.monotonic()+3
            last_rx = time.monotonic()
            while not self.stop_event.is_set():
                # Stream-mode switches overtake configuration commands; a
                # pending acknowledged command is superseded when capture mode
                # engages because HKG ACK frames stop while it is active.
                while True:
                    try: request = self.stream_requests.get_nowait()
                    except queue.Empty: break
                    if request[0] == 'key':
                        _,threshold,sensor,session = request
                        if self.stream_mode == 'key' and (self.key_threshold,self.key_sensor) == (threshold,sensor):
                            continue  # already streaming this sensor
                        self.stream_mode = 'key'
                        self.key_threshold,self.key_sensor,self.key_session = threshold,sensor,session
                        self.key_decoder = KeyDecoder(threshold,session)
                        self.clear_samples()
                        tx.extend(f'stream key {threshold} {session} {sensor}\n'.encode())
                        if pending is not None:
                            self.notify('Configuration command superseded by keystroke capture mode')
                            pending = None
                    else:
                        self.stream_mode = 'gui'
                        self.key_threshold = self.key_sensor = self.key_session = None
                        self.key_decoder = None
                        gui_decoder = Decoder()  # resynchronize on the next HKG frame
                        tx.extend(b'stream gui\n')
                if self.stream_mode == 'gui' and pending is None and not tx:
                    try: action,args = self.requests.get_nowait()
                    except queue.Empty: pass
                    else:
                        self.next_id = self.next_id % 0xffffffff+1
                        pending = action,args,self.next_id
                        tx.extend(('cfg '+action+' '+str(self.next_id)+''.join(' '+str(v) for v in args)+'\n').encode())
                        deadline = time.monotonic()+3
                readable,writable,_ = select.select([fd],[fd] if tx else [],[],.02)
                if writable:
                    try: sent = os.write(fd,tx)
                    except BlockingIOError: sent = 0
                    del tx[:sent]
                if readable:
                    try: data = os.read(fd,65536)
                    except BlockingIOError: continue
                    if not data: raise OSError('CDC disconnected')
                    last_rx = time.monotonic()
                    if self.stream_mode == 'key':
                        for raw in self.key_decoder.feed(data):
                            if raw is not None: self.push_sample(raw)
                    else:
                        for snapshot in gui_decoder.feed(data):
                            now = time.monotonic(); last_rx = now
                            with self.lock: self.latest = now,snapshot
                            if pending and snapshot.ack == pending[2]:
                                action,args,_ = pending
                                if snapshot.result != 1:
                                    raise ValueError(f'Device rejected {action} {args}; remaining changes cancelled')
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
                                # `clean` is confirmed by its ACK alone: the device
                                # verified the erase by reading both pages back blank
                                # before answering result 1.
                                if action == 'velocity':
                                    level, = args
                                    if not 1 <= level <= 10 or snapshot.velocity_start != level:
                                        raise ValueError('Velocity start readback differs from requested value')
                                self.connected = True
                                self.notify(f'Confirmed {action} {args}' if action != 'get' else 'Connected: device telemetry acknowledged')
                                pending = None
                if pending and time.monotonic() >= deadline:
                    raise TimeoutError('Command not acknowledged; no retry. Remaining changes cancelled.')
                if time.monotonic()-last_rx > 2:
                    raise TimeoutError('Telemetry stale/disconnected. No further configuration sent.')
        except Exception as error:
            self.notify(f'ERROR: {error}')
        finally:
            self.connected = False
            if fd is not None:
                try:
                    if original is not None: termios.tcsetattr(fd,termios.TCSANOW,original)
                except OSError: pass
                os.close(fd)
            self.notify('Disconnected; keyboard operation does not depend on the GUI')
