"""Read the firmware's literal defaults; never maintain a second set in Python.

Run host tools from a complete checkout (or ship defaults.h alongside them at
the same relative path). Missing definitions fail loudly; no stale fallback.
This parser deliberately does not evaluate C expressions or run a compiler.
"""
import ast
from pathlib import Path
import re

HEADER = Path(__file__).resolve().parents[1] / 'firmware/app/include/defaults.h'


def parse_defaults(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    result = {}
    for name, value in re.findall(r'^#define[ \t]+(\w+)[ \t]+([^\n]+)', text, re.M):
        if re.fullmatch(r'-?(?:0x[0-9a-fA-F]+|[0-9]+)[uUlL]*', value.strip()):
            if name in result:
                raise ValueError(f'duplicate default: {name}')
            result[name] = int(value.strip().rstrip('uUlL'), 0)
    return result


DEFAULTS = parse_defaults(HEADER.read_text(encoding='utf-8'))


def initializer(name):
    """Read a literal C-only table for host display and reference audits."""
    text = HEADER.read_text(encoding='utf-8').replace('\\\n', '')
    match = re.search(r'^#define[ \t]+' + re.escape(name) + r'[ \t]+(\{[^\n]+\})', text, re.M)
    if not match:
        raise ValueError(f'missing literal initializer: {name}')
    value = ast.literal_eval(match[1].replace('{', '[').replace('}', ']'))
    def valid(item):
        return type(item) is int or isinstance(item, list) and all(valid(v) for v in item)
    if not valid(value):
        raise ValueError(f'non-integer initializer: {name}')
    return value
