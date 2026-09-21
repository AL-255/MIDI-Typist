"""Host board contracts, selected by the device's build target, never key count.

Geometry is presentation metadata. The READY identity must match the selected
contract before configuration commands or profile imports are allowed.
"""
from dataclasses import dataclass
from functools import lru_cache
import json
from pathlib import Path
import re
from firmware_defaults import DEFAULTS as D
from keyboard_labels import sensor_labels


@dataclass(frozen=True)
class Key:
    sensor: int
    label: str
    x: float
    y: float
    width: float


def ansi_geometry():
    """Huntsman ANSI geometry; sensor order comes from its firmware tables."""
    rows=[
        [('Esc',1)]+[(s,1) for s in '1234567890-=']+[('BkS',2)],
        [('Tab',1.5)]+[(s,1) for s in 'QWERTYUIOP[]']+[('\\',1.5)],
        [('Cap',1.75)]+[(s,1) for s in "ASDFGHJKL;'"]+[('Ent',2.25)],
        [('LSh',2.25)]+[(s,1) for s in 'ZXCVBNM,./']+[('RSh',2.75)],
        [('LCt',1.25),('LGu',1.25),('LAl',1.25),('Spc',6.25),
         ('Fn',1.25),('RAl',1.25),('Mnu',1.25),('RCt',1.25)],
    ]
    labels=sensor_labels()[61];keys=[]
    for y,row in enumerate(rows):
        x=0
        for label,width in row:
            keys.append(Key(labels.index(label),label,x,y,width));x+=width
        if x!=15:raise ValueError('Invalid Huntsman row width')
    return tuple(keys)


def fun60_geometry():
    path=Path(__file__).resolve().parents[1]/'firmware/boards/monsgeek_fun60_pro_wired/include/fun60_keys.def'
    keys=[]
    for line in path.read_text().splitlines():
        if not line.startswith('FUN60_KEY('):continue
        match=re.fullmatch(r'FUN60_KEY\((\d+), ("(?:\\.|[^"\\])*"), (0x[0-9a-f]+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+)\)',line)
        if not match:raise ValueError('Invalid FUN60 key table entry')
        sensor,label,_,_,_,_,x,y,width=match.groups()
        keys.append(Key(int(sensor),json.loads(label),int(x)/4,int(y),int(width)/4))
    if len(keys)!=61:raise ValueError('Incomplete FUN60 key table')
    return tuple(keys)


@dataclass(frozen=True)
class Board:
    target: str
    name: str
    layouts: tuple  # accepted (profile, sensor count) pairs
    graphical_profile: int
    scan_hz: int
    midi_product: str
    geometry_factory: object
    storage_description: str

    def accepts(self,profile,count):return (profile,count)==(0,0) or (profile,count) in self.layouts
    def graphical(self,profile,count):return profile==self.graphical_profile and (profile,count) in self.layouts

    @lru_cache(maxsize=None)
    def geometry(self):
        keys=self.geometry_factory()
        count=dict(self.layouts)[self.graphical_profile]
        if sorted(k.sensor for k in keys)!=list(range(count)) or len({k.label for k in keys})!=count:
            raise ValueError(f'Invalid geometry/sensor mapping for {self.target}')
        if any(k.x<0 or k.y<0 or k.width<=0 for k in keys):raise ValueError('Invalid key dimensions')
        return keys

    def labels(self):return tuple(k.label for k in sorted(self.geometry(),key=lambda k:k.sensor))


BOARDS={b.target:b for b in (
    Board('RZ03-0499','Razer Huntsman V3 Pro Mini',((1,61),(2,62),(3,65)),1,
          D['HUNTSMAN_ASSUMED_SCAN_HZ'],'Huntsman V3 Pro Mini MIDI',ansi_geometry,
          'two reserved tail pages in the original free block; factory serial data is preserved'),
    Board('monsgeek_fun60_pro_wired','MonsGeek FUN60 PRO Wired',((4,61),),4,
          D['FUN60_SCAN_HZ'],'FUN60 PRO MIDI-Typist',fun60_geometry,
          'two reserved application sectors; factory data is preserved, but reflashing clears custom settings'),
)}
KNOWN_TARGETS={target:board.name for target,board in BOARDS.items()}


def board_for_target(target):
    try:return BOARDS[target]
    except (KeyError,TypeError):raise ValueError(f'Unsupported firmware build target: {target}') from None
