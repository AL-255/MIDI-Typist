#!/usr/bin/env python3
"""Standalone ANSI keyboard monitor and per-key Schmitt configuration GUI."""
import argparse
from collections import deque
import json
import math
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from keyboard_gui_model import Snapshot, ansi_geometry, profile_from_snapshot, validate_pair, validate_profile, note_name, parse_note, MIDI_CONTROLS, CAPTURE_POINTS, KeystrokeCapture, FLAG_JANKO, JANKO_NOTES, KNOWN_TARGETS
from keyboard_gui_transport import Connection, find_cdc_device, USB_VENDOR_ID, USB_PRODUCT_ID
from last_key_stream import press_velocity, velocity_window, VELOCITY_WINDOW
import firmware_flasher

AXIS_W = 34  # left gutter for the raw-value vertical axis of the bottom plot
# Fn+Tab (MIDI) trigger point: level 1 is the velocity window's bottom-out
# floor, level 0 (10) stops one count below the release threshold. Mirrors
# keyboard_raw_press_level() in the firmware.
TRIGGER_FLOOR, TRIGGER_CEILING = 1500, 3599
TRIGGER_LEVELS = [f'{level} — {TRIGGER_FLOOR + (level-1)*(TRIGGER_CEILING-TRIGGER_FLOOR)//9}' for level in range(1,11)]
VELOCITY_STARTS = [f'{level} — {(level-1)*100//9}%' for level in range(1,11)]


class App:
    def __init__(self,root,device='/dev/ttyACM0',demo=False):
        self.root,self.demo = root,demo
        self.connection = None
        self.snapshot = None
        self.device_build = None  # build identity reported by the connected device
        self.selected = 32
        self.keys = ansi_geometry()
        self.items = {}
        self.titles = {}
        self.history = deque(maxlen=180)
        self.capture = KeystrokeCapture()
        self.hold_mode = tk.BooleanVar(value=False)
        self.key_capture = False  # full-rate per-key stream active
        self.capture_rate = 0.0   # measured samples/s of the active key stream
        self._rate_count = 0; self._rate_at = None
        self.last_sequence = None
        self.initial_fields = False
        self.flashing = False           # a worker thread owns the device
        self.flash_queue = queue.Queue()
        root.title('Huntsman • Keyboard configuration')
        root.geometry('1180x920'); root.minsize(930,900)
        root.configure(bg='#101820')
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TFrame',background='#101820')
        style.configure('TLabel',background='#101820',foreground='#d9e5ec')
        style.configure('TButton',padding=7)
        style.configure('Title.TLabel',font=('sans',18,'bold'))
        outer = ttk.Frame(root,padding=18); outer.pack(fill='both',expand=True)
        ttk.Label(outer,text='HUNTSMAN  /  KEYBOARD',style='Title.TLabel').pack(anchor='w')
        ttk.Label(outer,text='Raw Schmitt thresholds • press below the lower value, release above the upper value').pack(anchor='w',pady=(3,12))
        bar = ttk.Frame(outer); bar.pack(fill='x')
        self.device = tk.StringVar(value=device)
        ttk.Entry(bar,textvariable=self.device,width=25).pack(side='left')
        self.detect_button = ttk.Button(bar,text='Detect',command=self.detect)
        self.detect_button.pack(side='left',padx=(6,0))
        self.connect_button = ttk.Button(bar,text='Connect',command=self.toggle_connection)
        self.connect_button.pack(side='left',padx=6)
        self.enable_button = ttk.Button(bar,text='Enable keyboard',command=lambda:self.enable(True))
        self.enable_button.pack(side='left',padx=3)
        self.disable_button = ttk.Button(bar,text='Disable keyboard',command=lambda:self.enable(False))
        self.disable_button.pack(side='left',padx=3)
        ttk.Button(bar,text='Save profile…',command=self.save_profile).pack(side='right',padx=3)
        self.load_button = ttk.Button(bar,text='Load + apply profile…',command=self.load_profile)
        self.load_button.pack(side='right',padx=3)
        self.status = tk.StringVar(value=('DEMO — no device access' if demo else
            'Disconnected — press Connect to use the detected device' if device else
            f'Disconnected — no {USB_VENDOR_ID:04x}:{USB_PRODUCT_ID:04x} CDC device detected; click Detect'))
        ttk.Label(outer,textvariable=self.status,wraplength=1100).pack(anchor='w',pady=(12,4))
        self.canvas = tk.Canvas(outer,height=270,bg='#101820',highlightthickness=0)
        self.canvas.pack(fill='x'); self.canvas.bind('<Configure>',lambda _:self.draw())
        ttk.Label(outer,text='Orange = sensor down   •   Cyan = selected   •   Numbers = raw / device velocity (0–1; legacy firmware: counts/s)').pack(anchor='w',pady=(0,12))
        calbar = ttk.Frame(outer); calbar.pack(fill='x',pady=(0,6))
        self.calibrate_button = ttk.Button(calbar,text='Calibrate keys → device flash',command=self.calibrate)
        self.calibrate_button.pack(side='left')
        self.cancel_calibration_button = ttk.Button(calbar,text='Cancel calibration',command=self.cancel_calibration)
        self.cancel_calibration_button.pack(side='left',padx=6)
        self.calibration_status = tk.StringVar(value='Calibration: connect to a keyboard to read status.')
        ttk.Label(outer,textvariable=self.calibration_status,wraplength=1100).pack(anchor='w',pady=(0,8))
        self.message = tk.StringVar(value='Calibration saves only after all keys are completed; thresholds and MIDI mappings remain RAM-only.')
        self.footer = ttk.Label(outer,textvariable=self.message,wraplength=890)
        self.footer.pack(side='bottom',anchor='w',pady=(12,0))
        lower = ttk.Frame(outer); lower.pack(fill='both',expand=True)
        panel = ttk.Frame(lower); panel.pack(side='left',fill='y',padx=(0,20))
        self.key_title = tk.StringVar(value='A  /  sensor 32')
        ttk.Label(panel,textvariable=self.key_title,style='Title.TLabel').pack(anchor='w')
        self.details = tk.StringVar(value='Waiting for device telemetry')
        ttk.Label(panel,textvariable=self.details,justify='left',wraplength=355).pack(anchor='w',pady=10)
        self.press = tk.StringVar(value='3500'); self.release = tk.StringVar(value='3600')
        for title,var in (('Press when raw <',self.press),('Release when raw >',self.release)):
            row = ttk.Frame(panel); row.pack(fill='x',pady=3)
            ttk.Label(row,text=title,width=21).pack(side='left')
            ttk.Entry(row,textvariable=var,width=9).pack(side='left')
        buttons = ttk.Frame(panel); buttons.pack(anchor='w',pady=9)
        self.apply_button = ttk.Button(buttons,text='Apply to selected key',command=self.apply)
        self.apply_button.pack(side='left')
        self.apply_all_button = ttk.Button(buttons,text='Apply thresholds to all keys',command=self.apply_all)
        self.apply_all_button.pack(side='left',padx=(6,0))
        midi_row = ttk.Frame(panel); midi_row.pack(anchor='w',pady=3)
        ttk.Label(midi_row,text='MIDI note: ').pack(side='left')
        self.midi_note = tk.StringVar(value='Off')
        self.midi_entry = ttk.Combobox(midi_row,textvariable=self.midi_note,width=9,
                                      values=['Off']+[note_name(n) for n in range(128)])
        self.midi_entry.pack(side='left')
        self.midi_button = ttk.Button(midi_row,text='Apply MIDI mapping',command=self.apply_midi)
        self.midi_button.pack(side='left',padx=6)
        device_row = ttk.Frame(panel); device_row.pack(anchor='w',pady=3)
        ttk.Label(device_row,text='Trigger point: ').pack(side='left')
        self.trigger_point = tk.StringVar(value=TRIGGER_LEVELS[9])
        self.trigger_entry = ttk.Combobox(device_row,textvariable=self.trigger_point,width=13,
                                          values=list(TRIGGER_LEVELS),state='readonly')
        self.trigger_entry.pack(side='left')
        self.trigger_button = ttk.Button(device_row,text='Apply trigger point to all keys',command=self.apply_trigger_point)
        self.trigger_button.pack(side='left',padx=6)
        velocity_row = ttk.Frame(panel); velocity_row.pack(anchor='w',pady=3)
        ttk.Label(velocity_row,text='Velocity start: ').pack(side='left')
        self.velocity_start = tk.StringVar(value=VELOCITY_STARTS[0])
        self.velocity_entry = ttk.Combobox(velocity_row,textvariable=self.velocity_start,width=13,
                                           values=list(VELOCITY_STARTS),state='readonly')
        self.velocity_entry.pack(side='left')
        self.velocity_button = ttk.Button(velocity_row,text='Apply velocity start',command=self.apply_velocity_start)
        self.velocity_button.pack(side='left',padx=6)
        flash_row = ttk.Frame(panel); flash_row.pack(anchor='w',pady=3)
        ttk.Label(flash_row,text='Firmware: ').pack(side='left')
        self.flash_image = tk.StringVar(value=str(firmware_flasher.REPO_ROOT/'build-keyboard-fn-menu'/'huntsman_firmware.bin'))
        self.flash_entry = ttk.Entry(flash_row,textvariable=self.flash_image,width=44)
        self.flash_entry.pack(side='left')
        self.flash_browse = ttk.Button(flash_row,text='Browse…',command=self.browse_firmware)
        self.flash_browse.pack(side='left',padx=4)
        self.flash_button = ttk.Button(flash_row,text='Flash application…',command=self.flash_firmware)
        self.flash_button.pack(side='left')
        self.flash_progress = ttk.Progressbar(panel,orient='horizontal',length=420,mode='determinate')
        self.flash_progress.pack(anchor='w',pady=(0,2))
        self.flash_status = tk.StringVar(value='Flashing writes the application only; changes are listed before it starts.')
        ttk.Label(panel,textvariable=self.flash_status,justify='left').pack(anchor='w')
        ttk.Label(panel,text='Fn+Tab (MIDI): trigger point, 1 = bottom-out … 0 = release − 1\nFn+V: transmitted-velocity start, 1 = 0% … 0 = 100%\nFn+Enter: keyboard ↔ MIDI; RAlt/RCtrl: octave −/+\nLCtrl/LAlt: pitch −/+; LWin: modulation\nSpace: sustain (CC64), uses key thresholds\nWheels: raw 3800 = 0%, 1000 = 100%\nMIDI channel 1; C4=60. Notes/Off configurable.\nPer-key, mapping and Fn-menu edits are RAM-only; only the saved calibration\nsurvives a power cycle, until Fn+R or `cfg clean`. Host JSON export includes\nMIDI mappings. Config edits release keys/notes and wait for neutral.',justify='left').pack(anchor='w')
        plot = ttk.Frame(lower); plot.pack(side='right',fill='both',expand=True)
        holdbar = ttk.Frame(plot); holdbar.pack(fill='x',pady=(0,4))
        self.hold_button = ttk.Checkbutton(holdbar,text=f'Hold first {CAPTURE_POINTS} pts of keystroke',
                                           variable=self.hold_mode,command=self.toggle_hold)
        self.hold_button.pack(side='left')
        self.hold_status = tk.StringVar(value='')
        ttk.Label(holdbar,textvariable=self.hold_status).pack(side='left',padx=8)
        self.graph = tk.Canvas(plot,height=200,bg='#17232d',highlightthickness=0)
        self.graph.pack(fill='both',expand=True)
        root.protocol('WM_DELETE_WINDOW',self.close)
        if demo: self.connect_button.configure(state='disabled')
        self.update()

    def draw(self):
        self.canvas.delete('all'); self.items.clear(); self.titles.clear()
        unit = max(50,(self.canvas.winfo_width()-4)/15)
        height = 52
        for key in self.keys:
            x,y = key.x*unit+2,key.y*height+2
            tag = f'key{key.sensor}'
            rect = self.canvas.create_rectangle(x,y,x+key.width*unit-4,y+height-5,
                                                fill='#21313e',outline='#354958',width=2,tags=tag)
            self.titles[key.sensor] = self.canvas.create_text(x+key.width*unit/2-2,y+12,text=key.label,fill='#f0f5f7',font=('sans',10,'bold'),tags=tag)
            text = self.canvas.create_text(x+key.width*unit/2-2,y+28,text='—',fill='#9cafbc',font=('monospace',10),tags=tag)
            velocity = self.canvas.create_text(x+key.width*unit/2-2,y+40,text='v —',fill='#80c8ce',font=('monospace',8),tags=tag)
            self.items[key.sensor] = rect,text,velocity
            self.canvas.tag_bind(tag,'<Button-1>',lambda _,i=key.sensor:self.select(i))
        self.paint()

    def select(self,index):
        self.selected = index; self.history.clear(); self.capture.reset()
        label = next(k.label for k in self.keys if k.sensor == index)
        self.key_title.set(f'{label}  /  sensor {index}')
        if self.snapshot and self.snapshot.count == 61:
            self.press.set(str(self.snapshot.press[index])); self.release.set(str(self.snapshot.release[index]))
            self.trigger_point.set(self.trigger_choice(self.snapshot.press[index]))
            self.midi_note.set(note_name(self.snapshot.midi_mapping[index]))

    @staticmethod
    def trigger_choice(press):
        """Nearest Fn+Tab level for an observed press threshold."""
        values = [TRIGGER_FLOOR + level*(TRIGGER_CEILING-TRIGGER_FLOOR)//9 for level in range(10)]
        nearest = min(range(10),key=lambda level:abs(values[level]-press))
        return TRIGGER_LEVELS[nearest]
        self.paint()

    def usable(self,allow_calibration=False):
        return bool(not self.demo and self.connection and self.connection.connected and self.snapshot and
                    self.connection.stream_mode == 'gui' and
                    (allow_calibration or not self.snapshot.calibration_flags & 1) and
                    self.snapshot.profile == 1 and self.snapshot.count == 61 and
                    self.connection.snapshot() and time.monotonic()-self.connection.snapshot()[0] < 1)

    def toggle_hold(self):
        self.capture.reset()

    def sync_hold_stream(self):
        """Engage/disengage the full-rate per-key stream to match hold mode."""
        connection = self.connection
        if self.demo or not connection or not connection.is_alive(): return
        calibration = bool(self.snapshot and self.snapshot.calibration_flags & 1)
        if self.hold_mode.get() and connection.connected and not calibration:
            if connection.stream_mode != 'key' or connection.key_sensor != self.selected:
                self.capture.reset()
                self.capture_rate = 0.0; self._rate_count = 0; self._rate_at = None
                threshold = self.snapshot.press[self.selected] if self.snapshot and self.snapshot.count == 61 else 3500
                connection.stream_key(threshold,self.selected)
        elif connection.stream_mode == 'key':
            connection.stream_gui()
            self.capture_rate = 0.0; self._rate_count = 0; self._rate_at = None

    def pump_key_samples(self,s):
        if not self.connection or s.count != 61: return
        samples = self.connection.drain_samples()
        if not samples: return
        press,release = s.press[self.selected],s.release[self.selected]
        for raw in samples: self.capture.feed_sample(raw,press,release)
        if self.capture.velocity is None and len(self.capture.points) >= 2:
            # Mirror the device fit: up to ten points from the trigger, cut
            # before the bottom-out sample (1500); median interval filter only
            # when more than five samples were collected.
            window = velocity_window(self.capture.points)
            if window is not None and len(window) >= 2:
                self.capture.velocity = press_velocity(tuple(window))
        self._rate_count += len(samples)
        if self._rate_at is None: self._rate_at = time.monotonic()
        elapsed = time.monotonic()-self._rate_at
        if elapsed >= .5:
            self.capture_rate = self._rate_count/elapsed
            self._rate_count = 0; self._rate_at = time.monotonic()

    def draw_axis(self,w,h):
        """Vertical raw-value axis (1..4096 scale) with ticks and gridlines."""
        self.graph.create_line(AXIS_W,15,AXIS_W,h-15,fill='#354958')
        for value in range(0,5000,1000):
            y = h-15-value/4096*(h-30)
            self.graph.create_line(AXIS_W-4,y,AXIS_W,y,fill='#9cafbc')
            self.graph.create_text(AXIS_W-7,y,anchor='e',text=str(value),fill='#9cafbc',font=('monospace',8))
            self.graph.create_line(AXIS_W,y,w,y,fill='#23303b',dash=(2,4))

    def paint_hold(self,w,h):
        cap = self.capture
        if self.key_capture:
            suffix = f' @ {self.capture_rate:,.0f} Hz' if self.capture_rate else ' @ full scan rate'
        else:
            suffix = ''
        if cap.armed:
            self.graph.create_text(w/2,h/2,
                text=f'Armed — press the selected key to hold its first {CAPTURE_POINTS} points{suffix}',
                fill='#9cafbc',font=('sans',11))
            self.hold_status.set('Armed — waiting for a keystroke trigger'+suffix)
            return
        span = CAPTURE_POINTS-1
        coords = [(AXIS_W+i/span*(w-AXIS_W-4),h-15-v/4096*(h-30)) for i,v in enumerate(cap.points)]
        if len(cap.points) > 1:
            self.graph.create_line(*[c for xy in coords for c in xy],fill='#e9f0f4',width=2)
        for i,(x,y) in enumerate(coords):
            self.graph.create_oval(x-2,y-2,x+2,y+2,fill='#f1a366' if i == 0 else '#e9f0f4',outline='')
        if cap.velocity is not None and len(cap.points) >= 2:
            # Fitted velocity line: a straight slant anchored at the trigger
            # point whose slope is the measured counts/s converted back to raw
            # counts per sample (assumed 8 kHz), spanning the fitted window.
            window = velocity_window(cap.points)
            fitted = len(window) if window is not None else min(VELOCITY_WINDOW,len(cap.points))
            if fitted >= 2:
                drop = cap.velocity/8000.0  # raw counts per sample
                x0,y0 = coords[0]
                x1 = AXIS_W+(fitted-1)/span*(w-AXIS_W-4)
                y1 = y0+drop*(fitted-1)/4096*(h-30)
                self.graph.create_line(x0,y0,x1,y1,fill='#7ee787',width=2,dash=(6,3))
        for i in range(0,CAPTURE_POINTS,5):
            self.graph.create_text(AXIS_W+i/span*(w-AXIS_W-4),h-3,text=str(i),fill='#9cafbc',font=('monospace',8))
        state = f'{"held" if cap.done else "capturing"} • {len(cap.points)}/{CAPTURE_POINTS} points{suffix}'
        if cap.velocity is not None:
            normalized = 0.0 if cap.velocity <= 0 else 1.0 if cap.velocity >= 4500000 else cap.velocity/4500000.0
            state += f' • velocity {normalized:.4f} [0–1] ({cap.velocity:,.0f} counts/s; assumed 8 kHz)'
        elif cap.fit:
            state += f' • device velocity {cap.fit[1]:.4f} [0–1] (fit {cap.fit[0]})'
        elif cap.done:
            state += ' • no velocity measured'
        self.hold_status.set(state)

    def paint(self,stale=False):
        s = self.snapshot
        for index,(rect,text,velocity) in self.items.items():
            valid = s and s.profile == 1 and s.count == 61 and not stale
            down = valid and s.down[index]
            fill = '#a95420' if down else '#21313e' if valid else '#26303a'
            if valid and s.calibration_flags & 1:
                fill = '#20683b' if s.calibration_done[index] else '#1d3963'
                if s.calibration_state in (1,2): fill = '#59316d'
                elif s.calibration_state == 3 and s.velocity_state[index] & 8: fill = '#a96d17'
            self.canvas.itemconfigure(rect,fill=fill,
                                      outline='#56d7db' if index == self.selected else '#354958')
            self.canvas.itemconfigure(text,text=str(s.raw[index]) if valid else '—')
            result = 'v —'
            if valid and s.velocity_state[index] & 2:
                result = f'v{s.velocity[index]:.3f}'
            self.canvas.itemconfigure(velocity,text=result)
        janko = bool(s and s.flags & FLAG_JANKO)
        for key in self.keys:
            label = key.label
            if s and s.count == 61:
                note = JANKO_NOTES.get(label) if janko else None
                suffix = MIDI_CONTROLS.get(label,note or note_name(s.midi_mapping[key.sensor]))
                label += '/'+suffix
            if key.sensor in self.titles: self.canvas.itemconfigure(self.titles[key.sensor],text=label)
        if s and s.count == 61:
            i = self.selected
            state = s.velocity_state[i]
            result = 'no completed fit'
            if state & 2: result = f'{s.velocity[i]:.6f} [0–1]'
            velocity = f'Velocity: {result}  (assumed 8 kHz)\nFits: {s.captures[i]}  |  '
            velocity += ('pending; ' if state & 4 else '') + ('armed' if state & 1 else 'waiting for release')
            self.details.set(f'Raw: {s.raw[i]}   Sensor: {"DOWN" if s.down[i] else "up"}\n'
                             f'On device: press {s.press[i]}, release {s.release[i]}\n'
                             f'Config revision: {s.revision}\n'
                             f'Velocity start: {s.velocity_start} ({(s.velocity_start-1)*100//9}%, Fn+V)\n'
                             f'{velocity}\n'
                             f'HID submitted: {s.report.hex()}'
                             f'\nMIDI base: {note_name(s.midi_mapping[i])} ({s.midi_mapping[i] if s.midi_mapping[i] != 255 else "unmapped"}); octave {s.octave:+d}')
        self.graph.delete('all')
        w,h = max(1,self.graph.winfo_width()),max(1,self.graph.winfo_height())
        self.draw_axis(w,h)
        if s and s.count == 61:
            for value,color,title in ((s.press[self.selected],'#f1a366','press'),(s.release[self.selected],'#56d7db','release')):
                y = h-15-value/4096*(h-30)
                self.graph.create_line(AXIS_W,y,w-2,y,fill=color,dash=(4,4))
                self.graph.create_text(w-6,y+10 if title == 'press' else y-10,anchor='e',text=f'{title} {value}',fill=color)
        if self.hold_mode.get():
            self.paint_hold(w,h)
        else:
            self.hold_status.set('')
            if len(self.history)>1:
                points = []
                for i,value in enumerate(self.history): points.extend((AXIS_W+i/(len(self.history)-1)*(w-AXIS_W-2),h-15-value/4096*(h-30)))
                self.graph.create_line(*points,fill='#e9f0f4',width=2)

    def detect(self):
        path = find_cdc_device()
        if path:
            self.device.set(path)
            self.message.set(f'Detected {path} (USB {USB_VENDOR_ID:04x}:{USB_PRODUCT_ID:04x}); press Connect.')
        else:
            self.device.set('')
            self.message.set(f'No USB {USB_VENDOR_ID:04x}:{USB_PRODUCT_ID:04x} CDC device detected; check the cable and udev permissions.')

    def toggle_connection(self):
        if self.connection and self.connection.is_alive():
            self.connection.stop(); self.message.set('Disconnecting…'); return
        path = self.device.get().strip()
        if not path or path.lower() == 'auto':
            path = find_cdc_device()
            if not path:
                messagebox.showerror('Detect',f'No USB {USB_VENDOR_ID:04x}:{USB_PRODUCT_ID:04x} CDC device detected.\n\n'
                    'Plug in the keyboard, wait for the CDC port, or enter a device node (e.g. /dev/ttyACM0) and connect again.')
                return
            self.device.set(path)
        self.connection = Connection(path)
        self.initial_fields = False; self.snapshot = None; self.last_sequence = None
        self.connection.start(); self.message.set(f'Connecting to {path}; requesting GUI stream and acknowledged readback…')

    def calibrate(self):
        if not self.usable() or self.snapshot.performance_mode: return
        if not messagebox.askyesno('Calibrate all keys',
            'Keyboard output pauses. Release ALL keys; wait for blue. Fully press and hold blue keys for one second until green. You may hold multiple keys together; each key has an independent timer. Include Fn and modifiers.\n\n'
            'Five seconds of inactivity discards the attempt. Completing all keys saves calibration in two dedicated pages (0x78000 / 0x78200) inside the original free block, preserving the serial-number area. Continue?'): return
        try:
            self.connection.submit('calibrate')
            self.message.set('Calibration requested; ACK starts the routine, not a flash save. Watch progress below.')
        except queue.Full: messagebox.showerror('Busy','Configuration queue is full.')

    def cancel_calibration(self):
        if not self.usable(allow_calibration=True): return
        try: self.connection.submit('calcancel')
        except queue.Full: messagebox.showerror('Busy','Configuration queue is full.')

    def apply(self):
        try:
            pair = validate_pair(int(self.press.get()),int(self.release.get()))
            if not self.usable(): raise ValueError('Connect to a fresh ANSI keyboard snapshot first.')
            self.connection.submit('set',self.selected,*pair)
            self.message.set('Threshold change queued; waiting for device ACK/readback…')
        except (ValueError,queue.Full) as error: messagebox.showerror('Thresholds',str(error))

    def enable(self,enabled):
        if self.usable():
            try: self.connection.submit('enable',int(enabled))
            except queue.Full: messagebox.showerror('Busy','Configuration queue is full.')

    def apply_midi(self):
        try:
            if not self.usable(): raise ValueError('Connect to a keyboard snapshot first.')
            label = next(k.label for k in self.keys if k.sensor == self.selected)
            if label in MIDI_CONTROLS: raise ValueError('This key is a reserved MIDI mode/octave/wheel/sustain control.')
            self.connection.submit('midi',self.selected,parse_note(self.midi_note.get()))
            self.message.set('MIDI mapping queued; waiting for device ACK/readback…')
        except (ValueError,queue.Full) as error: messagebox.showerror('MIDI mapping',str(error))

    def apply_trigger_point(self):
        """All keys adopt one MIDI trigger point; per-key releases are kept."""
        try:
            if not self.usable(): raise ValueError('Connect to a keyboard snapshot first.')
            if self.snapshot.performance_mode != 1:
                raise ValueError('The MIDI trigger point page (Fn+Tab) applies in MIDI mode only.')
            level = TRIGGER_LEVELS.index(self.trigger_point.get())+1
            press = TRIGGER_FLOOR + (level-1)*(TRIGGER_CEILING-TRIGGER_FLOOR)//9
            if not messagebox.askyesno('Trigger point',
                f'Set the MIDI trigger point to level {level} (raw press < {press}) for all 61 keys?\n'
                'Each key keeps its release threshold; press < release is enforced.'):
                return
            if not self.connection.requests.empty(): raise ValueError('Wait for queued changes to finish first.')
            for index in range(61):
                release = self.snapshot.release[index]
                self.connection.submit('set',index,min(press,release-1),release)
            self.message.set('Trigger point queued for all keys with per-key readback; a failure cancels remaining changes.')
        except (ValueError,queue.Full) as error: messagebox.showerror('Trigger point',str(error))

    def apply_velocity_start(self):
        """Fn+V equivalent: the transmitted-velocity starting point."""
        try:
            if not self.usable(): raise ValueError('Connect to a keyboard snapshot first.')
            level = VELOCITY_STARTS.index(self.velocity_start.get())+1
            self.connection.submit('velocity',level)
            self.message.set('Velocity start queued; waiting for device ACK/readback…')
        except (ValueError,queue.Full) as error: messagebox.showerror('Velocity start',str(error))

    def browse_firmware(self):
        path = filedialog.askopenfilename(title='Application image',
                                          filetypes=[('Firmware image','*.bin'),('All files','*')])
        if path: self.flash_image.set(path)

    def flash_confirm(self,size,digest):
        return messagebox.askyesno('Flash application',(
            f'Write this image to the keyboard?\n\n{self.flash_image.get()}\n'
            f'{size} bytes\nsha256 {digest}\n\n'
            'Application region only: bootloader, factory/security data, primary settings and '
            'serial-number storage are never written.\n'
            'The device is cleared afterwards (cold boot): the saved calibration is '
            'erased and it returns to defaults.\n\n'
            'Keep the keyboard connected until the progress bar finishes. Continue?'))

    def flash_firmware(self):
        """Flash the chosen image in a worker thread; never implicitly."""
        if self.flashing: messagebox.showwarning('Flash application','A flash is already running.'); return
        image = self.flash_image.get().strip()
        try:
            size,digest = firmware_flasher.image_digest(image)
            if not firmware_flasher.updater_available():
                raise firmware_flasher.FlasherError('the updater submodule is missing; run: '
                                                    + firmware_flasher.INIT_HINT)
        except firmware_flasher.FlasherError as error:
            messagebox.showerror('Flash application',str(error)); return
        elevated = hasattr(os,'geteuid') and os.geteuid() != 0
        if elevated and not shutil.which('pkexec'):
            messagebox.showerror('Flash application',
                'Flashing needs raw USB access. Run this GUI with sudo, or install pkexec '
                '(PolicyKit) so it can elevate just the flash.')
            return
        if not self.flash_confirm(size,digest): return
        if self.connection and self.connection.is_alive():
            self.message.set('Disconnecting to release the device for flashing…')
            self.connection.stop(); self.connection.join(timeout=1.5)
        self.flashing = True
        self.flash_button.configure(state='disabled')
        self.flash_progress.configure(value=0,maximum=100)
        self.flash_status.set('Preparing flash…')
        threading.Thread(target=self.flash_worker,args=(image,elevated),daemon=True).start()

    def flash_worker(self,image,elevated):
        """Thread: flash, then report progress and outcome through the queue."""
        def post(kind,payload): self.flash_queue.put((kind,payload))
        def progress(done,total): post('progress',(done,total))
        def status(text): post('status',str(text))
        try:
            if elevated:
                script = Path(firmware_flasher.__file__).with_name('flash_application.py')
                command = ['pkexec',sys.executable,str(script),image]
                status('running the elevated flasher (PolicyKit may ask for your password)')
                process = subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                           text=True,bufsize=1)
                for line in process.stdout:
                    found = re.search(r'program (\d+)/(\d+)',line)
                    if found: progress(int(found.group(1)),int(found.group(2)))
                    elif line.strip(): status(line.strip())
                if process.wait() != 0:
                    raise firmware_flasher.FlasherError('the elevated flasher failed; see the status text')
            else:
                result = firmware_flasher.flash_image(image,progress=progress,status=status)
                status('flash complete, sha256 '+result.digest)
            post('done',None)
        except Exception as error:  # noqa: BLE001 - reported to the user verbatim
            post('error',str(error))

    def poll_flash(self):
        """Drain worker messages on the Tk thread."""
        while True:
            try: kind,payload = self.flash_queue.get_nowait()
            except queue.Empty: return
            if kind == 'progress':
                done,total = payload
                self.flash_progress.configure(maximum=total or 1,value=done)
                self.flash_status.set(f'Programming {done}/{total} ({done*100//(total or 1)}%)…')
            elif kind == 'status': self.flash_status.set(payload)
            elif kind == 'error':
                self.flashing = False; self.flash_button.configure(state='normal')
                self.flash_status.set('Flash failed: '+payload)
                messagebox.showerror('Flash application',payload)
            else:
                self.flashing = False; self.flash_button.configure(state='normal')
                self.flash_progress.configure(value=self.flash_progress['maximum'])
                self.flash_status.set('Flash complete; the device is at its defaults. Press Connect to reconnect.')
                messagebox.showinfo('Flash application','Application flashed and the device cold-booted.\n\n'
                                    'Press Connect to resume telemetry.')

    def apply_all(self):
        try:
            pair = validate_pair(int(self.press.get()),int(self.release.get()))
            if not self.usable(): raise ValueError('Connect to a keyboard snapshot first.')
            if not messagebox.askyesno('Apply to all keys',f'Set all 61 keys to press {pair[0]}, release {pair[1]}?\nThis releases held keys and waits for neutral.'):
                return
            self.connection.submit('all',*pair)
            self.message.set('All-key thresholds queued; waiting for ACK and all 61 readbacks…')
        except (ValueError,queue.Full) as error: messagebox.showerror('Thresholds',str(error))

    def save_profile(self):
        try:
            if not self.snapshot: raise ValueError('No device configuration to save.')
            profile = profile_from_snapshot(self.snapshot)
            path = filedialog.asksaveasfilename(defaultextension='.json',filetypes=[('Keyboard profile','*.json')])
            if path:
                Path(path).write_text(json.dumps(profile,indent=2)+'\n')
                self.message.set('Saved device-confirmed thresholds to '+path)
        except (ValueError,OSError) as error: messagebox.showerror('Save profile',str(error))

    def load_profile(self):
        if not self.usable(): return
        path = filedialog.askopenfilename(filetypes=[('Keyboard profile','*.json')])
        if not path: return
        try:
            profile = json.loads(Path(path).read_text())
            values = validate_profile(profile)
            if not messagebox.askyesno('Apply profile','Temporarily disable keyboard output and apply all 61 keys? Settings are RAM-only.'):
                return
            enabled = bool(self.snapshot.flags & 1)
            if not self.connection.requests.empty(): raise ValueError('Wait for queued changes to finish first.')
            self.connection.submit('enable',0)
            for index,pair in sorted(values.items()): self.connection.submit('set',index,*pair)
            if profile['version'] == 2:
                for key in profile['keys']:
                    if key['label'] not in MIDI_CONTROLS: self.connection.submit('midi',key['sensor'],key['midi'])
            self.connection.submit('enable',int(enabled))
            self.message.set('Applying profile with per-key readback; a failure cancels remaining changes.')
        except (ValueError,OSError,queue.Full) as error: messagebox.showerror('Load profile',str(error))

    def update(self):
        stale = True
        if self.demo:
            raw = [3900]*61
            raw[32] = int(3900-2600*(.5+.5*math.sin(time.monotonic()*2)))
            mapping = tuple(255 if k.label in MIDI_CONTROLS else 60+k.sensor for k in self.keys)
            self.snapshot = Snapshot(1,61,7,1,int(time.monotonic()*30),0,0,0,0,
                                     tuple(raw),(3500,)*61,(3600,)*61,tuple(v<3500 for v in raw),bytes(16),
                                     midi_mapping=mapping,
                                     velocity=tuple(0.5 if i == 32 else 0.0 for i in range(61)),
                                     captures=tuple(1 if i == 32 else 0 for i in range(61)),
                                     velocity_state=tuple(2 if i == 32 else 1 for i in range(61)))
            stale = False
        elif self.connection:
            latest = self.connection.snapshot()
            if latest:
                self.snapshot = latest[1]; stale = time.monotonic()-latest[0]>1 or not self.connection.connected
            while True:
                try: self.message.set(self.connection.events.get_nowait())
                except queue.Empty: break
            if self.connection.build != self.device_build:
                self.device_build = self.connection.build
                if self.device_build:
                    target = self.connection.build_target
                    name = KNOWN_TARGETS.get(target)
                    self.message.set(f'Device build {self.device_build}' +
                                     (f' ({name})' if name else f' (unrecognized build target {target})'))
            self.connect_button.configure(text='Disconnect' if self.connection.is_alive() else 'Connect')
        self.poll_flash()
        s = self.snapshot
        self.sync_hold_stream()
        self.key_capture = not self.demo and bool(self.connection and self.connection.is_alive() and self.connection.stream_mode == 'key')
        if s:
            if s.count == 61 and not self.initial_fields:
                self.select(self.selected); self.initial_fields = True
            self.velocity_start.set(VELOCITY_STARTS[s.velocity_start-1])
            if self.hold_mode.get() and not self.demo:
                self.pump_key_samples(s)
            if s.sequence != self.last_sequence and s.count == 61 and not stale:
                self.last_sequence = s.sequence
                if self.hold_mode.get() and self.demo:
                    captures = s.captures[self.selected]
                    velocity = s.velocity[self.selected]
                    fit_valid = bool(s.velocity_state[self.selected] & 2)
                    self.capture.feed(s.raw[self.selected],s.down[self.selected],captures,velocity,fit_valid)
                elif not self.hold_mode.get():
                    self.history.append(s.raw[self.selected])
            state = 'STALE / disconnected' if stale else 'REPORTING' if s.flags & 2 else 'Waiting for all keys released' if s.flags & 1 else 'Keyboard disabled'
            if self.key_capture:
                rate = f' {self.capture_rate:,.0f} samples/s' if self.capture_rate else ''
                state = f'KEYSTROKE CAPTURE{rate} • press the selected key (telemetry paused)'
            if not stale and s.mode: state = f'Legacy FN editor {s.mode} — Escape to exit; use GUI for raw thresholds'
            state += f' | {"MIDI" if s.performance_mode else "KEYBOARD"} | octave {s.octave:+d} | MIDI errors={s.midi_errors}'
            if s.flags & FLAG_JANKO: state += ' | JANKÓ layout (Fn+J)'
            if s.midi_cleanup: state += ' | MIDI note cleanup pending'
            if s.profile not in (0,1): state = 'Unsupported graphical layout (ANSI only)'
            self.status.set(f'{"DEMO • " if self.demo else ""}{state}  |  {s.count} sensors  |  valid={bool(s.flags & 4)}  |  '
                            f'scan errors={s.scan_errors}  LED errors={s.light_errors}  |  FN={bool(s.flags & 32)}  |  config revision={s.revision}'
                            + (f'  |  build {self.device_build}' if self.device_build else ''))
        self.trigger_button.configure(state='normal' if self.usable() else 'disabled')
        self.velocity_button.configure(state='normal' if self.usable() else 'disabled')
        for button in (self.enable_button,self.disable_button,self.apply_button,self.load_button):
            button.configure(state='normal' if self.usable() else 'disabled')
        self.apply_all_button.configure(state='normal' if self.usable() else 'disabled')
        midi_usable = self.usable() and next(k.label for k in self.keys if k.sensor == self.selected) not in MIDI_CONTROLS
        self.midi_button.configure(state='normal' if midi_usable else 'disabled')
        supported = self.usable(allow_calibration=True) and bool(s.calibration_flags & 4)
        active = supported and bool(s.calibration_flags & 1)
        self.calibrate_button.configure(state='normal' if supported and not active and not s.performance_mode else 'disabled')
        self.cancel_calibration_button.configure(state='normal' if active else 'disabled')
        if s:
            names = ('Idle','Release all keys','Settling: keep all keys released','Fully hold blue keys for 1 s (parallel)',
                     'Registered: release the green key','Saving','Complete: saved to device','Aborted: discarded','Save failed: previous calibration retained')
            reasons = ('','inactivity timeout','invalid/stale scan or USB reset','cancelled','storage failure')
            label = next((k.label for k in self.keys if k.sensor == s.calibration_selected),'—')
            holding = sum(bool(v & 8) for v in s.velocity_state)
            self.calibration_status.set(f'{"STALE • " if stale else ""}Calibration: {names[s.calibration_state]} | '
                f'{s.calibration_completed}/{s.count} | holding {holding} | key {label}, hold {s.calibration_hold}/1000 ms | idle limit {s.calibration_idle/1000:.1f} s | '
                f'flash generation {s.calibration_generation} ({"saved" if s.calibration_flags & 2 else "factory bounds"})'
                + (f' | {reasons[s.calibration_reason]}' if s.calibration_reason else '')
                + (f' | storage error 0x{s.calibration_error:x}' if s.calibration_error else ''))
        self.paint(stale)
        self.after_id = self.root.after(33,self.update)

    def close(self):
        if self.flashing:
            messagebox.showwarning('Flash application',
                'A flash is still running. Wait for it to finish before closing.')
            return
        self.root.after_cancel(self.after_id)
        if self.connection: self.connection.stop(); self.connection.join(timeout=.3)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',default='auto',
                        help=f"CDC device node, or 'auto' (default) to detect USB {USB_VENDOR_ID:04x}:{USB_PRODUCT_ID:04x}")
    parser.add_argument('--demo',action='store_true',help='visual demo only; never opens a device')
    args = parser.parse_args()
    device = '' if args.demo else (find_cdc_device() if args.device == 'auto' else args.device)
    root = tk.Tk(); App(root,device,args.demo); root.mainloop()


if __name__ == '__main__': main()
