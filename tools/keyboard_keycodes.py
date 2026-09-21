"""Keyboard/keypad usage-page choices; physical legends are independent."""
NAMES = {0:'Disabled'}
NAMES.update({i+4:chr(65+i) for i in range(26)})
NAMES.update({i+0x1e:c for i,c in enumerate('1234567890')})
NAMES.update(dict(enumerate(('Enter','Escape','Backspace','Tab','Space','-','=',
    '[',']','Backslash','Non-US #',';',"'",'Grave accent',',','.','/','Caps Lock'),0x28)))
NAMES.update({i+0x3a:f'F{i+1}' for i in range(12)})
NAMES.update(dict(enumerate(('Print Screen','Scroll Lock','Pause','Insert','Home',
    'Page Up','Delete','End','Page Down','Right','Left','Down','Up','Num Lock',
    'Keypad /','Keypad *','Keypad -','Keypad +','Keypad Enter','Keypad 1',
    'Keypad 2','Keypad 3','Keypad 4','Keypad 5','Keypad 6','Keypad 7',
    'Keypad 8','Keypad 9','Keypad 0','Keypad .','Non-US Backslash','Application',
    'Power','Keypad ='),0x46)))
NAMES.update({i+0x68:f'F{i+13}' for i in range(12)})
NAMES.update({i+0x87:f'International {i+1}' for i in range(9)})
NAMES.update({i+0x90:f'Language {i+1}' for i in range(9)})
NAMES.update(dict(enumerate(('Left Ctrl','Left Shift','Left Alt','Left GUI',
    'Right Ctrl','Right Shift','Right Alt','Right GUI'),0xe0)))
USAGES = (0, *range(4,0xe8))

def keycode_name(usage):
    if type(usage) is not int or usage not in USAGES:
        raise ValueError('Invalid keyboard/keypad usage.')
    return f'{NAMES.get(usage,"Keyboard usage")} (0x{usage:02X})'

CHOICES = tuple(map(keycode_name,USAGES))

def parse_keycode(text):
    try: return USAGES[CHOICES.index(text)]
    except ValueError: raise ValueError('Select a keyboard keycode from the list.') from None
