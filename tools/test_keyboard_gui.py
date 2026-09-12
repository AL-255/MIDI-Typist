#!/usr/bin/env python3
"""Offline GUI model and real POSIX PTY transport tests (no keyboard access)."""
import copy
import os
import pty
import select
import shutil
import struct
import tempfile
import threading
import time
import unittest
from keyboard_gui_model import MAGIC, parse_build, SIZE, CAPTURE_POINTS, KeystrokeCapture, Decoder, decode, ansi_geometry, profile_from_snapshot, validate_profile, note_name, parse_note, FLAG_JANKO, JANKO_NOTES
from keyboard_gui_transport import Connection, find_cdc_device


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
    def __init__(self,fd,reject=False,mismatch=False,silent=False,key_rate=.000125):
        super().__init__(daemon=True)
        self.fd,self.reject,self.mismatch,self.silent = fd,reject,mismatch,silent
        self.stop_event = threading.Event()
        self.commands = []   # configuration commands (cfg ...)
        self.queries = []    # console queries such as `version`
        self.press,self.release = [3500]*61,[3600]*61
        self.mapping = [255]*61
        self.flags,self.ack,self.result,self.sequence = 7,0,0,0
        self.error = None
        self.calibration_state = 0
        self.build = 'v0.1.0-RZ03-0499'  # console build identity
        self.raw = None  # optional 61-value override for the next snapshots
        self.velocity_start = 1
        self.performance_mode = 0
        self.stream_mode = 'gui'       # 'gui' HKG packets or 'key' HKL1 records
        self.key_mode = None           # (session, threshold, sensor) while in key mode
        self.key_seq = 0; self.key_first = True
        self.key_rate = key_rate       # seconds between HKL1 records (8 ksps default)
        self.key_raw = 3900            # raw value for the pinned sensor
        self.key_cb = None             # optional callable(seq) -> raw override

    def run(self):
        buffer = bytearray(); streaming = False; last = 0
        try:
            while not self.stop_event.is_set():
                if select.select([self.fd],[],[],.01)[0]:
                    buffer.extend(os.read(self.fd,4096))
                    while b'\n' in buffer:
                        line,_,buffer = buffer.partition(b'\n')
                        fields = line.decode().split()
                        if fields == ['stream','gui']:
                            streaming = True; self.stream_mode = 'gui'
                        elif fields[:2] == ['stream','key']:
                            streaming = True; self.stream_mode = 'key'
                            self.key_mode = (int(fields[3]),int(fields[2]),int(fields[4]) if len(fields) > 4 else 255)
                            self.key_seq = 0; self.key_first = True
                        if fields == ['version']:
                            self.queries.append(fields)
                            if not self.silent:
                                os.write(self.fd,b'build='+self.build.encode()+b'\r\n')
                            continue
                        if not fields or fields[0] != 'cfg': continue
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
                if streaming and not self.silent:
                    if self.stream_mode == 'key' and time.monotonic()-last > self.key_rate:
                        session,threshold,sensor = self.key_mode
                        raw = self.key_cb(self.key_seq) if self.key_cb else self.key_raw
                        frame = bytearray(20)
                        struct.pack_into('<4sIIHBBH',frame,0,b'HKL1',session,self.key_seq,raw,sensor,
                                         1 if self.key_first else 0,threshold)
                        frame[18:20] = struct.pack('<H',sum(struct.unpack('<9H',frame[:18])) & 0xffff)
                        os.write(self.fd,frame)
                        self.key_seq += 1; self.key_first = False; last = time.monotonic()
                    elif self.stream_mode == 'gui' and time.monotonic()-last > .03:
                        os.write(self.fd,packet(self.ack,self.result,self.press,self.release,self.flags,self.sequence,mapping=self.mapping,
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
        for offset,value in ((1112,9),(1113,2),(1114,61),(1115,4),(1128,128),(1129,5),(1134,1),(1144,1)):
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

    def fake_sysfs(self,ports):
        """Minimal sysfs tree: tty entries -> interface dirs -> USB devices + dev nodes."""
        base = tempfile.mkdtemp(prefix='gui-sysfs-')
        self.addCleanup(shutil.rmtree,base,ignore_errors=True)
        os.makedirs(os.path.join(base,'class','tty'))
        os.makedirs(os.path.join(base,'dev'))
        for name,vendor,product in ports:
            device_dir = os.path.join(base,'devices','usb','dev-'+name)
            interface_dir = os.path.join(device_dir,'iface')
            tty_dir = os.path.join(interface_dir,'tty',name)
            os.makedirs(tty_dir)
            if vendor is not None:
                with open(os.path.join(device_dir,'idVendor'),'w') as stream: stream.write(str(vendor)+'\n')
            if product is not None:
                with open(os.path.join(device_dir,'idProduct'),'w') as stream: stream.write(str(product)+'\n')
            os.symlink(interface_dir,os.path.join(tty_dir,'device'))
            os.symlink(tty_dir,os.path.join(base,'class','tty',name))
            with open(os.path.join(base,'dev',name),'w') as stream: stream.write('')
        return base

    def test_device_detection(self):
        dev = lambda base: os.path.join(base,'dev')
        base = self.fake_sysfs([('ttyACM1','1532','02b0'),('ttyACM0','1d6b','0003')])
        self.assertEqual(find_cdc_device(sysfs=base,dev=dev(base)),dev(base)+'/ttyACM1')  # name order; only 1532:02b0 matches
        self.assertEqual(find_cdc_device(0x1d6b,0x0003,sysfs=base,dev=dev(base)),dev(base)+'/ttyACM0')
        self.assertEqual(find_cdc_device(0x1532,0x02b0,sysfs=base,dev=dev(base)),dev(base)+'/ttyACM1')  # explicit IDs
        base = self.fake_sysfs([('ttyACM0','1532','0200')])
        self.assertIsNone(find_cdc_device(sysfs=base,dev=dev(base)))  # wrong product
        base = self.fake_sysfs([('ttyACM0','zzzz','02b0')])
        self.assertIsNone(find_cdc_device(sysfs=base,dev=dev(base)))  # unreadable identity, no crash
        base = self.fake_sysfs([('ttyACM0',None,None)])
        self.assertIsNone(find_cdc_device(sysfs=base,dev=dev(base)))  # no USB identity on the chain
        base = self.fake_sysfs([('ttyUSB0','1532','02b0')])
        self.assertIsNone(find_cdc_device(sysfs=base,dev=dev(base)))  # non-ACM port ignored
        base = self.fake_sysfs([('ttyACM0','1532','02b0')])
        os.remove(dev(base)+'/ttyACM0')
        self.assertIsNone(find_cdc_device(sysfs=base,dev=dev(base)))  # matching port without a device node
        self.assertIsNone(find_cdc_device(sysfs='/nonexistent'))

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
        self.assertTrue(c.feed(3400,True))  # captures0 is None on legacy firmware
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
                           ('Q','C#4'),('Y','B4'),('J','D5'),(']','B5'),
                           ('Cap','C4'),('LSh','C#4'),('B','B4'),('RSh','B5')):
            self.assertEqual(JANKO_NOTES[label],note)
            self.assertIn(label,keys)
        labels = [k.label for k in ansi_geometry()]
        self.assertEqual(sorted(JANKO_NOTES),sorted(l for l in labels if l in JANKO_NOTES))
        self.assertEqual(len(JANKO_NOTES),50)
        for label in ('Fn','Spc','Ent','BkS','\\','Mnu','RAl','RCt','LCt','LGu','LAl'):
            self.assertNotIn(label,JANKO_NOTES)

    def test_decoder(self):
        data = packet()
        d = Decoder(); results = []
        for byte in b'old CDC text\n'+data+data: results.extend(d.feed(bytes([byte])))
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
            connection.stream_gui()
            until(lambda:connection.stream_mode == 'gui')
            until(lambda:connection.snapshot() and time.monotonic()-connection.snapshot()[0] < 1)
            self.assertTrue(connection.connected)
        finally: self.cleanup(*resources)

    def transport(self,**options):
        master,slave = pty.openpty()
        device = Device(master,**options); connection = Connection(os.ttyname(slave))
        device.start(); connection.start()
        return master,slave,device,connection

    def cleanup(self,master,slave,device,connection):
        connection.stop(); connection.join(1)
        device.stop_event.set(); device.join(1)
        os.close(master); os.close(slave)
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
            self.assertEqual(device.queries,[['version']])
            self.assertEqual((connection.build,connection.build_target),('v0.1.0-RZ03-0499','RZ03-0499'))
            self.assertEqual(parse_build(b'\nbuild=v0.1.0-RZ03-0499\r\n'),('v0.1.0-RZ03-0499','0.1.0','RZ03-0499'))
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
            self.assertEqual(len(device.commands),1)
            self.assertFalse(connection.connected)
        finally: self.cleanup(*resources)


if __name__ == '__main__': unittest.main()
