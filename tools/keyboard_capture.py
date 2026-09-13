"""Strict HKL1 receiver: bounded output, no resampling or silent recovery."""
from firmware_defaults import DEFAULTS as D
import struct

SIZE = 20
ASSUMED_SCAN_HZ = D['HUNTSMAN_ASSUMED_SCAN_HZ']
BOTTOM_OUT = D['RAW_BOTTOM_OUT']      # velocity window closes below this raw value (excluded)
VELOCITY_WINDOW = D['RAW_VELOCITY_WINDOW']   # maximum readbacks per fit, triggering sample included


def press_velocity(samples):
    """Velocity of a closed window, matching the MCU fit.

    ``samples`` is the window including the triggering readback: up to ten
    consecutive values, cut before the first sample below BOTTOM_OUT. The
    speed is the total drop divided by the interval count at the assumed
    8 kHz. Windows longer than five samples additionally discard the single
    interval furthest from the median (earliest wins ties); host output
    retains fractional counts/s, before the MCU's 0..1 clamp.
    """
    if len(samples) < 2 or len(samples) > VELOCITY_WINDOW:
        raise ValueError(f'velocity requires 2..{VELOCITY_WINDOW} window readbacks')
    delta = [a-b for a,b in zip(samples,samples[1:])]
    if len(delta) >= D['VELOCITY_FILTER_MIN_INTERVALS']:
        ordered = sorted(delta)
        twice_median = ordered[(len(delta)-1)//2] + ordered[len(delta)//2]
        outlier = max(range(len(delta)),key=lambda i:abs(2*delta[i]-twice_median))
        del delta[outlier]
    return sum(delta) * ASSUMED_SCAN_HZ / len(delta)


def velocity_window(points):
    """First ten points of a capture, cut at the bottom-out sample.

    Returns the closed window once it is complete (ten points, or a
    bottom-out sample already present in ``points``); None while the window
    is still collecting. The triggering point is window sample zero. The
    closing sample is excluded unless it is the only follow-up readback,
    matching the MCU: a trigger at the bottom-out floor still yields a fit.
    """
    window = list(points[:VELOCITY_WINDOW])
    closed = len(window) >= VELOCITY_WINDOW
    for i, value in enumerate(window):
        if i and value < BOTTOM_OUT:
            window = window[:i if i > 1 else 2]  # one interval is still a measurement
            closed = True
            break
    return window if closed else None  # fewer than ten points and no bottom-out: open


class StreamError(Exception):
    pass


class KeyDecoder:
    def __init__(self, threshold, session):
        self.threshold = threshold
        self.session = session
        self.buffer = bytearray()
        self.sequence = None
        self.key = 255

    def feed(self, data):
        self.buffer.extend(data)
        while True:
            if len(self.buffer) < SIZE: return
            frame = bytes(self.buffer[:SIZE])
            if frame[:4] != b'HKL1':
                raise StreamError('lost HKL1 framing; refusing to skip data')
            session, seq, raw, key, flags, threshold, checksum = struct.unpack_from('<IIHBBHH', frame, 4)
            if session != self.session:
                raise StreamError('unexpected HKL1 session')
            if checksum != sum(struct.unpack('<9H', frame[:18])) & 65535:
                raise StreamError('HKL1 checksum failure')
            if flags & ~7 or threshold != self.threshold:
                raise StreamError('unexpected HKL1 flags or threshold')
            if flags & 2:
                raise StreamError('device stream buffer overflow or USB data loss')
            if flags & 4:
                raise StreamError('invalid hardware readback or layout change')
            if self.sequence is None:
                if seq != 0 or not flags & 1:
                    raise StreamError('missing session-start report; data already lost')
            elif flags & 1 or seq != (self.sequence + 1) & 0xffffffff:
                raise StreamError('HKL1 sequence gap, duplicate, or unexpected session restart')
            if key > 64 or not 1 <= raw <= 4096:
                raise StreamError('invalid key index or readback')
            self.sequence = seq
            self.session = session
            self.key = key
            del self.buffer[:SIZE]
            yield raw

    def finish(self):
        if self.sequence is None:
            raise StreamError('no HKL1 session received')
        if self.buffer:
            raise StreamError('truncated HKL1 report at end of input')
