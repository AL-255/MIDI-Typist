"""GUI telemetry, physical ANSI geometry and host profile validation."""
from dataclasses import dataclass
from firmware_defaults import DEFAULTS as D, initializer
import struct
import math
import re
from keyboard_boards import Key, ansi_geometry, get_board, boards, DEFAULT_TARGET
from keyboard_keycodes import keycode_name

HEADER_SIZE, RECORD_SIZE, MAX_KEYS, MAX_HID = 80, 17, 128, 32
MAX_SIZE = HEADER_SIZE + MAX_KEYS*RECORD_SIZE + MAX_HID + 4
MIDI_CONTROLS = {'Fn':'mode', 'RAl':'oct−', 'RCt':'oct+',
                 'LCt':'bend−', 'LAl':'bend+', 'LGu':'mod', 'Spc':'sustain'}
# Current count-aware wire format. Board identity comes from SysEx READY.
MAGIC = b'MTG4'
# Build identity answered by the `version` command: project version plus the
# board model the firmware targets, e.g. v0.1.0-RZ03-0499.
BUILD_RE = re.compile(r'build=(v(\d+\.\d+\.\d+)-([A-Za-z0-9_.-]+) '
                      r'git=([0-9a-f]{40}|[0-9a-f]{64}|unknown) state=(clean|dirty|unknown))\r?\n')
KNOWN_TARGETS = {target:board.name for target,board in boards().items()}
FLAG_JANKO = 64  # the built-in Jankó note layout is active
def note_name(note):
    return 'Off' if note == 255 else f'{("C","C#","D","D#","E","F","F#","G","G#","A","A#","B")[note%12]}{note//12-1}'

# HID label spelling is protocol metadata; pitches come only from defaults.h.
_HID_LABELS = {i+4:chr(65+i) for i in range(26)}
_HID_LABELS.update({i+0x1e:label for i,label in enumerate('1234567890')})
_HID_LABELS.update({0x29:'Esc',0x2a:'BkS',0x2b:'Tab',0x2d:'-',0x2e:'=',
    0x2f:'[',0x30:']',0x31:'\\',0x39:'Cap',0x33:';',0x34:"'",0x28:'Ent',
    0x36:',',0x37:'.',0x38:'/'})
JANKO_NOTES = {_HID_LABELS[usage]:note_name(note)
               for usage,note in initializer('DEFAULT_JANKO_NOTE_MAP')}
JANKO_NOTES.update(LSh=note_name(D['JANKO_LEFT_SHIFT']), RSh=note_name(D['JANKO_RIGHT_SHIFT']))


@dataclass(frozen=True)
class Snapshot:
    profile: int
    count: int
    flags: int
    result: int
    sequence: int
    revision: int
    ack: int
    scan_errors: int
    light_errors: int
    raw: tuple
    press: tuple
    release: tuple
    down: tuple
    report: bytes
    mode: int = 0
    velocity: tuple = ()
    captures: tuple = ()
    velocity_state: tuple = ()
    velocity_start: int = D['DEFAULT_MIDI_VELOCITY_START']
    performance_mode: int = D['DEFAULT_MIDI_MODE']
    octave: int = D['DEFAULT_MIDI_OCTAVE']
    midi_mapping: tuple = ()
    midi_cleanup: bool = False
    midi_errors: int = 0
    mode_changes: int = 0
    calibration_state: int = 0
    calibration_completed: int = 0
    calibration_selected: int = 255
    calibration_flags: int = 0
    calibration_hold: int = 0
    calibration_idle: int = 0
    calibration_done: tuple = ()
    calibration_reason: int = 0
    calibration_upper: int = 0
    calibration_lower: int = 0
    calibration_generation: int = 0
    calibration_error: int = 0
    storage_flags: int = 0  # valid snapshot, pending save, fault
    storage_slot: int = 255
    storage_generation: int = 0
    sample_hz: int = 0
    keyboard_mapping: tuple = ()
    transport: int = 0
    transport_flags: int = 0


@dataclass(frozen=True)
class PowerStatus:
    flags: int
    percent: int
    charger: int
    adc: int
    age_ms: int


def decode_power(payload):
    if len(payload)!=16 or payload[:5]!=b'MTP1\x01':
        raise ValueError('Unsupported power status reply')
    flags,percent,charger,adc,reserved,age=struct.unpack_from('<BBBHHI',payload,5)
    if (reserved or flags & ~31 or percent>100 or charger>5 or
        (not flags & 4 and (percent or adc!=65535)) or
        (flags & 2 and not flags & 1) or
        (flags & 8 and flags & 7!=5) or
        (flags & 16 and not flags & 8) or
        (charger and (not flags & 1 or ((charger==1)==bool(flags & 2))))):
        raise ValueError('Invalid power status fields')
    return PowerStatus(flags,percent,charger,adc,age)


def power_text(status):
    source='External power' if status.flags & 2 else 'Battery power' if status.flags & 1 else 'Power source unknown'
    level=f'{status.percent}% (estimate)' if status.flags & 4 else 'level unavailable'
    charge=('charger status unknown','on battery','charging','full',
            'charger pin low (polarity unverified)','charger pin high (polarity unverified)')[status.charger]
    warning=' — CRITICAL BATTERY' if status.flags & 16 else ' — LOW BATTERY' if status.flags & 8 else ''
    return f'{source} | Battery {level} | {charge}{warning}'


def transport_text(snapshot):
    names=('Not reported','USB','Bluetooth 1','Bluetooth 2','Bluetooth 3','2.4 GHz')
    if not snapshot.transport:return ''
    state=('pairing requested / searching' if snapshot.transport_flags & 4 else
           'switching' if snapshot.transport_flags & 2 else
           'ready' if snapshot.transport_flags & 1 else 'waiting for host')
    return f'{names[snapshot.transport]}: {state}'


def parse_build(text):
    """Parse current READY/version provenance into (display, version, target)."""
    if isinstance(text,bytes): text = text.decode('ascii','replace')
    match = BUILD_RE.search(text)
    if not match: return None
    if (match[4] == 'unknown') != (match[5] == 'unknown'): return None
    return match[1],match[2],match[3]


def frame_size(count, hid_bytes):
    return ((HEADER_SIZE + count*RECORD_SIZE + hid_bytes+3)&~3) + 4


def decode(data):
    if not HEADER_SIZE+4 <= len(data) <= MAX_SIZE or len(data)%4 or data[:4] != MAGIC:
        raise ValueError('bad GUI frame size/magic')
    size,profile,count,flags,result,mode,velocity_start = struct.unpack_from('<H6B',data,4)
    sample_hz = struct.unpack_from('<I',data,32)[0]
    hid_bytes,performance_mode,octave,channel,cleanup = struct.unpack_from('<BBbBB',data,36)
    if (size != len(data) or count > MAX_KEYS or not 2 <= hid_bytes <= MAX_HID or
            size != frame_size(count,hid_bytes) or mode > 2 or flags & ~127 or
            result > 2 or not 1 <= velocity_start <= 10 or
            struct.unpack_from('<H',data,76)[0] != HEADER_SIZE):
        raise ValueError('unsupported GUI header')
    if (not count and (profile or sample_hz or flags & 4)) or (count and (not profile or not sample_hz)):
        raise ValueError('invalid GUI layout/rate')
    if sum(struct.unpack_from(f'<{(size-4)//2}H',data)) & 0xffffffff != struct.unpack_from('<I',data,size-4)[0]:
        raise ValueError('GUI checksum mismatch')
    end = HEADER_SIZE+count*RECORD_SIZE
    transport,transport_flags=data[78:80]
    if (transport>5 or transport_flags & ~7 or (not transport and transport_flags) or
        (transport_flags & 4 and (transport<2 or transport_flags & 1))):
        raise ValueError('invalid transport state')
    if any(data[end+hid_bytes:size-4]):
        raise ValueError('invalid GUI padding')
    sequence,revision,ack,scan_errors,light_errors = struct.unpack_from('<5I',data,12)
    records = tuple(struct.iter_unpack('<HHHfIBBB',data[HEADER_SIZE:end]))
    raw,press,release,velocity,captures,bits,mapping,keycodes = tuple(zip(*records)) if count else ((),)*8
    if any(not 1 <= p < r < 4096 for p,r in zip(press,release)):
        raise ValueError('invalid threshold readback')
    if flags & 4 and any(not 1 <= v <= 4096 for v in raw):
        raise ValueError('valid flag contradicts raw values')
    if any(s & ~63 for s in bits) or any(not math.isfinite(v) or not 0.0 <= v <= 1.0 for v in velocity):
        raise ValueError('invalid velocity data')
    states = tuple(s & 15 for s in bits)
    down = tuple(bool(s & 16) for s in bits)
    done = tuple(bool(s & 32) for s in bits)
    if performance_mode > 1 or not -10 <= octave <= 10 or channel != 1 or cleanup > 1:
        raise ValueError('invalid MIDI state')
    if any(n > 127 and n != 255 for n in mapping): raise ValueError('invalid MIDI mapping')
    if any(k!=0 and not 4<=k<=0xe7 for k in keycodes): raise ValueError('invalid keyboard mapping')
    errors,changes = struct.unpack_from('<II',data,48)
    state,completed,selected,cflags,reason,storage_flags,storage_slot = struct.unpack_from('<7B',data,41)
    hold,idle,upper,lower = struct.unpack_from('<4H',data,56)
    generation,error,storage_generation = struct.unpack_from('<III',data,64)
    if storage_flags & ~7 or storage_slot not in (0,1,255):
        raise ValueError('invalid storage state')
    if (state > 8 or state == 4 or completed > count or selected != 255 and selected >= count or
        cflags & ~7 or bool(cflags & 1) != (1 <= state <= 5) or hold > D['CALIBRATION_HOLD_MS'] or idle > D['CALIBRATION_IDLE_MS'] or
        sum(done) != completed or reason > 4 or upper > 4096 or lower > 4096):
        raise ValueError('invalid calibration state')
    if any(v & 8 and (state != 3 or done[i]) for i,v in enumerate(states)):
        raise ValueError('invalid calibration hold bitmap')
    cal = dict(calibration_state=state,calibration_completed=completed,calibration_selected=selected,
               calibration_flags=cflags,calibration_hold=hold,calibration_idle=idle,
               calibration_done=done,calibration_reason=reason,
               calibration_upper=upper,calibration_lower=lower,calibration_generation=generation,calibration_error=error,
               storage_flags=storage_flags,storage_slot=storage_slot,storage_generation=storage_generation,
               sample_hz=sample_hz,keyboard_mapping=keycodes,
               transport=transport,transport_flags=transport_flags)
    return Snapshot(profile,count,flags,result,sequence,revision,ack,scan_errors,light_errors,
                    raw,press,release,down,bytes(data[end:end+hid_bytes]),mode,
                    velocity,captures,states,velocity_start,performance_mode,octave,mapping,bool(cleanup),errors,changes,**cal)

class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self,data):
        self.buffer.extend(data)
        while len(self.buffer) >= 6:
            if self.buffer[:4] != MAGIC: raise ValueError('bad GUI frame magic')
            size = struct.unpack_from('<H',self.buffer,4)[0]
            if not HEADER_SIZE+4 <= size <= MAX_SIZE or size%4: raise ValueError('bad GUI frame size')
            if len(self.buffer) < size: break
            result = decode(self.buffer[:size])
            del self.buffer[:size]
            yield result


def validate_pair(press,release):
    if type(press) is not int or type(release) is not int or not 1 <= press < release < 4096:
        raise ValueError('Require 1 <= press < release <= 4095 (press below; release above).')
    return press,release


def profile_from_snapshot(snapshot, target=DEFAULT_TARGET):
    board = get_board(target)
    if not board.accepts(snapshot):
        raise ValueError('Snapshot does not match the selected board layout.')
    labels = board.labels()
    return {'version':4,'target':target,'layout':board.profile,'keys':[
        {'sensor':i,'label':labels[i],'press':snapshot.press[i],'release':snapshot.release[i],
         'midi':snapshot.midi_mapping[i], 'keyboard':snapshot.keyboard_mapping[i]}
        for i in range(board.count)]}


def validate_profile(data, target=DEFAULT_TARGET):
    board = get_board(target)
    if not isinstance(data,dict) or type(data.get('version')) is not int or data.get('version') != 4:
        raise ValueError('Expected a board-specific profile, version 4.')
    if data.get('target') != target or type(data.get('layout')) is not int or data.get('layout') != board.profile:
        raise ValueError('Profile targets a different keyboard or layout.')
    keys = data.get('keys')
    if not isinstance(keys,list) or len(keys) != board.count: raise ValueError(f'Profile must contain all {board.count} keys.')
    labels = board.labels()
    result = {}
    for key in keys:
        if not isinstance(key,dict): raise ValueError('Invalid profile key.')
        index = key.get('sensor')
        if type(index) is not int or not 0 <= index < board.count or index in result or key.get('label') != labels[index]:
            raise ValueError('Profile sensor/label mismatch or duplicate.')
        result[index] = validate_pair(key.get('press'),key.get('release'))
        note = key.get('midi')
        if type(note) is not int or not (0 <= note <= 127 or note == 255): raise ValueError('Invalid MIDI note.')
        if labels[index] in MIDI_CONTROLS and note != 255: raise ValueError('Reserved MIDI control key.')
        keycode_name(key.get('keyboard'))
        if labels[index]=='Fn' and key['keyboard']!=0: raise ValueError('Fn cannot be remapped.')
    return result




CAPTURE_POINTS = D['CAPTURE_DEFAULT_POINTS']


class KeystrokeCapture:
    """Holds the first CAPTURE_POINTS samples of the latest keystroke.

    Armed until the watched key reports a down edge; the triggering sample
    becomes sample zero and every following sample appends one more. A new
    down edge always restarts the capture (latest keystroke wins), and the
    collected points are held — feed() returns False — until that happens.
    feed() consumes telemetry frames with the device-reported down state and
    velocity fit; feed_sample() consumes full-rate key-stream readbacks and
    applies the key's Schmitt pair itself. The trigger and subsequent samples
    reproduce the device's bottom-out/ten-point window (host-side fit stored in
    ``velocity``). The device fit counter attribution applies to telemetry
    frames: the fit whose completion counter first rises after the trigger
    belongs to this keystroke (normally already present in the triggering
    snapshot at the 33 ms telemetry throttle).
    """

    def __init__(self):
        self.reset()

    def reset(self):
        self.points = []
        self.prev_down = None  # None: adopt the next frame as baseline, never trigger
        self.fit = None      # (captures, velocity) of the keystroke's fit, if any
        self.velocity = None # host-computed raw counts/s (board-declared rate)
        self.captures0 = None
        self.done = False
        self.armed = True

    def feed(self, raw, down, captures=None, velocity=None, fit_valid=False):
        """Consume one frame; return True when the waveform should refresh."""
        edge = down and self.prev_down is False
        self.prev_down = down
        if edge:
            self.points = [raw]
            self.captures0 = captures
            self.fit = None
            self.velocity = None
            self.done = False
            self.armed = False
            return True
        if self.done or self.armed:
            return False  # held or still waiting; the waveform stays frozen
        self.points.append(raw)
        if (self.fit is None and captures is not None and self.captures0 is not None
                and captures > self.captures0):
            self.fit = (captures, velocity)
        if len(self.points) >= CAPTURE_POINTS:
            if self.fit is None and fit_valid and captures is not None:
                self.fit = (captures, velocity)
            self.done = True
        return True

    def feed_sample(self, raw, press, release):
        """Consume one full-rate key-stream sample.

        The down state uses the selected key's Schmitt pair; every sample is
        appended to an active capture for the bottom-out/ten-point device
        velocity window. Its timebase is the board-declared rate, not a measured cadence.
        """
        down = (raw < press) if not self.prev_down else (raw <= release)
        return self.feed(raw, down)


def parse_note(text):
    text = text.strip()
    if text.lower() in ('off','none','unmapped'): return 255
    if text.isdecimal():
        value = int(text)
    else:
        m = re.fullmatch(r'([A-Ga-g])([#b]?)(-?\d+)',text)
        if not m: raise ValueError('Use a MIDI number 0–127, note name (C0, Eb1, F#2), or Off.')
        letter,accidental,octave = m.groups()
        value = (int(octave)+1)*12 + {'C':0,'D':2,'E':4,'F':5,'G':7,'A':9,'B':11}[letter.upper()] + {'':0,'#':1,'b':-1}[accidental]
    if not 0 <= value <= 127: raise ValueError('MIDI note must be 0–127 (C-1 through G9).')
    return value
