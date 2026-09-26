"""Physical sensor labels for the GUI, read from board mapping tables."""
from pathlib import Path
import re

def sensor_labels(physical_ids=False):
    # Reuse the firmware's production-derived tables, not a guessed scan order.
    source = (Path(__file__).resolve().parent.parent / 'firmware/boards/huntsman_v3_pro_mini/src/keyboard_reference_tables.c').read_text()

    def table(name, count, width):
        body = source.split(f'{name}[{count}] = {{', 1)[1].split('};', 1)[0]
        rows = [tuple(int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', row))
                for row in re.findall(r'\{([^{}]+)\}', body)]
        if len(rows) != count or any(len(row) != width for row in rows):
            raise ValueError(f'invalid recovered table {name}')
        return rows

    grid = table('g_keyboard_grid', 72, 5)
    actions = {row[0]: row for row in table('g_keyboard_base', 135, 8)}
    usages = {i + 4: chr(ord('A') + i) for i in range(26)}
    usages.update({i + 0x1e: key for i, key in enumerate('1234567890')})
    usages.update({0x28: 'Ent', 0x29: 'Esc', 0x2a: 'BkS', 0x2b: 'Tab',
                   0x2c: 'Spc', 0x2d: '-', 0x2e: '=', 0x2f: '[', 0x30: ']',
                   0x31: '\\', 0x32: '#', 0x33: ';', 0x34: "'", 0x35: '`',
                   0x36: ',', 0x37: '.', 0x38: '/', 0x39: 'Cap',
                   0x64: 'N\\', 0x65: 'Mnu'})
    # International HID usages: neutral names, not guessed regional legends.
    usages.update({i + 0x87: f'I{i + 1}' for i in range(9)})
    modifiers = dict(zip((1 << i for i in range(8)),
                        ('LCt', 'LSh', 'LAl', 'LGu', 'RCt', 'RSh', 'RAl', 'RGu')))
    layouts = {}
    for profile, count in ((1, 61), (2, 62), (3, 65)):
        labels = [None] * count
        for position, row in enumerate(grid):
            sensor = row[profile + 1]
            if sensor >= count:
                continue
            key = row[1]
            # Same production boot patch as keyboard_key_for_sensor().
            if profile != 3 and position in (52, 53):
                key = 0x3b if position == 52 else 0x3e
            action = actions.get(key)
            if key == 0x3b:
                label = 'Fn'
            elif action is not None and action[1:3] == (2, 2):
                label = modifiers.get(action[3]) if action[3] else usages.get(action[4])
            else:
                label = None
            if labels[sensor] is not None:
                raise ValueError('duplicate recovered sensor mapping')
            labels[sensor] = key if physical_ids else label or f'?{key:02X}'
        if any(label is None for label in labels):
            raise ValueError('incomplete recovered sensor mapping')
        layouts[count] = labels
    return layouts
