#!/usr/bin/env python3
"""Offline GUI model and bidirectional SysEx transport tests (no keyboard access)."""
import copy
import os
from pathlib import Path
import queue
from unittest.mock import patch
import midi_sysex as sx
import select
import shutil
import struct
import tempfile
import threading
import time
import unittest
from keyboard_gui_model import MAGIC, parse_build, SIZE, CAPTURE_POINTS, KeystrokeCapture, Decoder, decode, ansi_geometry, profile_from_snapshot, validate_profile, note_name, parse_note, FLAG_JANKO, JANKO_NOTES
from keyboard_gui_transport import Connection, SAMPLE_CAPACITY


def packet(ack=1, result=1, press=None, release=None, flags=7, sequence=0, velocity_start=1,
           velocity=None, captures=None, states=None, mapping=None, performance_mode=0, octave=0, calibration_state=0, raw=None):
    data = bytearray(SIZE)
    struct.pack_into('<4sH6B5I',data,0,MAGIC,SIZE,velocity_start,1,61,flags,result,0,sequence,0,ack,0,0)
    raw_values = raw if raw is not None else [3900]*61
    press_values = press or [3500]*61
    for offset,values in ((32,raw_values),(162,press_values),(292,release or [3600]*61)):
        struct.pack_into('<61H',data,offset,*values)
    bits = 0
    for i,value in enumerate(raw_values):
        if value < press_values[i]: bits |= 1 << i
    data[422:431] = bits.to_bytes(9,'little')
    struct.pack_into('<61f',data,447,*(velocity or [0]*61))
    struct.pack_into('<61I',data,707,*(captures or [0]*61))
    data[967:1028] = bytes(states or [1]*61)
    struct.pack_into('<BbBB',data,1032,performance_mode,octave,1,0)
    data[1036:1097] = bytes(mapping or [255]*61)
    data[1112]=calibration_state
    data[1114]=255; data[1115]=4 | int(1 <= calibration_state <= 5)
    struct.pack_into('<I',data,SIZE-4,sum(struct.unpack_from(f'<{(SIZE-4)//2}H',data)))
    return bytes(data)


class Device(threading.Thread):
    def __init__(self,fd=None,reject=False,mismatch=False,silent=False,key_rate=.000125):
        super().__init__(daemon=True)
        self.fd,self.reject,self.mismatch,self.silent = fd,reject,mismatch,silent
        self.inbox = queue.Queue()
        self.outbox = queue.Queue()
        self.session = None
        self.stop_event = threading.Event()
        self.commands = []   # configuration commands (cfg ...)
        self.queries = []    # console queries such as `version`
        self.press,self.release = [3500]*61,[3600]*61
        self.mapping = [255]*61
        self.flags,self.ack,self.result,self.sequence = 7,0,0,0
        self.error = None
        self.calibration_state = 0
        self.build = 'v0.1.0-RZ03-0499 git='+'a'*40+' state=dirty'
        self.raw = None  # optional 61-value override for the next snapshots
        self.velocity_start = 1
        self.performance_mode = 0
        self.stream_mode = 'gui'       # 'gui' HKG packets or 'key' HKL1 records
        self.key_mode = None           # (session, threshold, sensor) while in key mode
        self.key_seq = 0; self.key_first = True
        self.key_rate = key_rate       # seconds between HKL1 records (8 ksps default)
        self.key_raw = 3900            # raw value for the pinned sensor
        self.key_cb = None             # optional callable(seq) -> raw override

    def send(self, data): self.inbox.put(data)
    def receive(self, timeout):
        try: return self.outbox.get(timeout=timeout)
        except queue.Empty: return None
    def close(self): pass
    def emit(self, kind, payload=b'', sequence=0):
        self.outbox.put(sx.encode(kind, self.session, sequence, payload))

    def run(self):
        streaming = False; last = 0
        try:
            while not self.stop_event.is_set():
                try: wire = self.inbox.get(timeout=.001)
                except queue.Empty: wire = None
                if wire:
                    kind, self.session, sequence, payload = sx.decode(wire)
                    if kind == sx.HELLO:
                        self.queries.append(['hello'])
                        if not self.silent: self.emit(sx.READY, b'build='+self.build.encode())
                    elif kind == sx.CLOSE: streaming = False
                    elif kind == sx.COMMAND:
                        fields = payload.decode().split()
                        if fields == ['stream','gui']:
                            streaming = True; self.stream_mode = 'gui'
                        elif fields[:2] == ['stream','key']:
                            streaming = True; self.stream_mode = 'key'
                            self.key_mode = (int(fields[3]),int(fields[2]),int(fields[4]))
                            self.key_seq = 0; self.key_first = True
                        if fields and fields[0] == 'cfg':
                            self.commands.append(fields)
                            self.ack = int(fields[2]); self.result = 1
                            if fields[1] == 'set':
                                index,press,release = map(int,fields[3:])
                                if self.reject: self.result = 2
                                elif not self.mismatch: self.press[index],self.release[index] = press,release
                            elif fields[1] == 'enable': self.flags = (self.flags & ~3) | int(fields[3])
                            elif fields[1] == 'all':
                                if self.reject: self.result = 2
                                elif not self.mismatch:
                                    self.press = [int(fields[3])]*61; self.release = [int(fields[4])]*61
                            elif fields[1] == 'midi':
                                if self.reject: self.result = 2
                                elif not self.mismatch: self.mapping[int(fields[3])] = int(fields[4])
                            elif fields[1] == 'velocity':
                                if not self.mismatch: self.velocity_start = int(fields[3])
                            elif fields[1] == 'calibrate': self.calibration_state=3
                            elif fields[1] == 'calcancel': self.calibration_state=7

                        if not self.silent: self.emit(sx.ACK, sequence=sequence)
                if streaming and not self.silent:
                    if self.stream_mode == 'key' and time.monotonic()-last > self.key_rate:
                        session,threshold,sensor = self.key_mode
                        raw = self.key_cb(self.key_seq) if self.key_cb else self.key_raw
                        frame = bytearray(20)
                        struct.pack_into('<4sIIHBBH',frame,0,b'HKL1',session,self.key_seq,raw,sensor,
                                         1 if self.key_first else 0,threshold)
                        frame[18:20] = struct.pack('<H',sum(struct.unpack('<9H',frame[:18])) & 0xffff)
                        self.emit(sx.SAMPLES,frame)
                        self.key_seq += 1; self.key_first = False; last = time.monotonic()
                    elif self.stream_mode == 'gui' and time.monotonic()-last > .03:
                        self.emit(sx.SNAPSHOT,packet(self.ack,self.result,self.press,self.release,self.flags,self.sequence,mapping=self.mapping,
                                               calibration_state=self.calibration_state,raw=self.raw,
                                               velocity_start=self.velocity_start,performance_mode=self.performance_mode,
                                               states=[9,9]+[1]*59 if self.calibration_state==3 else None))
                        self.sequence += 1; last = time.monotonic()
        except Exception as error: self.error = error


def until(predicate,seconds=3):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        if predicate(): return
        time.sleep(.01)
    raise AssertionError('Timed out waiting for test condition')


class Tests(unittest.TestCase):
    def test_capture_buffer_integrity(self):
        connection = Connection('/unused')
        connection.key_sensor = 32
        for i in range(SAMPLE_CAPACITY): connection.push_sample(i)
        self.assertEqual(connection.drain_samples(sensor=31), [])
        self.assertEqual(len(connection.samples), SAMPLE_CAPACITY)
        with self.assertRaisesRegex(BufferError, 'overflow'):
            connection.push_sample(1)
        self.assertEqual(connection.drain_samples(), [])
        connection.push_sample(3900)
        self.assertEqual(connection.drain_samples(sensor=32), [3900])

    def test_capture_waits_for_configuration(self):
        resources = self.transport(); _,_,device,connection = resources
        try:
            until(lambda:connection.connected)
            connection.submit('set',32,2800,3200)
            connection.submit('enable',1)
            connection.stream_key(2800,32)
            until(lambda:connection.stream_mode == 'key')
            self.assertEqual(connection.snapshot()[1].press[32],2800)
            self.assertEqual(device.commands[-1][1],'enable')
            messages = []
            while not connection.events.empty(): messages.append(connection.events.get_nowait())
            self.assertTrue(any(message.startswith('Confirmed set') for message in messages))
            self.assertTrue(any(message.startswith('Confirmed enable') for message in messages))
            connection.submit('velocity',5)
            connection.stream_gui()
            until(lambda:connection.snapshot()[1].velocity_start == 5)
        finally: self.cleanup(*resources)

    def test_calibration_model(self):
        s=decode(packet())
        self.assertEqual((s.calibration_state,s.calibration_flags,s.calibration_selected),(0,4,255))
        b=bytearray(packet())
        struct.pack_into('<4BHH',b,1112,3,1,32,5,500,4000)
        b[1120]=1
        struct.pack_into('<HH',b,1130,4000,1000)
        def checksum(data):
            struct.pack_into('<I',data,len(data)-4,sum(struct.unpack_from(f'<{(len(data)-4)//2}H',data)))
            return data
        s=decode(checksum(b)); self.assertTrue(s.calibration_done[0]); self.assertEqual(s.calibration_hold,500)
        for offset,value in ((1112,9),(1113,2),(1114,61),(1115,4),(1128,128),(1129,5),(1134,1),(1144,8),(1145,2)):
            bad=bytearray(b); bad[offset]=value
            with self.assertRaises(ValueError): decode(checksum(bad))
        parallel=bytearray(packet(calibration_state=3,states=[8,8]+[0]*59))
        s=decode(parallel); self.assertEqual(s.velocity_state[:3],(8,8,0))
        parallel[1112]=0; parallel[1115]=4
        with self.assertRaisesRegex(ValueError,'hold bitmap'): decode(checksum(parallel))
        with self.assertRaisesRegex(ValueError,'hold bitmap'): decode(packet(states=[8]+[0]*60))

    def test_midi_model(self):
        for n in range(128): self.assertEqual(parse_note(note_name(n)),n)
        self.assertEqual(parse_note('Eb0'),15)
        self.assertEqual(parse_note('C0'),12)
        self.assertEqual(parse_note('Off'),255)
        for bad in ('128','-1','C10','oops','C-2'):
            with self.assertRaises(ValueError): parse_note(bad)
        mapping = [255]*61; mapping[32]=60
        s=decode(packet(mapping=mapping,performance_mode=1,octave=-2))
        self.assertEqual((s.performance_mode,s.octave,s.midi_mapping[32]),(1,-2,60))
        p=profile_from_snapshot(s)
        self.assertEqual(p['version'],2)
        validate_profile(p)
        with self.assertRaisesRegex(ValueError,'version 2'): validate_profile({**p,'version':1})
        for key in p['keys']:
            if key['label'] in ('Fn','LCt','LGu','LAl','RAl','RCt','Spc'):
                key['midi']=60
                with self.assertRaisesRegex(ValueError,'Reserved MIDI'): validate_profile(p)
                key['midi']=255
        p['keys'][32]['midi']=128
        with self.assertRaises(ValueError): validate_profile(p)
        for offset,value in ((1032,2),(1033,11),(1034,2),(1035,2),(1036,128),(1101,1),(1112,1)):
            bad=bytearray(packet()); bad[offset]=value
            struct.pack_into('<I',bad,len(bad)-4,sum(struct.unpack_from(f'<{(len(bad)-4)//2}H',bad)))
            with self.assertRaises(ValueError): decode(bad)

    def test_midi_transport(self):
        resources=self.transport(); _,_,device,connection=resources
        try:
            until(lambda:connection.connected)
            connection.submit('midi',32,60)
            until(lambda:connection.snapshot()[1].midi_mapping[32]==60)
            self.assertEqual(device.commands[-1][1],'midi')
        finally: self.cleanup(*resources)
        resources=self.transport(mismatch=True); _,_,_,connection=resources
        try:
            until(lambda:connection.connected)
            connection.submit('midi',32,60)
            until(lambda:not connection.is_alive())
            self.assertTrue(any('MIDI mapping readback' in x for x in list(connection.events.queue)))
        finally: self.cleanup(*resources)

    def test_geometry(self):
        keys = ansi_geometry()
        self.assertEqual(sorted(k.sensor for k in keys),list(range(61)))
        self.assertEqual(next(k for k in keys if k.label == 'A').sensor,32)
        self.assertEqual(next(k for k in keys if k.label == 'Spc').width,6.25)
        self.assertEqual(next(k for k in keys if k.label == 'Fn').x,10)
        self.assertEqual(next(k for k in keys if k.label == 'RAl').x,11.25)
        for row in range(5):
            values = [k for k in keys if k.y == row]
            self.assertEqual(sum(k.width for k in values),15)
            for left,right in zip(values,values[1:]): self.assertEqual(left.x+left.width,right.x)

    def test_device_detection(self):
        from midi_backend import find_midi_device, is_control_port
        self.assertTrue(is_control_port('Huntsman V3 Pro Mini MIDI:Huntsman V3 Pro Mini MIDI MIDI- 28:1'))
        self.assertTrue(is_control_port('MIDI-Typist Control'))
        self.assertFalse(is_control_port('Huntsman V3 Pro Mini MIDI:Huntsman V3 Pro Mini MIDI MIDI  28:0'))
        for ports, expected in (([], None), (['Control'], 'Control'), (['Control1','Control2'], None)):
            with patch('midi_backend.control_ports', return_value=ports):
                self.assertEqual(find_midi_device(), expected)

    def test_keystroke_capture(self):
        self.assertEqual(CAPTURE_POINTS,20)
        c = KeystrokeCapture()
        self.assertTrue(c.armed)
        self.assertFalse(c.feed(3900,False))
        self.assertTrue(c.armed)  # no trigger yet: waveform stays frozen
        self.assertTrue(c.feed(3400,True))  # down edge: trigger frame is sample zero
        self.assertEqual((c.points,c.armed,c.done),([3400],False,False))
        self.assertTrue(c.feed(3600,False))  # release does not truncate the capture
        for i in range(CAPTURE_POINTS-2): self.assertTrue(c.feed(3000+i,False))
        self.assertTrue(c.done)
        self.assertEqual(len(c.points),CAPTURE_POINTS)
        self.assertIsNone(c.fit)  # no captures data on this fake device
        before = list(c.points)
        self.assertFalse(c.feed(2800,False))  # held: no refresh, points frozen
        self.assertEqual(c.points,before)
        self.assertTrue(c.feed(2000,True))  # latest keystroke wins and restarts
        self.assertEqual((c.points,c.done),([2000],False))
        c = KeystrokeCapture()
        self.assertFalse(c.feed(2500,True))  # held at arm time is a baseline, not a trigger
        self.assertTrue(c.armed)
        self.assertFalse(c.feed(3900,False))
        self.assertTrue(c.feed(2500,True))  # release + press is the real edge
        self.assertEqual(c.points,[2500])
        c = KeystrokeCapture()
        c.feed(3900,False)  # released baseline before the trigger
        self.assertTrue(c.feed(3400,True))  # raw-only capture has no device fit counter
        self.assertTrue(c.feed(3200,True,captures=1,velocity=0.5,fit_valid=True))
        self.assertIsNone(c.fit)  # no trigger-frame counter to compare against
        c = KeystrokeCapture()
        c.feed(3900,False,captures=4,velocity=0.0,fit_valid=True)
        self.assertTrue(c.feed(3400,True,captures=5,velocity=0.42,fit_valid=True))
        for _ in range(CAPTURE_POINTS-1):
            c.feed(3200,True,captures=5,velocity=0.42,fit_valid=True)
        self.assertEqual(c.fit,(5,0.42))  # fit completed in the trigger snapshot
        c = KeystrokeCapture()
        c.feed(3900,False,captures=4,velocity=0.0)
        c.feed(3400,True,captures=4,velocity=0.0)
        self.assertIsNone(c.fit)  # stale fit from before the trigger is not attributed
        c.feed(3300,True,captures=5,velocity=0.87,fit_valid=True)
        self.assertEqual(c.fit,(5,0.87))  # first counter rise after the trigger
        c.reset()
        self.assertTrue(c.armed); self.assertEqual(c.points,[]); self.assertIsNone(c.fit)

    def test_janko_layout_model(self):
        s = decode(packet(flags=7|FLAG_JANKO))
        self.assertTrue(s.flags & FLAG_JANKO)
        self.assertEqual(decode(packet(flags=7)).flags & FLAG_JANKO,0)
        with self.assertRaises(ValueError): decode(packet(flags=128))
        # Host display table mirrors the firmware rows: whole-tone steps inside
        # a row and the specified staggered notes on the physical keys.
        keys = {k.label: k.sensor for k in ansi_geometry()}
        for label,note in (('Esc','A#3'),('1','C4'),('=','A#5'),('Tab','B3'),
                           ('Q','C#4'),('Y','B4'),('J','D5'),(']','B5'),('\\','C#6'),
                           ('Cap','C4'),('LSh','C#4'),('B','B4'),('RSh','B5'),
                           ('BkS','C6'),('Ent','C6')):
            self.assertEqual(JANKO_NOTES[label],note)
            self.assertIn(label,keys)
        labels = [k.label for k in ansi_geometry()]
        self.assertEqual(sorted(JANKO_NOTES),sorted(l for l in labels if l in JANKO_NOTES))
        self.assertEqual(len(JANKO_NOTES),53)
        for label in ('Fn','Spc','Mnu','RAl','RCt','LCt','LGu','LAl'):
            self.assertNotIn(label,JANKO_NOTES)

    def test_decoder(self):
        data = packet()
        d = Decoder(); results = []
        for byte in data+data: results.extend(d.feed(bytes([byte])))
        self.assertEqual(len(results),2)
        self.assertEqual(results[0].raw,(3900,)*61)
        for index in (5,9,20,70,425,470,479):
            broken = bytearray(data); broken[index] ^= 1
            with self.assertRaises(ValueError): decode(broken)
        with self.assertRaises(ValueError): list(d.feed(b'x'+data))
        s = decode(packet(captures=[45]*61,states=[7]*61))
        self.assertEqual(s.captures,(45,)*61)
        self.assertEqual(s.velocity_state,(7,)*61)
        for level in (1,5,10):
            self.assertEqual(decode(packet(velocity_start=level)).velocity_start,level)
        for bad in (0,11,255):
            with self.assertRaises(ValueError): decode(packet(velocity_start=bad))
        with self.assertRaises(ValueError): decode(packet(states=[8]*61))
        with self.assertRaises(ValueError): decode(packet(velocity=[9828001]*61))
        for value in (0.0,0.25,0.5,1.0):
            s = decode(packet(velocity=[value]*61,states=[2]*61))
            self.assertEqual(s.velocity,(value,)*61)
            self.assertIsInstance(s.velocity[0],float)
        for value in (-0.1,1.1,float('nan'),float('inf'),-float('inf')):
            with self.assertRaises(ValueError): decode(packet(velocity=[value]*61))

    def test_profiles(self):
        profile = profile_from_snapshot(decode(packet()))
        self.assertEqual(len(validate_profile(profile)),61)
        for field,value in (('sensor',61),('label','Wrong'),('press',3700),('press',True),('release',4097)):
            bad = copy.deepcopy(profile); bad['keys'][0][field] = value
            with self.assertRaises(ValueError): validate_profile(bad)
        bad = copy.deepcopy(profile); bad['keys'][0] = bad['keys'][1]
        with self.assertRaises(ValueError): validate_profile(bad)

    def test_transport_key_stream_switch(self):
        resources = self.transport(); _,_,device,connection = resources
        try:
            until(lambda:connection.connected)
            self.assertEqual(connection.stream_mode,'gui')
            connection.stream_key(3500,5)
            until(lambda:connection.stream_mode == 'key')
            self.assertEqual((connection.key_sensor,connection.key_threshold),(5,3500))
            self.assertEqual(device.stream_mode,'key')
            collected = []
            def accumulate():
                collected.extend(connection.drain_samples())
                return len(collected) >= 30
            until(accumulate)
            self.assertTrue(all(value == 3900 for value in collected))
            # GUI telemetry pauses; the connection survives past the 2 s
            # telemetry timeout because full-rate samples keep it alive.
            time.sleep(2.2)
            self.assertTrue(connection.is_alive() and connection.connected)
            self.assertEqual(connection.stream_mode,'key')
            previous_session = connection.key_session
            connection.stream_key(3400,32)
            until(lambda:connection.key_session != previous_session and connection.key_sensor == 32)
            until(lambda:bool(connection.drain_samples(32)))
            self.assertTrue(connection.is_alive() and connection.connected)
            connection.stream_gui()
            until(lambda:connection.stream_mode == 'gui')
            until(lambda:connection.snapshot() and time.monotonic()-connection.snapshot()[0] < 1)
            self.assertTrue(connection.connected)
        finally: self.cleanup(*resources)

    def transport(self,**options):
        master = slave = None
        device = Device(**options); connection = Connection('fake', backend_factory=lambda _:device)
        device.start(); connection.start()
        return master,slave,device,connection

    def cleanup(self,master,slave,device,connection):
        connection.stop(); connection.join(1)
        device.stop_event.set(); device.join(1)
        self.assertFalse(connection.is_alive()); self.assertIsNone(device.error)

    def test_transport_ack_and_profile_batch(self):
        resources = self.transport(); _,_,device,connection = resources
        try:
            until(lambda:connection.connected)
            connection.submit('enable',0)
            for i in range(61): connection.submit('set',i,3000+i,3300+i)
            connection.submit('enable',1)
            until(lambda:len(device.commands) == 64,5)
            until(lambda:connection.snapshot()[1].ack == int(device.commands[-1][2]))
            s = connection.snapshot()[1]
            self.assertEqual(s.press,tuple(range(3000,3061)))
            self.assertEqual(s.release,tuple(range(3300,3361)))
            self.assertTrue(s.flags & 1)
            connection.submit('all',3100,3400)
            until(lambda:connection.snapshot()[1].press == (3100,)*61)
            self.assertEqual(connection.snapshot()[1].release,(3400,)*61)
        finally: self.cleanup(*resources)

    def test_transport_rejection_and_readback_mismatch_cancel(self):
        for option in ('reject','mismatch'):
            for action,args in (('set',(32,3000,3200)),('all',(3000,3200))):
                resources = self.transport(**{option:True}); _,_,device,connection = resources
                try:
                    until(lambda:connection.connected)
                    connection.submit(action,*args)
                    connection.submit('enable',1)
                    until(lambda:not connection.is_alive())
                    self.assertEqual(len(device.commands),2)
                    self.assertFalse(connection.connected)
                finally: self.cleanup(*resources)

    def test_transport_build_identity_and_velocity_start(self):
        resources = self.transport(); _,_,device,connection = resources
        try:
            until(lambda:connection.connected)
            self.assertEqual(device.queries,[['hello']])
            self.assertEqual((connection.build,connection.build_target),(device.build,'RZ03-0499'))
            self.assertEqual(parse_build('build='+device.build+'\r\n'),(device.build,'0.1.0','RZ03-0499'))
            self.assertIsNone(parse_build('build=v0.1.0-RZ03-0499\n'))
            self.assertIsNone(parse_build('build=v0.1.0-RZ03-0499 git=unknown state=clean\n'))
            for state in ('clean','dirty'):
                text='v0.1.0-RZ03-0499 git='+'b'*64+' state='+state
                self.assertEqual(parse_build('build='+text+'\n')[0],text)
            self.assertIsNone(parse_build(b'build=v0.1.0-RZ03-'))  # partial read stays unresolved
            self.assertEqual(connection.snapshot()[1].velocity_start,1)
            connection.submit('velocity',7)
            until(lambda:device.velocity_start == 7)
            until(lambda:connection.snapshot()[1].velocity_start == 7,3)
            self.assertEqual(connection.snapshot()[1].velocity_start,7)
            connection.submit('velocity',10); until(lambda:device.velocity_start == 10)
        finally: self.cleanup(*resources)
        resources = self.transport(mismatch=True); _,_,device,connection = resources
        try:
            until(lambda:connection.connected)
            connection.submit('velocity',4)
            until(lambda:not connection.is_alive())
            self.assertEqual(device.velocity_start,1)  # device never applied it
        finally: self.cleanup(*resources)

    def test_transport_stale_does_not_retry(self):
        resources = self.transport(silent=True); _,_,device,connection = resources
        try:
            until(lambda:not connection.is_alive(),4)
            self.assertEqual(len(device.commands),0)
            self.assertFalse(connection.connected)
        finally: self.cleanup(*resources)


class PortableTypography(unittest.TestCase):
    """Fonts and the scrolling settings panel must stay platform-independent."""

    def test_candidates_cover_every_platform(self):
        import gui_fonts
        for family in ('Segoe UI','Helvetica Neue','Noto Sans','DejaVu Sans','Cantarell'):
            self.assertIn(family,gui_fonts.NATIVE_SANS_FAMILIES)
        for family in ('Consolas','Menlo','DejaVu Sans Mono','Ubuntu Mono'):
            self.assertIn(family,gui_fonts.NATIVE_MONO_FAMILIES)
        # A Tk build without fontconfig needs X11 bitmap faces with real bold.
        self.assertIn('Lucida',gui_fonts.X11_SANS_FAMILIES)
        self.assertIn('Terminus',gui_fonts.X11_MONO_FAMILIES)

    def test_no_hard_coded_family_tuples(self):
        # Tk does not substitute a missing family: ('sans', 10) silently
        # becomes the `fixed` bitmap font, which is what looked pixelated.
        tools = Path(__file__).resolve().parent
        for name in ('keyboard_gui.py','keyboard_flash_tab.py'):
            source = (tools/name).read_text()
            for generic in ("'sans'",'"sans"',"'monospace'",'"monospace"'):
                self.assertNotIn(f'font=({generic}',source,name)

    def test_native_families_are_the_antialiased_ones(self):
        import gui_fonts
        for family in ('DejaVu Sans','Segoe UI','Helvetica Neue','Noto Sans'):
            self.assertIn(family,gui_fonts.NATIVE_SANS_FAMILIES)
        for family in ('Lucida','Helvetica','Terminus'):
            self.assertTrue(family in gui_fonts.X11_SANS_FAMILIES or
                            family in gui_fonts.X11_MONO_FAMILIES)
        # The two worlds never overlap: a fontconfig family is not a bitmap one.
        self.assertFalse(set(gui_fonts.NATIVE_SANS_FAMILIES) & set(gui_fonts.X11_SANS_FAMILIES))

    def test_x11_pixel_sizes_are_native_pixels_only(self):
        import gui_fonts
        self.assertTrue(all(size > 0 for size in gui_fonts.X11_PIXEL_SIZES))
        self.assertIn(gui_fonts.X11_BASE_PIXELS,gui_fonts.X11_PIXEL_SIZES)
        self.assertIn(gui_fonts.X11_TITLE_PIXELS,gui_fonts.X11_PIXEL_SIZES)
        self.assertTrue(gui_fonts.X11_TITLE_PIXELS > gui_fonts.X11_BASE_PIXELS)

    def test_scroll_area_handles_every_wheel_protocol(self):
        import gui_widgets
        source = Path(gui_widgets.__file__).read_text()
        for sequence in ('<MouseWheel>','<Button-4>','<Button-5>'):
            self.assertIn(sequence,source)
        self.assertTrue(hasattr(gui_widgets.ScrollArea,'overflowing'))


if __name__ == '__main__': unittest.main()
