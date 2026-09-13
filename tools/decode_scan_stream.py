#!/usr/bin/env python3
"""Decode CDC binary capture from stdin, a file, or a device.

Default output: sequence,tick,dropped,flags,raw0,... (fixed-width decimal).
Use --live for a latest-only serial display (20 lines/s by default), --hex
for hexadecimal readbacks, --bars for labelled in-place colored blocks,
or --summary for capture rates. --last-key requests compact device streaming
on a tty, identifies the triggering key, prints its next 20 readbacks plus
a bottom-out-window velocity estimate (up to ten samples, assumed 8 kHz), and exits. Add --repeat
to re-arm after release and capture again until Ctrl-C.
"""
import argparse
import os
import select
import struct
import sys
import threading
import time

SIZE = 160


def decode(record):
    if len(record) != SIZE or record[:4] != b'HKS1': return None
    size, count, profile, sequence, tick, dropped, flags, reserved = struct.unpack_from('<HBBIIIHH', record, 4)
    if size != SIZE or profile not in (1, 2, 3) or count != (65 if profile == 3 else 60 + profile): return None
    if reserved or flags & ~1 or any(record[24 + count*2:156]): return None
    checksum = sum(struct.unpack_from('<78H', record)) & 0xffffffff
    if checksum != struct.unpack_from('<I', record, 156)[0]: return None
    return sequence, tick, dropped, flags, struct.unpack_from('<' + 'H'*count, record, 24)


class Decoder:
    def __init__(self): self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        while len(self.buffer) >= 4:
            start = self.buffer.find(b'HKS1')
            if start < 0:
                del self.buffer[:-3]
                return
            if start: del self.buffer[:start]
            if len(self.buffer) < SIZE: return
            result = decode(self.buffer[:SIZE])
            if result is None:
                del self.buffer[0]
                continue
            del self.buffer[:SIZE]
            yield result


def format_record(result, hexadecimal=False):
    seq, tick, dropped, flags, samples = result
    values = [f'{v:04x}' if hexadecimal else f'{v:5d}' for v in samples]
    return ','.join([f'{seq:10d}', f'{tick:10d}', f'{dropped:10d}', f'{flags:5d}', *values])


class LatestReader:
    """Drain input independently of stdout; retain only the newest valid record."""
    def __init__(self, fd):
        self.fd = fd
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.latest = None
        self.received = 0
        self.done = False
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        decoder = Decoder()
        try:
            while not self.stop.is_set():
                if not select.select([self.fd], [], [], .05)[0]:
                    continue
                data = os.read(self.fd, 65536)
                if not data:
                    break
                newest = None
                count = 0
                for record in decoder.feed(data):
                    newest = record
                    count += 1
                if newest is not None:
                    with self.lock:
                        self.received += count
                        self.latest = (self.received, time.monotonic(), newest)
        except OSError as error:
            self.error = error
        finally:
            self.done = True

    def snapshot(self):
        with self.lock:
            return self.latest


def live_display(stream, emit, rate=20., duration=None):
    reader = LatestReader(stream.fileno())
    reader.thread.start()
    interval = 1. / rate
    started = time.monotonic()
    deadline = started + interval
    printed = last_seen = 0
    try:
        while duration is None or time.monotonic() - started < duration:
            # A slow terminal must not stall the reader or enqueue display rows.
            time.sleep(max(0., deadline - time.monotonic()))
            snapshot = reader.snapshot()
            if snapshot is not None:
                generation, received_at, record = snapshot
                if generation != last_seen and time.monotonic() - received_at <= max(.25, interval * 2):
                    emit(record)
                    printed += 1
                    last_seen = generation
            if reader.done:
                if reader.error:
                    raise reader.error
                break
            # Never catch up by printing a burst of historical rows.
            deadline = time.monotonic() + interval
    finally:
        reader.stop.set()
        reader.thread.join()
    return reader.received, printed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', nargs='?', help='device or capture file; default /dev/ttyACM0 for --last-key, stdin otherwise; - explicitly reads stdin')
    parser.add_argument('--hex', action='store_true')
    parser.add_argument('--summary', action='store_true')
    parser.add_argument('--live', action='store_true', help='drain continuously; display newest received report only')
    parser.add_argument('--bars', action='store_true', help='two-row labelled ANSI block display (implies --live)')
    parser.add_argument('--start', type=int, default=0, help='first raw sensor index in bar viewport (default 0)')
    parser.add_argument('--rate', type=float, default=20., help='maximum live display lines/s (default 20)')
    parser.add_argument('--duration', type=float, help='stop live display after this many seconds')
    parser.add_argument('--last-key', action='store_true', help='identify triggering key, print next 20 readbacks and bottom-out-window velocity (up to ten samples, assumed 8 kHz); fail on loss')
    parser.add_argument('--repeat', action='store_true', help='with --last-key, re-arm when the captured key rises above threshold; repeat until Ctrl-C')
    parser.add_argument('--layout', choices=('ansi','iso','jis'), default='ansi', help='last-key label mapping (default ansi, matching this keyboard)')
    parser.add_argument('--threshold', type=int, default=3800, help='last-key press when raw drops strictly below this value (default 3800)')
    parser.add_argument('--buffer-frames', type=int, default=8192, help='last-key pending output limit; overflow is fatal (default 8192)')
    parser.add_argument('--timeout', type=float, default=5., help='last-key complete-report timeout in seconds (default 5)')
    args = parser.parse_args()
    if args.capture is None:
        args.capture = '/dev/ttyACM0' if args.last_key else '-'
    args.live = args.live or args.bars
    if args.last_key:
        if args.live or args.hex or args.summary or args.start or args.duration is not None or args.rate != 20.:
            parser.error('--last-key cannot be combined with display, rate or duration options')
        if not 1 <= args.threshold <= 4096 or not 1 <= args.buffer_frames <= 1048576:
            parser.error('threshold must be 1..4096; buffer-frames must be 1..1048576')
        if not 0 < args.timeout < float('inf'):
            parser.error('--timeout must be finite and positive')
        if args.capture == '-' and os.isatty(sys.stdin.fileno()):
            parser.error('--last-key cannot use an interactive terminal as stdin; specify the CDC device or pipe an HKL1 capture')
        from last_key_stream import receive
        from scan_bars import sensor_labels
        labels = sensor_labels()[{'ansi':61, 'iso':62, 'jis':65}[args.layout]]
        import termios
        import tty
        # Open the tty bidirectionally for the mode command, but allow captured
        # HKL1 files/stdin to be replayed read-only without sending commands.
        stream = sys.stdin.buffer if args.capture == '-' else open(args.capture, 'rb', buffering=0)
        owned = args.capture != '-'
        if os.isatty(stream.fileno()):
            path = os.ttyname(stream.fileno())
            if owned: stream.close()
            stream = open(path, 'r+b', buffering=0)
            owned = True
        fd = stream.fileno()
        original = termios.tcgetattr(fd) if os.isatty(fd) else None
        session = None
        try:
            print(f'Capture Armed: input={args.capture}; trigger=raw<{args.threshold}; '
                  f'layout={args.layout}; next=20 (excluding trigger); '
                  f'report_timeout={args.timeout:g}s; velocity_fit=up-to-10@8000Hz (assumed), floor=1500; '
                  f'repeat={"on" if args.repeat else "off"}; awaiting stream', flush=True)
            if original is not None:
                import secrets
                session = secrets.randbits(32)
                tty.setraw(fd, termios.TCSANOW)  # never flush/discard tty input
                command = f'\nstream key {args.threshold} {session}\n'.encode('ascii')
                while command:
                    command = command[os.write(fd, command):]
            receive(fd, sys.stdout.fileno(), args.threshold, args.buffer_frames, args.timeout, session,
                    sample_count=20, labels=labels, repeat=args.repeat)
        finally:
            try:
                if original is not None: termios.tcsetattr(fd, termios.TCSANOW, original)
            finally:
                if owned: stream.close()
        return
    if args.threshold != 3800 or args.buffer_frames != 8192 or args.timeout != 5. or args.layout != 'ansi' or args.repeat:
        parser.error('--threshold, --buffer-frames, --timeout, --layout and --repeat require --last-key')
    if args.bars and args.hex:
        parser.error('--bars and --hex are mutually exclusive')
    if not 0 <= args.start <= 64:
        parser.error('--start must be a raw sensor index in 0..64')
    if args.start and not args.bars:
        parser.error('--start requires --bars')
    if not 0 < args.rate <= 1000 or (args.duration is not None and not 0 < args.duration < float('inf')):
        parser.error('rate must be in (0,1000]; duration must be finite and positive')
    if args.live and args.summary:
        parser.error('--live and --summary are mutually exclusive')
    stream = sys.stdin.buffer if args.capture == '-' else open(args.capture, 'rb')
    if args.live:
        import termios
        import tty
        fd = stream.fileno()
        original = termios.tcgetattr(fd) if os.isatty(fd) else None
        renderer = None
        try:
            if args.bars:
                from scan_bars import BarDisplay
                renderer = BarDisplay(sys.stdout, args.start)
            if original is not None:
                tty.setraw(fd, termios.TCSANOW)
                # Discard old host tty bytes; the decoder resynchronizes safely.
                termios.tcflush(fd, termios.TCIFLUSH)
            emit = renderer if renderer is not None else lambda record: print(format_record(record, args.hex), flush=True)
            received, printed = live_display(stream, emit, args.rate, args.duration)
            if renderer is not None:
                renderer.close()
            print(f'received={received} printed={printed} not_displayed={received - printed}', file=sys.stderr)
        finally:
            try:
                if renderer is not None:
                    renderer.close()
            finally:
                if original is not None:
                    termios.tcsetattr(fd, termios.TCSANOW, original)
        return
    decoder = Decoder()
    count = gaps = 0
    first = last = None
    while True:
        data = stream.read1(65536)
        if not data: break
        for result in decoder.feed(data):
            seq, tick, dropped, flags, samples = result
            if first is None: first = result
            if last is not None: gaps += ((seq - last[0]) & 0xffffffff) - 1
            last = result
            count += 1
            if not args.summary:
                print(format_record(result, args.hex))
    if args.summary and first is not None:
        ticks = (last[1] - first[1]) & 0xffffffff
        hz = (count - 1) * 8000 / ticks if ticks else 0
        print(f'frames={count} received_hz={hz:.2f} sequence_gaps={gaps} '
              f'dropped_counter={last[2]} elapsed_ticks={ticks}')


if __name__ == '__main__':
    from last_key_stream import StreamError
    try:
        main()
    except (StreamError, OSError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        if '--last-key' in sys.argv:
            sys.exit(130)
