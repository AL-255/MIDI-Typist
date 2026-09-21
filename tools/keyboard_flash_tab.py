"""Model-independent Tk firmware workspace; device adapters contain no widgets."""
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from flash_models import ConnectedDevice, adapters, matching_build


class FlashTab(ttk.Frame):
    MODES = {'custom':'MIDI-TYPIST', 'razer':'RAZER FIRMWARE',
             'bootloader':'BOOTLOADER', 'bootloader-unverified':'BOOTLOADER · SKU UNVERIFIED',
             'unknown':'UNRECOGNIZED APPLICATION'}

    def _font(self, size=None, weight='normal', mono=False):
        """The App's resolved typography, or None for the ttk default."""
        fonts = getattr(self.app, 'fonts', None)
        if fonts is None: return None
        return fonts.mono_font(size, weight) if mono else fonts.sans_font(size, weight)

    def __init__(self, parent, app, registry=None):
        super().__init__(parent,padding=24)
        self.app = app
        self.registry = registry if registry is not None else adapters()
        self.device = self.image = None
        self.busy = False
        self.messages = queue.Queue()
        self.process = None
        self.refresh_after_exit = False
        self.epoch = 0
        self.paths = {}
        self.columnconfigure(0,weight=1); self.columnconfigure(1,weight=2)
        self.rowconfigure(3,weight=1)
        ttk.Label(self,text='Firmware studio',style='Title.TLabel').grid(row=0,column=0,columnspan=2,sticky='w')
        ttk.Label(self,text='Convert, restore or update your keyboard. One device, one verified application image.',
                  wraplength=1000).grid(row=1,column=0,columnspan=2,sticky='w',pady=(5,18))
        modelbar=ttk.Frame(self);modelbar.grid(row=2,column=0,columnspan=2,sticky='ew',pady=(0,18))
        ttk.Label(modelbar,text='KEYBOARD MODEL').pack(side='left',padx=(0,12))
        self.model=tk.StringVar(value=next(iter(self.registry.values())).name)
        self.model_picker=ttk.Combobox(modelbar,textvariable=self.model,state='readonly',width=38,
            values=[adapter.name for adapter in self.registry.values()])
        self.model_picker.pack(side='left');self.model_picker.bind('<<ComboboxSelected>>',lambda _:self.refresh())
        self.refresh_button=ttk.Button(modelbar,text='Refresh device',command=self.refresh)
        self.refresh_button.pack(side='right')

        devicecard=ttk.LabelFrame(self,text='  01  /  Connected device  ',padding=18)
        devicecard.grid(row=3,column=0,sticky='nsew',padx=(0,16))
        self.badge=tk.StringVar(value='NOT CHECKED')
        ttk.Label(devicecard,textvariable=self.badge,foreground='#68d8cf',font=self._font(13,'bold'),
                  wraplength=330).pack(anchor='w',pady=(0,16))
        self.identity={}
        for key in ('Product','Serial number','Firmware','USB identity','Connection','Speed'):
            ttk.Label(devicecard,text=key.upper(),foreground='#8299aa',font=self._font(9)).pack(anchor='w',pady=(9,2))
            value=tk.StringVar(value='—');self.identity[key]=value
            ttk.Label(devicecard,textvariable=value,wraplength=340,justify='left').pack(anchor='w')
        self.device_note=tk.StringVar(value='Select the model and refresh. No flashing starts automatically.')
        ttk.Label(devicecard,textvariable=self.device_note,wraplength=340,foreground='#e8c27d',
                  justify='left').pack(anchor='w',pady=16)
        self.inspect_button=ttk.Button(devicecard,text='Read firmware details…',command=self.inspect)
        self.inspect_button.pack(anchor='w')
        self.extra=tk.StringVar(value='')
        ttk.Label(devicecard,textvariable=self.extra,wraplength=340,justify='left',foreground='#a6bdca').pack(anchor='w',pady=12)

        right=ttk.Frame(self);right.grid(row=3,column=1,sticky='nsew');right.columnconfigure(0,weight=1)
        actions=ttk.LabelFrame(right,text='  02  /  Choose destination  ',padding=18)
        actions.grid(row=0,column=0,sticky='ew')
        self.action=tk.StringVar();self.action_widgets=[]
        self.action_area=ttk.Frame(actions);self.action_area.pack(fill='x')
        self.option_note=tk.StringVar(value='Available actions appear after device identification.')
        ttk.Label(actions,textvariable=self.option_note,wraplength=570,foreground='#a6bdca').pack(anchor='w',pady=(12,0))

        firmware=ttk.LabelFrame(right,text='  03  /  Review firmware  ',padding=18)
        firmware.grid(row=1,column=0,sticky='ew',pady=(16,0));firmware.columnconfigure(0,weight=1)
        self.path=tk.StringVar()
        self.path.trace_add('write',lambda *_:self.invalidate_image())
        self.entry=ttk.Entry(firmware,textvariable=self.path,width=45)
        self.entry.grid(row=0,column=0,sticky='ew')
        self.browse_button=ttk.Button(firmware,text='Browse…',command=self.browse)
        self.browse_button.grid(row=0,column=1,padx=(6,0))
        self.validate_button=ttk.Button(firmware,text='Validate image',command=self.validate)
        self.validate_button.grid(row=1,column=0,columnspan=2,sticky='w',pady=10)
        self.image_info=tk.StringVar(value='Choose a file to see its application size and SHA-256.')
        ttk.Label(firmware,textvariable=self.image_info,wraplength=570,justify='left',font=self._font(9,mono=True)).grid(row=2,column=0,columnspan=2,sticky='w')
        self.confirm_model=tk.BooleanVar(value=False)
        self.confirm_check=ttk.Checkbutton(firmware,text='I verified this firmware is for the selected keyboard model.',
            variable=self.confirm_model,command=self.sync)
        self.confirm_check.grid(row=3,column=0,columnspan=2,sticky='w',pady=(14,0))
        self.safety=tk.StringVar(value=self.adapter.safety)
        ttk.Label(right,textvariable=self.safety,wraplength=610,foreground='#a6bdca',justify='left').grid(row=2,column=0,sticky='w',pady=14)
        self.flash_button=ttk.Button(right,text='Review and flash…',command=self.flash)
        self.flash_button.grid(row=3,column=0,sticky='ew')
        self.progress=ttk.Progressbar(right,mode='determinate')
        self.progress.grid(row=4,column=0,sticky='ew',pady=(12,6))
        self.status=tk.StringVar(value='Ready to identify a keyboard.')
        ttk.Label(right,textvariable=self.status,wraplength=610).grid(row=5,column=0,sticky='w')
        self.log=tk.Text(right,height=6,bg='#17232d',fg='#b7cfdd',insertbackground='white',
                         relief='flat',wrap='word',font=self._font(9,mono=True),state='disabled')
        self.log.grid(row=6,column=0,sticky='nsew',pady=(12,0));right.rowconfigure(6,weight=1)
        self.sync()

    @property
    def adapter(self):
        return next(a for a in self.registry.values() if a.name==self.model.get())

    @property
    def option(self):
        if self.device is None:return None
        return next((a for a in self.adapter.actions(self.device) if a.id==self.action.get()),None)

    def append_log(self,text):
        self.log.configure(state='normal');self.log.insert('end',str(text)+'\n')
        if int(self.log.index('end-1c').split('.')[0])>200:self.log.delete('1.0','2.0')
        self.log.see('end');self.log.configure(state='disabled')

    def sync(self):
        for widget in (self.refresh_button,self.browse_button,self.entry,self.confirm_check,*self.action_widgets):
            widget.configure(state='disabled' if self.busy else 'normal')
        self.model_picker.configure(state='disabled' if self.busy else 'readonly')
        self.inspect_button.configure(state='normal' if self.device and self.device.mode in self.adapter.inspection_modes and not self.busy and not self.app.demo else 'disabled')
        self.validate_button.configure(state='normal' if self.option and self.path.get().strip() and not self.busy else 'disabled')
        ready=bool(self.device and self.option and self.image and self.confirm_model.get() and not self.busy and not self.app.demo)
        self.flash_button.configure(state='normal' if ready else 'disabled')

    def invalidate_image(self):
        self.epoch+=1;self.image=None;self.confirm_model.set(False)
        if hasattr(self,'image_info'):self.image_info.set('Not validated. No device has been modified.')
        if hasattr(self,'flash_button'):self.sync()

    def refresh(self):
        if self.busy:return
        if self.app.demo:
            self.status.set('Demo mode never accesses a device.');return
        self.invalidate_image();self.device=None;self.badge.set('DETECTING…')
        self.busy=True;self.sync();adapter=self.adapter
        owner=self.app.connection
        build_hint=matching_build(getattr(owner,'build',None),adapter.build_target) if owner and owner.connected else None
        control_available=not (owner and owner.is_alive())
        def run():
            try:
                devices=adapter.discover()
                if len(devices)==1:devices=[adapter.identify(devices[0],build_hint,control_available)]
                self.messages.put(('discovery',devices))
            except Exception as error:self.messages.put(('error',str(error)))
        threading.Thread(target=run,daemon=True).start()

    def show_device(self,device):
        self.device=device
        self.badge.set(self.MODES.get(device.mode,device.mode.upper()))
        build=matching_build(getattr(self.app.connection,'build',None),self.adapter.build_target) if self.app.connection and self.app.connection.connected else None
        values=(device.product,device.serial,build if device.mode=='custom' and build else device.version,
                device.usb_id,device.location,device.speed)
        for key,value in zip(self.identity,values):self.identity[key].set(value)
        self.device_note.set(device.details.get('Notice',device.details.get('Serial note',device.details.get('Recovery','Read firmware details to query the reported version and serial.'))))
        self.extra.set('\n'.join(f'{k}: {v}' for k,v in device.details.items() if k not in ('Serial note','Recovery')))

    def set_options(self):
        for widget in self.action_area.winfo_children():widget.destroy()
        self.action_widgets=[]
        options=self.adapter.actions(self.device) if self.device else ()
        for option in options:
            button=ttk.Radiobutton(self.action_area,text=option.title,variable=self.action,value=option.id,command=self.choose_action)
            button.pack(anchor='w',pady=4);self.action_widgets.append(button)
        self.action.set(options[0].id if options else '')
        self.choose_action()

    def choose_action(self):
        option=self.option
        self.option_note.set(option.description if option else (self.device.details.get('Flashing','No safe flashing action is available.') if self.device else 'No safe flashing action is available.'))
        self.path.set(self.paths.get((self.adapter.id,option.destination),self.adapter.default_image if option and option.destination=='custom' else '') if option else '')
        self.safety.set(self.adapter.safety)
        self.sync()

    def browse(self):
        if not self.option:return
        path=filedialog.askopenfilename(title='Select '+self.option.title+' image',filetypes=self.adapter.filetypes)
        if path:self.path.set(path);self.validate()

    def validate(self):
        if self.busy or not self.option:return
        path=self.path.get().strip();destination=self.option.destination;adapter=self.adapter
        epoch=self.epoch;self.busy=True;self.status.set('Validating image without touching the keyboard…');self.sync()
        def run():
            try:self.messages.put(('image',(epoch,adapter.load_image(path,destination))))
            except Exception as error:self.messages.put(('error',str(error)))
        threading.Thread(target=run,daemon=True).start()

    def inspect(self):
        if self.busy or self.device is None or self.device.mode not in self.adapter.inspection_modes or self.app.demo:return
        self.launch_worker('inspect')

    def flash(self):
        if self.busy or self.device is None or self.image is None or not self.option or not self.confirm_model.get() or self.app.demo:return
        device,image,option=self.device,self.image,self.option
        if not messagebox.askyesno('Confirm '+option.title,
            f'{self.adapter.name}\n{self.MODES.get(device.mode,device.mode)} → {option.title}\n'
            f'USB {device.usb_id} at {device.location}\nReported serial: {device.serial}\n\n'
            f'{image.path}\n{len(image.data):,} bytes\nSHA-256: {image.digest}\n\n'
            f'{self.adapter.safety}\n\nKeep the device connected until completion. Flash now?'):return
        if self.app.connection and self.app.connection.is_alive():
            self.app.connection.stop();self.app.connection.join(1.5)
            if self.app.connection.is_alive():
                self.status.set('Configuration connection did not close; nothing was flashed.');return
        self.launch_worker('flash',image,option)

    def launch_worker(self,operation,image=None,option=None):
        elevated=hasattr(os,'geteuid') and os.geteuid()!=0
        if elevated and not shutil.which('pkexec'):
            messagebox.showerror('USB access','Install PolicyKit (pkexec), or run the GUI with appropriate USB permissions.');return
        command=([shutil.which('pkexec')] if elevated else [])+[sys.executable,
            str(Path(__file__).with_name('device_flash_service.py')),'--gui-worker',operation,
            '--model',self.adapter.id,'--token',self.device.token]
        if image:command+=['--action',option.id,'--image',image.path,'--sha256',image.digest]
        self.busy=True;self.app.flashing=operation=='flash';self.sync()
        self.progress.configure(value=0);self.status.set('Authorizing USB access…' if elevated else 'Opening device…')
        self.append_log('Requested '+operation+' for '+self.device.location)
        def run():
            completed=False
            try:
                self.process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
                for line in self.process.stdout:
                    try:message=json.loads(line)
                    except ValueError:self.messages.put(('status',line.strip()));continue
                    kind=message.get('kind');payload=message.get('payload')
                    if kind in ('done','device','error'):completed=True
                    self.messages.put((kind,payload))
                code=self.process.wait()
                if not completed:raise RuntimeError(f'USB worker ended without confirmation (exit {code}); no automatic retry.')
            except Exception as error:self.messages.put(('error',str(error)))
            finally:self.process=None;self.messages.put(('worker_exit',None))
        threading.Thread(target=run,daemon=True).start()

    def poll(self):
        while True:
            try:kind,payload=self.messages.get_nowait()
            except queue.Empty:break
            if kind=='discovery':
                self.busy=False
                if len(payload)==1:
                    self.show_device(payload[0])
                    self.status.set('Device identified. Select a destination and validate an image.' if self.adapter.actions(payload[0]) else payload[0].details.get('Flashing','No safe flashing action is available.'))
                else:
                    self.device=None;self.badge.set('NO DEVICE' if not payload else 'MULTIPLE DEVICES')
                    for value in self.identity.values():value.set('—')
                    self.device_note.set('Connect the selected model.' if not payload else 'The updater requires exactly one matching device; flashing is disabled.')
                    self.extra.set('\n'.join(f'{d.location}: {d.product} ({d.mode})' for d in payload))
                self.set_options()
            elif kind=='image':
                self.busy=False;epoch,image=payload
                if epoch==self.epoch:
                    self.image=image;self.paths[(self.adapter.id,image.destination)]=image.path
                    self.image_info.set(f'{len(image.data):,} bytes · {image.destination.upper()}\nSHA-256\n{image.digest}\n\n{image.description}')
                    self.status.set('Image validated. Confirm the model, then review the flash plan.')
            elif kind=='device':
                device=ConnectedDevice(**payload)
                build=matching_build(self.identity['Firmware'].get(),self.adapter.build_target)
                if device.mode=='custom' and build:
                    from dataclasses import replace
                    device=replace(device,version=build)
                self.show_device(device);self.status.set('Device information read. No firmware was written.')
            elif kind=='progress':
                done,total=payload;self.progress.configure(maximum=total or 1,value=done)
                self.status.set(f'Programming application · {done:,} / {total:,} bytes · {done*100//(total or 1)}%')
            elif kind=='status':self.status.set(str(payload));self.append_log(payload)
            elif kind=='done':
                self.refresh_after_exit=True
                self.progress.configure(value=self.progress['maximum']);self.invalidate_image()
                self.device=None;self.set_options();self.badge.set('FLASH COMPLETE')
                self.status.set('Verified application return. Refresh to read the connected device, then reconnect configuration.')
                self.append_log('SUCCESS · application SHA-256 '+str(payload))
            elif kind=='error':
                self.busy=self.process is not None;self.status.set(str(payload));self.append_log('ERROR · '+str(payload))
            elif kind=='worker_exit':
                self.busy=False;self.app.flashing=False
                if self.refresh_after_exit:
                    self.refresh_after_exit=False;self.refresh()
            self.sync()
