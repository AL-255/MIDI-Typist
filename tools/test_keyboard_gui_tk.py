#!/usr/bin/env python3
"""Optional real Tk UI smoke test under a private Xvfb display, no device access."""
import argparse
import os
from keyboard_gui_transport import Connection
import select
import subprocess
import time
import tkinter as tk
from unittest.mock import patch
from types import SimpleNamespace
from keyboard_gui import App, AXIS_W, TRIGGER_LEVELS, VELOCITY_STARTS, TRIGGER_FLOOR
from keyboard_gui_model import CAPTURE_POINTS
from test_keyboard_gui import Device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--screenshot',help='optional PNG screenshot (requires Pillow)')
    args = parser.parse_args()
    read_fd,write_fd = os.pipe()
    server = subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1280x900x24','-nolisten','tcp'],
                              pass_fds=(write_fd,),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    os.close(write_fd)
    root = None
    try:
        if not select.select([read_fd],[],[],5)[0]: raise RuntimeError('Xvfb did not start')
        display = os.read(read_fd,32).decode().strip()
        if not display.isdecimal(): raise RuntimeError('Xvfb startup failed: '+server.stderr.read().decode())
        os.environ['DISPLAY'] = ':'+display
        root = tk.Tk(); app = App(root,demo=True)
        root.update()
        # The flashing tab is isolated from the configuration canvas. Demo
        # construction and selection never discover or open a real device.
        from flash_models import ConnectedDevice, FirmwareImage
        tab=app.flash_tab
        assert len(app.notebook.tabs())==2
        assert list(tab.model_picker['values'])==['Razer Huntsman Pro Mini V3',
                                                'MonsGeek M1 V5 TMR (experimental)']
        with patch.object(tab.adapter,'discover',side_effect=AssertionError('demo accessed USB')):
            app.notebook.select(tab);root.update()
        assert str(tab.flash_button['state'])=='disabled'
        app.demo=False
        device=ConnectedDevice(tab.adapter.id,'1-2','confirmed-token','custom','Test keyboard',
                               'TEST-SERIAL','v0.1.0','1532:02b0','480 Mb/s')
        tab.show_device(device);tab.set_options()
        assert [w['text'] for w in tab.action_widgets]==['Reflash MIDI-Typist','Restore Razer firmware']
        assert tab.identity['Serial number'].get()=='TEST-SERIAL'
        tab.image=FirmwareImage('unused.bin',bytes(131072),'a'*64,'custom','Test image')
        tab.confirm_model.set(True);tab.sync()
        with patch('keyboard_flash_tab.messagebox.askyesno',return_value=True),patch.object(tab,'launch_worker') as launch:
            tab.flash()
            assert launch.call_args.args[0]=='flash'
            assert launch.call_args.args[1].digest=='a'*64
            assert launch.call_args.args[2].id=='reflash'
        tab.action.set('restore');tab.choose_action()
        assert tab.image is None and not tab.confirm_model.get() and tab.path.get()==''
        for mode in ('bootloader','razer'):
            from dataclasses import replace
            tab.show_device(replace(device,mode=mode));tab.set_options()
            assert [w['text'] for w in tab.action_widgets]==['Install MIDI-Typist','Restore Razer firmware']
        huntsman_name=tab.model.get()
        tab.model.set('MonsGeek M1 V5 TMR (experimental)')
        for mode in ('candidate','factory','custom_candidate','custom','unverified_bootloader'):
            tab.show_device(replace(device,model=tab.adapter.id,mode=mode,version='v4.08'))
            tab.set_options()
            if mode=='factory':
                assert [w['text'] for w in tab.action_widgets]==[
                    'Install experimental MIDI-Typist','Install MonsGeek factory application']
            elif mode=='custom':
                assert [w['text'] for w in tab.action_widgets]==[
                    'Reflash experimental MIDI-Typist','Install MonsGeek factory application']
            else:assert not tab.action_widgets and tab.option is None
            assert str(tab.flash_button['state'])=='disabled'
            assert str(tab.inspect_button['state'])==('disabled' if mode=='unverified_bootloader' else 'normal')
            assert tab.identity['Firmware'].get()=='v4.08'
        tab.show_device(replace(device,model=tab.adapter.id,mode='custom_candidate'))
        with patch.object(tab,'close_configuration',return_value=False),patch.object(tab,'launch_worker') as launch:
            tab.inspect();launch.assert_not_called()
        with patch.object(tab,'close_configuration',return_value=True),patch.object(tab,'launch_worker') as launch:
            tab.inspect();launch.assert_called_once_with('inspect')
        tab.model.set(huntsman_name);tab.device=None;tab.set_options()
        app.demo=True;app.notebook.select(0);root.update()
        assert len(app.items) == 61 and len(app.canvas.find_all()) == 244
        assert app.usable() is False
        assert str(app.apply_button['state']) == 'disabled'
        assert '0.500000 [0–1]' in app.details.get()
        assert app.canvas.itemcget(app.items[32][2],'text') == 'v0.500'
        assert str(app.connect_button['state']) == 'disabled'
        graph_items = [item for item in app.graph.find_all() if app.graph.type(item) == 'text']
        graph_texts = [app.graph.itemcget(item,'text') for item in graph_items]
        # The raw-value axis labels whatever step fits the plot height: the
        # scale always starts at 0, reaches the top of the range, and no two
        # labels are closer than one text row (bitmap faces are taller).
        rows = {tick:int(app.graph.coords(item)[1]) for item,tick in zip(graph_items,graph_texts) if tick.isdigit()}
        ticks = sorted(int(tick) for tick in rows)
        assert ticks[0] == 0 and ticks[-1] >= 3000 and len(ticks) >= 3, ticks
        positions = sorted(rows[str(tick)] for tick in ticks)
        assert all(b-a >= app.fonts.linespace(9) for a,b in zip(positions,positions[1:])), positions
        assert 'press 3500' in graph_texts and 'release 3600' in graph_texts
        for key in app.keys:
            rect,_,_ = app.items[key.sensor]
            x1,y1,x2,y2 = app.canvas.coords(rect)
            assert 0 <= x1 < x2 <= app.canvas.winfo_width()
            assert 0 <= y1 < y2 <= app.canvas.winfo_height()
        key = next(k for k in app.keys if k.label == 'Tab')
        rect,_,_ = app.items[key.sensor]
        x1,y1,x2,y2 = app.canvas.coords(rect)
        app.canvas.event_generate('<Motion>',x=int((x1+x2)/2),y=int((y1+y2)/2))
        app.canvas.event_generate('<Button-1>',x=int((x1+x2)/2),y=int((y1+y2)/2))
        root.update()
        assert app.selected == key.sensor and app.press.get() == '3500'
        app.select(32); root.update()
        with patch('keyboard_gui.find_midi_device',return_value='/dev/fake'):
            app.detect()
        assert app.device.get() == '/dev/fake' and 'Detected' in app.message.get()
        with patch('keyboard_gui.find_midi_device',return_value=None):
            app.detect()
            assert app.device.get() == '' and 'No unique MIDI-Typist control port' in app.message.get()
        app.hold_button.invoke()
        assert app.hold_mode.get() and app.capture.armed
        root.update()
        app.hold_button.invoke()
        assert not app.hold_mode.get()
        root.update()
        if args.screenshot:
            from PIL import ImageGrab
            ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(args.screenshot)
        root.geometry('900x700'); root.update()
        assert len(app.items) == 61
        assert app.footer.winfo_rooty()+app.footer.winfo_height() < root.winfo_height()
        assert int(app.status_label.cget('wraplength')) <= root.winfo_width()  # status follows the window
        # The bottom-left settings region scrolls instead of clipping: its
        # fields, buttons and shortcut reference stay reachable in a small
        # window, and the scrollbar is only shown while the content overflows.
        area = app.panel_area
        assert area.overflowing() and area.showing_scrollbar()
        # At the minimum window height the panel viewport is short, so every
        # field, button and the shortcut reference must be scrollable into it.
        for target in (app.details_label,app.apply_button,app.help_label):
            area.reveal(target); root.update()
            assert area.visible(target),target
        area.scroll_units(1); root.update()
        assert area.canvas.yview()[0] > 0.0
        root.geometry('1180x920'); root.update()
        assert area.overflowing() and area.showing_scrollbar()  # still taller than the row
        area.canvas.yview_moveto(0.0); root.update()
        assert area.visible(app.details_label) and not area.visible(app.help_label)
        area.canvas.yview_moveto(1.0); root.update()
        assert area.visible(app.help_label)
        # Typography: no silent fallback to Tk's `fixed` bitmap font, and the
        # canvas uses the resolved families rather than a hard-coded one.
        from tkinter import font as tkfont
        import gui_fonts
        assert app.fonts.sans and app.fonts.mono
        for family in (app.fonts.sans,app.fonts.mono):
            assert tkfont.Font(family=family,size=10).actual()['family'].lower() == family.lower(), family
        assert gui_fonts.pick(('No Such Family 12345',),10) is None
        # On a Tk without fontconfig the widgets must use native bitmap pixel
        # sizes (negative) from an X11 face, never scaled point sizes.
        if not app.fonts.antialiased:
            assert app.fonts.sans in gui_fonts.X11_SANS_FAMILIES, app.fonts.sans
            assert int(app.canvas.itemcget(app.titles[32],'font').split()[-2]) < 0
        assert tkfont.Font(font=app.canvas.itemcget(app.titles[32],'font')).actual()['family'].lower() == app.fonts.sans.lower()
        assert tkfont.Font(font=app.canvas.itemcget(app.items[32][2],'font')).actual()['family'].lower() == app.fonts.mono.lower()
        app.close(); root = None
        print('PASS Tk: 61-key physical geometry, click-to-select, threshold fields, disabled demo controls, '
              'scrollable settings panel, resolved typography, resize')
        from keyboard_boards import M1_TARGET, DEFAULT_TARGET
        root=tk.Tk(); app=App(root,demo=True,board_target=M1_TARGET);root.update()
        assert app.board.count==82 and len(app.items)==82 and app.snapshot.count==82
        assert len(app.canvas.find_all())==328
        for width,height in ((1180,920),(900,700)):
            root.geometry(f'{width}x{height}');root.update()
            for key in app.keys:
                x1,y1,x2,y2=app.canvas.coords(app.items[key.sensor][0])
                assert 0 <= x1 < x2 <= app.canvas.winfo_width(),key
                assert 0 <= y1 < y2 <= app.canvas.winfo_height(),key
                app.select(key.sensor)
                assert key.label in app.key_title.get()
                assert app.press.get()=='3500'
            assert str(app.apply_button['state'])=='disabled'
        app.select(81);root.update()
        assert 'Right' in app.key_title.get() and 'Sensor:' in app.details.get()
        app.set_board(DEFAULT_TARGET);root.update()
        assert len(app.items)==61 and app.board.count==61
        app.set_board(M1_TARGET);root.update()
        assert len(app.items)==82 and app.selected==45
        app.close();root=None
        print('PASS Tk M1: 82 keys, six physical rows, all selections, resize, target switching; no device access')
        device = Device(board_target=M1_TARGET); device.start()
        try:
            with patch('keyboard_gui.Connection',side_effect=lambda name: Connection(name,backend_factory=lambda _:device)):
                root=tk.Tk();app=App(root,device='Fake M1 Control')
                app.toggle_connection()
                def m1_until(predicate,seconds=5):
                    deadline=time.monotonic()+seconds
                    while time.monotonic()<deadline:
                        root.update()
                        if predicate(): return
                        time.sleep(.01)
                    raise AssertionError('M1 GUI condition timed out')
                m1_until(app.usable)
                assert app.board.target==M1_TARGET and len(app.items)==82
                m1_until(lambda:'87% (estimate)' in app.power_status.get())
                assert 'polarity unverified' in app.power_status.get()
                with app.connection.lock:
                    power=app.connection.latest_power
                    app.connection.latest_power=(time.monotonic()-10,power[1])
                app.update();assert 'stale' in app.power_status.get()
                with app.connection.lock:app.connection.latest_power=power
                app.select(81);app.press.set('2500');app.release.set('2800')
                app.apply_button.invoke()
                m1_until(lambda:app.snapshot.press[81]==2500 and app.snapshot.release[81]==2800)
                assert 'Right' in app.key_title.get()
                app.midi_note.set('C4');app.midi_button.invoke()
                m1_until(lambda:app.snapshot.midi_mapping[81]==60)
                from keyboard_keycodes import keycode_name
                app.keyboard_code.set(keycode_name(0x87));app.keyboard_button.invoke()
                m1_until(lambda:app.snapshot.keyboard_mapping[81]==0x87)
                app.select(77);app.update()
                assert str(app.keyboard_button['state'])=='disabled'
                assert str(app.keyboard_entry['state'])=='disabled'
                app.select(81);app.update()
                # Entire board-sized profiles include all three per-key
                # settings. Verify the actual UI queues a complete batch.
                import json
                from keyboard_gui_model import profile_from_snapshot
                profile=profile_from_snapshot(app.snapshot,M1_TARGET)
                queued=[]
                with patch('keyboard_gui.filedialog.askopenfilename',return_value='mock.json'), \
                     patch('keyboard_gui.Path.read_text',return_value=json.dumps(profile)), \
                     patch('keyboard_gui.messagebox.askyesno',return_value=True), \
                     patch.object(app.connection,'submit',side_effect=lambda *args:queued.append(args)):
                    app.load_profile()
                assert queued[0]==('enable',0) and queued[-1]==('enable',1)
                assert len([q for q in queued if q[0]=='key'])==81
                assert ('key',81,0x87) in queued
                assert not any(q[:2]==('key',77) for q in queued)
                assert len(queued)<app.connection.requests.maxsize
                with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                    app.apply_all_button.invoke()
                m1_until(lambda:app.snapshot.press==(2500,)*82 and app.snapshot.release==(2800,)*82)
                app.hold_button.invoke()
                m1_until(lambda:app.connection.stream_mode=='key' and app.capture.armed)
                m1_until(lambda:'paused' in app.power_status.get())
                assert app.connection.key_sensor==81
                device.key_raw=2000
                m1_until(lambda:app.capture.done)
                assert len(app.capture.points)==CAPTURE_POINTS
                assert set(app.capture.points)=={2000}
                app.hold_button.invoke()
                m1_until(lambda:app.connection.stream_mode=='gui' and app.usable())
                m1_until(lambda:str(app.calibrate_button['state'])=='normal')
                with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                    app.calibrate_button.invoke()
                m1_until(lambda:app.snapshot.calibration_state==3)
                assert '0/82' in app.calibration_status.get()
                app.cancel_calibration_button.invoke()
                m1_until(lambda:app.snapshot.calibration_state==7)
                device.calibration_state=8;device.calibration_flags=6;device.storage_flags=4
                m1_until(lambda:app.snapshot.storage_flags==4 and 'Save failed: check storage status' in app.calibration_status.get())
                assert str(app.calibrate_button['state'])=='disabled'
                assert 'settings SAVE FAILED' in app.calibration_status.get()
                device.storage_flags=0
                device.calibration_state=0;device.calibration_flags=2
                m1_until(lambda:app.snapshot.calibration_flags==2 and 'read-only' in app.calibration_status.get())
                assert str(app.calibrate_button['state'])=='disabled'
                assert str(app.cancel_calibration_button['state'])=='disabled'
                assert 'settings not confirmed saved' in app.calibration_status.get()
                assert 'flash generation' not in app.calibration_status.get()
                device.calibration_flags=0
                m1_until(lambda:'Calibration: unavailable' in app.calibration_status.get())
                app.close();root=None
                assert device.error is None
        finally:
            device.stop_event.set();device.join(1)
        print('PASS Tk M1+SysEx mock: identity-selected geometry, sensor 81 edits/capture, all 82 thresholds, calibration')
        device = Device(); device.start()
        transport_patch = patch('keyboard_gui.Connection', side_effect=lambda name: Connection(name, backend_factory=lambda _:device))
        transport_patch.start()
        try:
            root = tk.Tk(); app = App(root,device='Fake Control')
            app.toggle_connection()
            def pump_until(predicate,seconds=3):
                deadline = time.monotonic()+seconds
                while time.monotonic() < deadline:
                    root.update()
                    if predicate(): return
                    time.sleep(.01)
                raise AssertionError('GUI condition timed out')
            pump_until(app.usable)
            assert str(app.calibrate_button['state']) == 'normal'
            with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                app.calibrate_button.invoke()
            pump_until(lambda:app.snapshot.calibration_state==3)
            assert not app.usable() and app.usable(allow_calibration=True)
            assert str(app.apply_button['state'])=='disabled'
            assert '0/61' in app.calibration_status.get()
            assert 'holding 2' in app.calibration_status.get()
            assert all(app.canvas.itemcget(app.items[i][0],'fill')=='#a96d17' for i in (0,1))
            app.cancel_calibration_button.invoke()
            pump_until(lambda:app.snapshot.calibration_state==7)
            assert app.usable()
            app.select(32); app.press.set('3000'); app.release.set('3250')
            app.apply_button.invoke()
            pump_until(lambda:app.snapshot.press[32] == 3000 and app.snapshot.release[32] == 3250)
            assert 'press 3000, release 3250' in app.details.get()
            with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                app.apply_all_button.invoke()
            pump_until(lambda:app.snapshot.press == (3000,)*61 and app.snapshot.release == (3250,)*61)
            app.midi_note.set('C4'); app.midi_button.invoke()
            pump_until(lambda:app.snapshot.midi_mapping[32] == 60)
            assert 'C4 (60)' in app.details.get()
            for label in ('Fn','Spc'):
                app.select(next(k.sensor for k in app.keys if k.label == label))
                root.update(); pump_until(lambda:str(app.midi_button['state']) == 'disabled')
                assert str(app.midi_button['state']) == 'disabled'
            # Fn+Tab / Fn+V equivalents: the GUI edits the same two settings.
            device.performance_mode = 1
            pump_until(lambda:app.snapshot.performance_mode == 1)
            app.select(32); root.update()
            assert app.trigger_point.get() == app.trigger_choice(3000)  # echoed from the device
            app.trigger_point.set(TRIGGER_LEVELS[0])                    # level 1 = bottom-out floor
            with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                app.trigger_button.invoke()
            pump_until(lambda:app.snapshot.press == (TRIGGER_FLOOR,)*61,12)
            assert app.snapshot.release == (3250,)*61  # per-key releases preserved
            assert app.trigger_point.get() == TRIGGER_LEVELS[0]
            app.velocity_start.set(VELOCITY_STARTS[9])  # 100%: every note at full velocity
            app.velocity_button.invoke()
            pump_until(lambda:app.snapshot.velocity_start == 10 and 'Velocity start: 10 (100%' in app.details.get(),6)
            device.performance_mode = 0
            pump_until(lambda:app.snapshot.performance_mode == 0)
            # Restore the pair the hold-mode fixture below expects.
            app.press.set('3000'); app.release.set('3250')
            with patch('keyboard_gui.messagebox.askyesno',return_value=True):
                app.apply_all_button.invoke()
            pump_until(lambda:app.snapshot.press == (3000,)*61 and app.snapshot.release == (3250,)*61,8)
            app.select(32); root.update()
            app.hold_button.invoke()
            pump_until(lambda:app.hold_mode.get() and app.key_capture and app.connection.stream_mode == 'key')
            assert app.connection.key_sensor == 32 and app.capture.armed
            assert 'Armed' in app.hold_status.get() and 'full scan rate' in app.hold_status.get()
            assert 'KEYSTROKE CAPTURE' in app.status.get()
            pump_until(lambda:'samples/s' in app.status.get(),3)  # measured rate appears
            history_len = len(app.history)
            device.key_raw = 2500  # below press 3000 → Schmitt down edge at 8 ksps
            pump_until(lambda:app.capture.done,4)
            assert len(app.capture.points) == CAPTURE_POINTS
            assert app.capture.points[0] == 2500 and set(app.capture.points) == {2500}
            assert 'held' in app.hold_status.get() and '0.0000 [0–1]' in app.hold_status.get()
            points = list(app.capture.points)
            root.update(); time.sleep(.05); root.update()
            assert app.capture.points == points  # held: waveform frozen between triggers
            assert len(app.history) == history_len  # no scrolling in hold mode
            device.key_raw = 3900
            pump_until(lambda:app.capture.prev_down is False)
            device.key_raw = 2000  # latest keystroke wins and restarts the capture
            pump_until(lambda:app.capture.points and app.capture.points[0] == 2000,3)
            assert set(app.capture.points) == {2000}
            # Fall ramp at 50 counts/sample reproduces the device velocity math;
            # its own counter keeps the trigger in the linear zone regardless
            # of how many records the session has already streamed.
            device.key_raw = 3900
            pump_until(lambda:app.capture.prev_down is False)
            ramp = {'n':0}
            def ramp_values(seq):
                ramp['n'] += 1
                return max(100, 3900 - (ramp['n'] % 100)*50)
            device.key_cb = ramp_values
            pump_until(lambda:app.capture.velocity == 3*50*8000/3,3)
            assert app.capture.points[0] < 3000
            assert '400,000 counts/s' in app.hold_status.get()
            fitted = [app.graph.coords(item) for item in app.graph.find_all()
                      if app.graph.type(item) == 'line' and app.graph.itemcget(item,'fill') == '#7ee787']
            assert len(fitted) == 1 and len(fitted[0]) == 4, fitted
            assert fitted[0][0] == app.axis_w and fitted[0][3] > fitted[0][1]  # anchored at trigger, downward slant
            assert app.axis_w >= AXIS_W  # measured from the resolved mono face
            device.key_cb = None
            app.hold_button.invoke()
            pump_until(lambda:not app.hold_mode.get() and app.connection.stream_mode == 'gui')
            pump_until(app.usable,4)
            pump_until(lambda:len(app.history) > history_len,3)
            # Jankó layout telemetry: status marker and built-in note labels.
            device.flags |= 64
            pump_until(lambda:'JANKÓ' in app.status.get())
            root.update()
            assert app.canvas.itemcget(app.titles[32],'text') == 'A/D4'  # Jankó row note, not the mapping
            device.flags &= ~64
            pump_until(lambda:'JANKÓ' not in app.status.get())
            root.update()
            assert app.canvas.itemcget(app.titles[32],'text') == 'A/C4'  # configured mapping again
            app.disable_button.invoke()
            pump_until(lambda:not app.snapshot.flags & 1)
            app.toggle_connection()
            pump_until(lambda:not app.connection.is_alive())
            assert not app.usable()
            app.device.set('auto')  # auto-detection resolves before connecting
            with patch('keyboard_gui.find_midi_device',return_value='Fake Control'):
                app.connect_button.invoke()
            pump_until(app.usable)
            assert app.device.get() == 'Fake Control'
            app.toggle_connection()
            pump_until(lambda:not app.connection.is_alive())
            app.close(); root = None
            print('PASS Tk+SysEx: calibration arm/status/disabled edits/cancel, select A, apply pair/all/MIDI, MIDI trigger point, velocity start, 8 ksps keystroke hold mode, device auto-detect, disable, disconnect')
        finally:
            device.stop_event.set(); device.join(1)
            transport_patch.stop()
    finally:
        if root: root.destroy()
        os.close(read_fd)
        server.terminate(); server.wait(timeout=3)


if __name__ == '__main__': main()
