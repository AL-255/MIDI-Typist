"""Private MIDI-worker lifecycle and loss tests; never opens a keyboard."""
import multiprocessing
import importlib.util
import os
import queue
import time
import unittest
from types import SimpleNamespace
from unittest.mock import Mock,patch
from firmware_defaults import DEFAULTS as D
from midi_backend import MidiBackend,_NativeMidi
from midi_sysex import MAX_WIRE


class NativeFixture:
    """Spawn-importable native stand-in, including hangs inside library calls."""
    def __init__(self,name):
        if name=='open-error':raise OSError('fixture open failed')
        self.name=name;self.pending=queue.Queue();self.flood=False;self.sent=0

    def send(self,wire):
        self.sent+=1
        if self.name=='send-hang':time.sleep(60)
        if self.name=='crash':os._exit(7)
        if self.name=='send-error':raise OSError('fixture send failed')
        if self.name=='overflow':self.flood=True
        else:self.pending.put(bytes([self.sent])+wire)

    def receive(self,timeout):
        if self.flood:return b'\xf0x\xf7'
        try:return self.pending.get(timeout=timeout)
        except queue.Empty:return None

    def close(self):
        if self.name=='close-hang':time.sleep(60)


class MidiBackendTests(unittest.TestCase):
    def open(self,name='echo'):
        backend=MidiBackend(name,native_factory=NativeFixture)
        self.addCleanup(backend.close)
        return backend

    def test_order_timeout_and_idempotent_close(self):
        backend=self.open()
        self.assertIsNone(backend.receive(0))
        for i in range(1,11):
            message=bytes([0xf0,i,0xf7]);backend.send(message)
            self.assertEqual(backend.receive(1),bytes([i])+message)
        with self.assertRaises(ValueError):backend.send(bytes(MAX_WIRE+1))
        backend.close();backend.close()
        with self.assertRaises(OSError):backend.receive(0)
        with self.assertRaises(OSError):backend.send(b'\xf0\xf7')

    def test_close_hang_is_reaped_and_reconnects(self):
        for _ in range(3):
            backend=self.open('close-hang');pid=backend.process.pid
            backend.send(b'\xf0x\xf7');self.assertIsNotNone(backend.receive(1))
            start=time.monotonic();backend.close()
            self.assertLess(time.monotonic()-start,3)
            self.assertNotIn(pid,[p.pid for p in multiprocessing.active_children()])
        self.open().send(b'\xf0x\xf7')

    def test_native_open_send_and_exit_failures(self):
        with self.assertRaisesRegex(OSError,'open failed'):self.open('open-error')
        for name,pattern in (('send-error','send failed'),('crash','exited')):
            with self.subTest(name=name):
                backend=self.open(name)
                with self.assertRaisesRegex(OSError,pattern):backend.send(b'\xf0x\xf7')
                with self.assertRaises(OSError):backend.send(b'\xf0y\xf7')

    def test_hung_send_times_out_without_retry(self):
        backend=self.open('send-hang')
        with self.assertRaisesRegex(TimeoutError,'no retry'):backend.send(b'\xf0x\xf7')
        self.assertEqual(backend.sequence,1)
        with self.assertRaises(TimeoutError):backend.send(b'\xf0y\xf7')
        self.assertEqual(backend.sequence,1)

    def test_overflow_is_out_of_band_and_precedes_queued_data(self):
        backend=self.open('overflow')
        try:backend.send(b'\xf0x\xf7')
        except BufferError:pass # flood can fill the queue before send returns
        self.assertTrue(backend.failed.wait(3))
        with self.assertRaisesRegex(BufferError,'overflow'):backend.receive(0)
        with self.assertRaises(BufferError):backend.send(b'\xf0y\xf7')

    def test_native_callback_overflow_and_error_are_retained(self):
        incoming,outgoing=Mock(),Mock()
        for client in (incoming,outgoing):client.get_ports.return_value=['fixture']
        with patch('midi_backend.clients',return_value=(incoming,outgoing)):
            native=_NativeMidi('fixture')
            try:
                for _ in range(D['MIDI_CONTROL_HOST_MESSAGES']+1):native._receive(([0xf0,0xf7],0))
                with self.assertRaisesRegex(BufferError,'overflow'):native.receive(0)
                with self.assertRaises(BufferError):native.send(b'\xf0\xf7')
                outgoing.send_message.assert_not_called()
            finally:native.close()

    @unittest.skipUnless(importlib.util.find_spec('tkinter'),'optional Tk widget integration')
    def test_failed_port_release_blocks_flashing(self):
        from keyboard_flash_tab import FlashTab
        owner=SimpleNamespace(is_alive=lambda:False,release_error='fixture retained owner')
        tab=SimpleNamespace(app=SimpleNamespace(connection=owner),status=Mock())
        self.assertFalse(FlashTab.close_configuration(tab))
        owner.release_error=None
        self.assertTrue(FlashTab.close_configuration(tab))


if __name__=='__main__':unittest.main()
