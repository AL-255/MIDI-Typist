"""Board descriptions shared by GUI geometry, profiles and target selection.

Sensor order is a board contract, never inferred from a key count. M1 geometry
and sensor/HID bindings are read from the same table compiled by its C port.
"""
from dataclasses import dataclass
from functools import lru_cache
import json
from pathlib import Path
import re

from firmware_defaults import DEFAULTS as D
from keyboard_labels import sensor_labels

DEFAULT_TARGET = 'RZ03-0499'
M1_TARGET = 'MG-M1V5TMR'


@dataclass(frozen=True)
class Key:
    sensor: int
    label: str
    x: float
    y: float
    width: float


@dataclass(frozen=True)
class Board:
    target: str
    name: str
    keys: tuple[Key, ...]
    sample_hz: int
    profile: int = 1
    hid_bytes: int = 30
    wire_layouts: tuple = ((1,61),(2,62),(3,65))
    power_status: bool = False

    @property
    def count(self): return len(self.keys)

    @property
    def width(self): return max(k.x+k.width for k in self.keys)

    @property
    def height(self): return max(k.y+1 for k in self.keys)

    def labels(self): return tuple(k.label for k in sorted(self.keys,key=lambda k:k.sensor))

    def default_keycodes(self):
        platform = 'huntsman_v3_pro_mini' if self.target == DEFAULT_TARGET else 'monsgeek_m1_v5_tmr'
        path = Path(__file__).resolve().parents[1] / 'firmware/boards' / platform / 'config/keymap.def'
        pairs = re.findall(r'KEYMAP\(\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)',path.read_text())
        mapping = {int(k,16):int(v,16) for k,v in pairs}
        if len(mapping)!=len(pairs): raise ValueError('Duplicate physical key mapping')
        physical = sensor_labels(physical_ids=True)[self.count] if self.target == DEFAULT_TARGET else range(1,self.count+1)
        return tuple(mapping[k] for k in physical)

    def accepts(self, snapshot):
        return bool(snapshot and snapshot.profile == self.profile and snapshot.count == self.count)

    def validates_wire(self,snapshot):
        return (len(snapshot.report)==self.hid_bytes and
                ((snapshot.profile,snapshot.count)==(0,0) or
                 ((snapshot.profile,snapshot.count) in self.wire_layouts and snapshot.sample_hz==self.sample_hz)))


def ansi_geometry():
    rows = [
        [('Esc',1)] + [(s,1) for s in '1234567890-='] + [('BkS',2)],
        [('Tab',1.5)] + [(s,1) for s in 'QWERTYUIOP[]'] + [('\\',1.5)],
        [('Cap',1.75)] + [(s,1) for s in "ASDFGHJKL;'"] + [('Ent',2.25)],
        [('LSh',2.25)] + [(s,1) for s in 'ZXCVBNM,./'] + [('RSh',2.75)],
        [('LCt',1.25),('LGu',1.25),('LAl',1.25),('Spc',6.25),
         ('Fn',1.25),('RAl',1.25),('Mnu',1.25),('RCt',1.25)],
    ]
    labels = sensor_labels()[61]
    keys = []
    for y,row in enumerate(rows):
        x = 0
        for label,width in row:
            keys.append(Key(labels.index(label),label,x,y,width))
            x += width
    return keys


def m1_records():
    path = Path(__file__).resolve().parents[1] / 'firmware/boards/monsgeek_m1_v5_tmr/include/m1_keys.def'
    pattern = re.compile(r'M1_KEY\(\s*(\d+),\s*(\d+),\s*(\d+),\s*(0x[0-9a-f]+),'
                         r'\s*(\d+),\s*(\d+),\s*(\d+),\s*("(?:[^"\\]|\\.)*")\)')
    records = []
    for line in path.read_text().splitlines():
        if not line.startswith('M1_KEY('): continue
        match = pattern.fullmatch(line)
        if not match: raise ValueError('Invalid M1 key definition')
        sensor,bank,rank,usage,x,y,width,label = match.groups()
        records.append((int(sensor),int(bank),int(rank),int(usage,16),
                        int(x),int(y),int(width),json.loads(label)))
    if ([r[0] for r in records] != list(range(82)) or
            len({(r[1],r[2]) for r in records}) != 82 or
            len({r[7] for r in records}) != 82 or
            any(not 0 <= r[1] < 6 or not 0 <= r[2] < 15 or r[6] < 1 for r in records)):
        raise ValueError('Incomplete or ambiguous M1 key definitions')
    return tuple(records)


@lru_cache(maxsize=1)
def boards():
    m1 = tuple(Key(s,label,x/4,y/4,w/4) for s,_,_,_,x,y,w,label in m1_records())
    return {
        DEFAULT_TARGET: Board(DEFAULT_TARGET,'Huntsman V3 Pro Mini',tuple(ansi_geometry()),D['HUNTSMAN_ASSUMED_SCAN_HZ']),
        M1_TARGET: Board(M1_TARGET,'MonsGeek M1 V5 TMR',m1,D['M1_SCAN_HZ'],hid_bytes=30,wire_layouts=((1,82),),power_status=True),
    }


def get_board(target=DEFAULT_TARGET):
    try: return boards()[target]
    except KeyError: raise ValueError(f'Unsupported keyboard target: {target}') from None
