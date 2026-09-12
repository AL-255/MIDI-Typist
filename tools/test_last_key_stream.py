#!/usr/bin/env python3
"""Strict compact decoder, loss detection, blocked stdout and tty command tests."""
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import unittest
from unittest.mock import patch

from last_key_stream import KeyDecoder, KeyCapture, StreamError, press_velocity
from scan_bars import sensor_labels

SCRIPT = str(Path(__file__).with_name('decode_scan_stream.py'))


def velocity_line(value):
    return f'Velocity: {value:+.3f} raw counts/s (up to 10 readbacks incl. trigger, cut before bottom-out 1500; median interval filter only above five samples; assumed 8000 Hz; positive=press)\n'.encode()


def packet(seq, raw=3799, key=3, flags=None, threshold=3800, session=123):
    data = struct.pack('<4sIIHBBH', b'HKL1', session, seq, raw, key,
                       int(seq == 0) if flags is None else flags, threshold)
    return data + struct.pack('<H', sum(struct.unpack('<9H', data)) & 65535)


class LastKeyTests(unittest.TestCase):
    def cli(self, data, *args):
        result = subprocess.run([sys.executable, '-B', SCRIPT, '-', '--last-key', *args],
                                input=data, capture_output=True, timeout=5)
        if result.stdout.startswith(b'Capture Armed:'):
            result.stdout = result.stdout.split(b'\n',1)[1]
        return result

    def test_exactly_twenty_after_trigger_then_exit(self):
        data = packet(0, 0, 255) + b''.join(packet(i, 3000 + i % 1000,key=32) for i in range(1, 16001))
        result = self.cli(data)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b'Key: A (sensor 32)\n' + b''.join(f'{3000+i}\n'.encode() for i in range(2,22)) + velocity_line(-8000))
        self.assertEqual(result.stderr, b'')

    def test_velocity_fit_sign_units_noise_and_window(self):
        self.assertEqual(press_velocity([3500,3490,3480,3470,3460]),80000)      # five samples: no filter
        self.assertEqual(press_velocity([3460,3470,3480,3490,3500]),-80000)
        self.assertEqual(press_velocity([3500]*5),0)
        self.assertAlmostEqual(press_velocity([3500,3403,3298,3204,3099]),401/4*8000)  # d(x)/count, fractional
        self.assertAlmostEqual(press_velocity([3500,3490,3480,3470,3460,3450,3440,3430,3420,3410]),80000)  # filtered
        self.assertEqual(press_velocity([3500,3400]),100*8000)                  # two-sample bottom-out window
        with self.assertRaises(ValueError): press_velocity([1])
        with self.assertRaises(ValueError): press_velocity([])
        with self.assertRaises(ValueError): press_velocity([1000]*11)
        # velocity_window: cut before the bottom-out sample, closed state only
        from last_key_stream import velocity_window, BOTTOM_OUT
        self.assertIsNone(velocity_window([3500,3400,3300]))
        self.assertEqual(velocity_window([3500,3400,3300,3200,1400]),[3500,3400,3300,3200])
        self.assertEqual(velocity_window([3500]+[3400]*9),[3500]+[3400]*9)
        self.assertEqual(velocity_window([3500]+[3400]*20),[3500]+[3400]*9)
        # A bottom-out on the first follow-up keeps that sample: the trigger
        # sits at the floor, and one interval still measures the press.
        self.assertEqual(velocity_window([3500,1400]),[3500,1400])
        self.assertIsNone(velocity_window([3500]))

    def test_velocity_pop_filter(self):
        # Ten-sample windows discard one glitch interval at every position.
        for index in range(9):
            for spike in (-500,500):
                intervals=[10]*9; intervals[index]=spike
                samples=[3500]
                for delta in intervals: samples.append(samples[-1]-delta)
                self.assertEqual(press_velocity(samples),80000)
        # Five-sample windows (four intervals) skip the filter: d(x)/count.
        for spike in (-500,500):
            intervals=[10]*4; intervals[2]=spike
            samples=[3500]
            for delta in intervals: samples.append(samples[-1]-delta)
            self.assertAlmostEqual(press_velocity(samples),(30+spike)/4*8000)
        self.assertEqual(press_velocity([3500,3500,3490,3480,3470,3460,3450,3440,3430,3410]),90000)   # earliest tie wins
        self.assertEqual(press_velocity([3500,3480,3470,3460,3450,3440,3430,3420,3410,3410]),70000)
        raw = [3500,3490,3480,3470,3460] + [1000]*15
        data = packet(0,3599,key=32) + b''.join(packet(i+1,v,key=32) for i,v in enumerate(raw))
        result = self.cli(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,b'Key: A (sensor 32)\n'+b''.join(f'{v}\n'.encode() for v in raw)+velocity_line(80000))

    def test_fragmented_prefix_and_old_session(self):
        data = b'old HKS1 traffic' + packet(50, session=5) + packet(0) + packet(1, 4000)
        decoder = KeyDecoder(3800, session=123)
        values = []
        for i in range(0, len(data), 3): values.extend(decoder.feed(data[i:i+3]))
        decoder.finish()
        self.assertEqual(values, [3799, 4000])

    def test_failures_are_nonzero_stderr_only(self):
        damaged = bytearray(packet(1)); damaged[-1] ^= 1
        cases = [packet(0)+packet(2), packet(0)+packet(0), packet(0)+bytes(damaged),
                 packet(0)+packet(1,flags=2), packet(0)+packet(1,flags=4),
                 packet(0)+packet(1,threshold=3700), packet(0)+packet(1,session=4),
                 packet(0)+packet(1)[:12], packet(1), b'',
                 packet(0)+b'junk'+packet(1), packet(0,0,3)]
        for data in cases:
            with self.subTest(data=data):
                result = self.cli(data)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'ERROR:', result.stderr)
                rows = result.stdout.splitlines()
                self.assertTrue(all(row.isdigit() or (i == 0 and row.startswith(b'Key: ')) for i,row in enumerate(rows)))

    def test_wrap_and_threshold(self):
        decoder = KeyDecoder(3800)
        list(decoder.feed(packet(0)))
        decoder.sequence = 0xffffffff
        self.assertEqual(list(decoder.feed(packet(0,flags=0))), [3799])
        result = self.cli(b''.join(packet(i,3500,key=32,threshold=3600) for i in range(21)), '--threshold', '3600')
        self.assertEqual(result.returncode, 0, result.stderr)
        for args in (('--threshold','0'), ('--threshold','4097'), ('--bars',),
                     ('--hex',), ('--live',), ('--summary',), ('--rate','50'),
                     ('--buffer-frames','0'), ('--timeout','nan')):
            self.assertNotEqual(self.cli(b'',*args).returncode, 0)

    def test_key_switch_warns_and_starts_fresh_capture(self):
        data = packet(0,3500,key=32)+packet(1,3499,key=32)+packet(2,3400,key=33)
        data += b''.join(packet(i,3000,key=33) for i in range(3,23))
        result = self.cli(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stderr,b'')
        self.assertIn(b'WARNING: selected sensor changed 32->33; discarding incomplete capture (1/20 samples)',result.stdout)
        self.assertIn(b'Capture Armed: restarting after key change',result.stdout)
        self.assertEqual(result.stdout.split(b'Key: Cap (sensor 33)\n')[1],b'3000\n'*20+velocity_line(0))
        self.assertEqual(result.stdout.count(b'Velocity:'),1)

    def test_repeated_key_changes_clear_velocity_and_wait_above_threshold(self):
        capture = KeyCapture(3600,20,sensor_labels()[61],repeat=True)
        capture.feed(32,3500)
        for value in (3400,3300,3200): capture.feed(32,value)
        lines = capture.feed(33,3900)
        self.assertTrue(lines[0].startswith('WARNING:'))
        self.assertFalse(any(line.startswith('Key:') for line in lines))
        self.assertEqual(capture.feed(33,3600),[])
        self.assertEqual(capture.feed(33,3500),['Key: Cap (sensor 33)\n'])
        capture.feed(33,3000)
        lines = capture.feed(32,3500)
        self.assertTrue(lines[-1].startswith('Key: A'))
        self.assertEqual(capture.captured,0)
        self.assertEqual(capture.velocity_samples,[3500])  # triggering sample is window x0
        for i in range(20): lines = capture.feed(32,3400)
        self.assertEqual(lines[-1].encode(),velocity_line(0))

    def test_repeat_release_boundary_and_fresh_window(self):
        capture = KeyCapture(3600,20,sensor_labels()[61],repeat=True)
        self.assertEqual(capture.feed(255,None),[])
        self.assertEqual(capture.feed(32,3599),['Key: A (sensor 32)\n'])
        lines = []
        for i in range(20): lines.extend(capture.feed(32,3500-i*10))
        self.assertEqual(lines[-1].encode(),velocity_line(80000))
        self.assertFalse(capture.done)
        for value in [3400]*100+[3600]*3:
            self.assertEqual(capture.feed(32,value),[])
        self.assertIn('Capture Armed: released raw=3601>3600',capture.feed(32,3601)[0])
        for value in (3800,3600): self.assertEqual(capture.feed(32,value),[])
        self.assertEqual(capture.feed(33,3599),['Key: Cap (sensor 33)\n'])
        lines = []
        for i in range(20): lines.extend(capture.feed(33,3500))
        self.assertEqual(lines[-1].encode(),velocity_line(0))
        lines = capture.feed(32,3900)  # cannot infer Caps release from A's value
        self.assertIn('previous capture complete; release not observed',lines[0])
        self.assertEqual(capture.state,'armed')
        self.assertEqual(capture.feed(32,3599),['Key: A (sensor 32)\n'])

    def test_repeat_release_on_last_capture_sample(self):
        capture = KeyCapture(3600,20,sensor_labels()[61],repeat=True)
        capture.feed(32,3599)
        for i in range(19):
            lines = capture.feed(32,3800)
            self.assertFalse(any('Armed' in line for line in lines))
        lines = capture.feed(32,3800)
        self.assertEqual(lines[0],'3800\n')
        self.assertTrue(lines[1].startswith('Velocity:'))
        self.assertTrue(lines[2].startswith('Capture Armed:'))
        self.assertEqual(capture.feed(32,3500),['Key: A (sensor 32)\n'])

    def test_repeat_two_captures_remains_running_until_ctrl_c(self):
        child = subprocess.Popen([sys.executable,'-B',SCRIPT,'-','--last-key','--threshold','3600','--repeat'],
                                 stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            first = [3500-i*10 for i in range(20)]
            second = [3200]*20
            samples = [3599]+first+[3400,3600,3601,3900,3600,3599]+second+[3601]
            child.stdin.write(b''.join(packet(i,v,key=32,threshold=3600) for i,v in enumerate(samples)))
            child.stdin.flush()
            output = bytearray()
            deadline = time.monotonic()+3
            while output.count(b'Capture Armed:')<3 and time.monotonic()<deadline:
                if select.select([child.stdout],[],[],.05)[0]:
                    data = os.read(child.stdout.fileno(),65536)
                    if not data: break
                    output.extend(data)
            self.assertEqual(output.count(b'Capture Armed:'),3)
            self.assertIsNone(child.poll())
            child.send_signal(signal.SIGINT)
            tail,error = child.communicate(timeout=3)
            output.extend(tail)
            self.assertEqual(child.returncode,130,error)
            self.assertEqual(error,b'')
            self.assertEqual(output.count(b'Key: A (sensor 32)'),2)
            self.assertEqual([int(line) for line in output.splitlines() if line.isdigit()],first+second)
            self.assertIn(velocity_line(80000),output)
            self.assertIn(velocity_line(0),output)
        finally:
            if child.poll() is None: child.kill(); child.communicate()

    def test_repeat_still_detects_loss_while_waiting_for_release(self):
        data = b''.join(packet(i,3500,key=32) for i in range(21))
        result = self.cli(data+packet(22,3500,key=32),'--repeat')
        self.assertEqual(result.returncode,1)
        self.assertIn(b'sequence gap',result.stderr)
        result = self.cli(data,'--repeat')
        self.assertEqual(result.returncode,1)
        self.assertIn(b'repeat capture input ended',result.stderr)

    def test_exits_without_waiting_for_input_eof(self):
        child = subprocess.Popen([sys.executable,'-B',SCRIPT,'-','--last-key','--threshold','3600'],
                                 stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            child.stdin.write(packet(0,3599,key=32,threshold=3600) +
                              b''.join(packet(i,3800+i,key=32,threshold=3600) for i in range(1,21)))
            child.stdin.flush()
            child.wait(timeout=3)  # input remains open throughout
            output,error = child.communicate(timeout=3)
            self.assertEqual(child.returncode,0,error)
            self.assertTrue(output.startswith(b'Capture Armed: input=-; trigger=raw<3600;'))
            self.assertEqual(output.split(b'\n',1)[1],b'Key: A (sensor 32)\n'+b''.join(f'{3800+i}\n'.encode() for i in range(1,21))+velocity_line(-8000))
        finally:
            if child.poll() is None: child.kill(); child.communicate()

    def test_blocked_stdout_overflow_exits_without_reader(self):
        read_fd, write_fd = os.pipe()
        child = None
        try:
            child = subprocess.Popen([sys.executable,'-B',SCRIPT,'-','--last-key','--buffer-frames','8'],
                                     stdin=subprocess.PIPE, stdout=write_fd, stderr=subprocess.PIPE)
            self.assertTrue(select.select([read_fd],[],[],3)[0])
            self.assertTrue(os.read(read_fd,4096).startswith(b'Capture Armed:'))
            # Fill stdout after the startup banner, so acquisition has begun.
            os.set_blocking(write_fd, False)
            try:
                while True: os.write(write_fd, b'x'*4096)
            except BlockingIOError: pass
            _, error = child.communicate(b''.join(packet(i) for i in range(30)), timeout=3)
            self.assertEqual(child.returncode, 1)
            self.assertIn(b'host output buffer overflow',error)
        finally:
            if child is not None and child.poll() is None: child.kill(); child.communicate()
            os.close(read_fd); os.close(write_fd)

    def test_omitted_device_opens_acm_not_terminal(self):
        from decode_scan_stream import main
        with patch.object(sys,'argv',[SCRIPT,'--last-key','--threshold','3600']), \
             patch('builtins.open',side_effect=StreamError('selected device')) as opened:
            with self.assertRaisesRegex(StreamError,'selected device'):
                main()
            opened.assert_called_once_with('/dev/ttyACM0','rb',buffering=0)

    def test_explicit_terminal_stdin_rejected_without_command(self):
        master,slave = pty.openpty()
        try:
            result = subprocess.run([sys.executable,'-B',SCRIPT,'-','--last-key'],
                                    stdin=slave,capture_output=True,timeout=3)
            self.assertEqual(result.returncode,2)
            self.assertEqual(result.stdout,b'')
            self.assertIn(b'interactive terminal',result.stderr)
            self.assertFalse(select.select([master],[],[],.05)[0])
        finally:
            os.close(master); os.close(slave)

    def test_tty_command_nonce_and_timeout(self):
        master, slave = pty.openpty()
        original = termios.tcgetattr(slave)
        child = None
        try:
            child = subprocess.Popen([sys.executable,'-B',SCRIPT,os.ttyname(slave),
                                      '--last-key','--threshold','3700','--timeout','.2'],
                                     stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            command = b''
            deadline = time.monotonic()+3
            while command.count(b'\n') < 2 and time.monotonic()<deadline:
                if select.select([master],[],[],.1)[0]: command += os.read(master,1024)
            parts = command.strip().split()
            self.assertEqual(parts[:3], [b'stream',b'key',b'3700'])
            session = int(parts[3])
            # The banner must reach a pipe before any trigger/input exists,
            # even without python -u (print explicitly flushes).
            self.assertTrue(select.select([child.stdout],[],[],.1)[0])
            banner = os.read(child.stdout.fileno(),4096)
            self.assertTrue(banner.startswith(b'Capture Armed:'))
            for detail in (b'trigger=raw<3700', b'layout=ansi', b'next=20', b'report_timeout=0.2s'):
                self.assertIn(detail,banner)
            os.write(master, packet(20,session=session^1) + packet(0,3500,threshold=3700,session=session))
            output,error = child.communicate(timeout=3)
            self.assertEqual(output,b'Key: 5 (sensor 3)\n')
            self.assertEqual(child.returncode,1)
            self.assertIn(b'timeout',error)
            self.assertEqual(termios.tcgetattr(slave), original)
        finally:
            if child is not None and child.poll() is None:
                child.kill(); child.communicate()
            os.close(master); os.close(slave)


if __name__ == '__main__': unittest.main()
