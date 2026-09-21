#!/usr/bin/env python3
"""Compare calibration/thresholds to production; test raw frames through NKRO/editor."""
import argparse
import ctypes as C
import random
import struct
from production_arm import ProductionArm
from test_keyboard_config import State as Editor
from test_optical_key import State as KeyState, Config


class Engine(C.Structure):
    _fields_ = [('config', Editor), ('report', C.c_uint8 * 30)] + [
        (name, C.c_uint8 * 256) for name in ('pressed', 'fn_at_press', 'modifiers', 'usages')]


class Scan(C.Structure):
    _fields_ = [('engine', Engine), ('keys', KeyState * 65), ('sums', C.c_uint32 * 65)] + [
        (name, C.c_uint16 * 65) for name in ('raw', 'lower', 'upper')] + [
        ('levels', C.c_uint8 * 65), ('settling', C.c_uint8), ('calibrated', C.c_uint8),
        ('count', C.c_uint8), ('ready', C.c_bool), ('valid', C.c_bool)]


def thresholds(lib, path):
    p = ProductionArm(path)
    p.write(0x200270ac, p.read(0x2002519c, 0x12d3))
    record = 0x20029ba8
    p.stubs[0x2000c0c0] = lambda *args: record
    p.stubs[0x2000c180] = lambda key, *_: key
    p.stubs[0x2000c23c] = lambda *args: 0
    lib.keyboard_scan_thresholds.argtypes = [C.POINTER(Editor), C.c_uint8, C.POINTER(Config)]
    total = 0
    for profile in (1, 2, 3):
        p.write(0x200284fc, [profile])
        for level in range(1, 11):
            act = int.from_bytes(p.read(0x2001b486 + 2 * level, 2), 'little')
            release = max(256, act - 0x666)
            rapid = p.read(0x2001dd7a + 2 * level, 2)
            for i in range(135):
                p.write(0x20027a2d + 7*i, struct.pack('<HHH', act, release, act))
                p.write(0x20027de2 + 8*i, rapid + rapid)
            for mode in (0, 1, 2):
                editor = Editor(mode, 0, level, level, level, level, 0, 1, profile, 0, 0)
                p.write(0x040008ec, [level])
                for i in range(135):
                    key = p.byte(0x20027a2c + 7*i)
                    p.call(0x20015c04, key)
                    p.call(0x200164ac, key)
                    if mode == 1: p.call(0x2001620c, key)
                    if mode == 2: p.call(0x20015bc0, level)
                    expected = (p.byte(record + 0x23), p.byte(record + 0x1f),
                                p.byte(record + 0x14), p.byte(record + 0x16),
                                p.byte(record + 0x17), p.byte(record + 0x18), 8, 8)
                    config = Config()
                    lib.keyboard_scan_thresholds(C.byref(editor), key, C.byref(config))
                    actual = tuple(getattr(config, n) for n, _ in Config._fields_)
                    assert actual == expected, (profile, level, mode, hex(key), actual, expected)
                    total += 1
    print(f'PASS {total} normal/editor per-key threshold comparisons against production')


def calibration(lib, path):
    p = ProductionArm(path)
    p.write(0x200284fc, [1])
    # Exclude optional persistent calibration overrides: generic templates
    # have invalid pairs too. The actual external branch runs unchanged.
    p.stubs[0x2000c024] = lambda *args: 0x2003e000
    for pos in range(72):
        p.write(0x04001825 + 3*pos, [pos, 0, 10])
    lib.optical_key_calibrate.argtypes = [C.c_uint16, C.c_uint16, C.c_uint16, C.c_bool,
                                         C.POINTER(C.c_uint16), C.POINTER(C.c_uint16)]
    rng = random.Random(0x15dec)
    cases = [(500, 3800, 3300), (0, 4095, 3500), (2240, 3360, 3360),
             (200, 4095, 4000), (1, 2001, 4095), (1, 2002, 4095)]
    cases += [(rng.randrange(4096), rng.randrange(65536), rng.randrange(4096)) for _ in range(150)]
    for settled_flag in (False, True):
        for lo, hi, settled in cases:
            for sensor in range(61):
                p.write(0x20029788 + sensor*3, struct.pack('<HB', lo, 0))
                p.write(0x2002990e + sensor*3, struct.pack('<HB', hi, 0))
            for pos in range(72):
                p.write(0x04002508 + pos*4, struct.pack('<I', settled))
                p.write(0x20029bac + pos*39, struct.pack('<II', 2240, 3360))
            p.call(0x20015dec, int(settled_flag))
            lower, upper = C.c_uint16(2240), C.c_uint16(3360)
            lib.optical_key_calibrate(lo, hi, settled, settled_flag, C.byref(lower), C.byref(upper))
            expected = struct.unpack('<II', p.read(0x20029bac, 8))
            assert (lower.value, upper.value) == expected, (lo, hi, settled, settled_flag, expected)
    print(f'PASS {len(cases)*2} endpoint/settling cases against production calibration')


def pipeline(lib):
    lib.keyboard_scan_init.argtypes = [C.POINTER(Scan), C.c_uint8]
    lib.keyboard_scan_frame.argtypes = [C.POINTER(Scan), C.POINTER(C.c_uint16),
                                       C.POINTER(C.c_uint8), C.POINTER(C.c_uint8), C.c_void_p]
    lib.keyboard_scan_neutral.argtypes = [C.POINTER(Scan)]
    lib.keyboard_scan_neutral.restype = C.c_bool
    lib.keyboard_key_for_sensor.argtypes = [C.c_uint8, C.c_uint8]
    lib.keyboard_key_for_sensor.restype = C.c_uint8
    for profile in (1, 2, 3):
        scan = Scan()
        raw = (C.c_uint16 * 65)(*[3800]*65)
        lower = (C.c_uint8 * 195)(*(struct.pack('<HB', 500, 0)*65))
        upper = (C.c_uint8 * 195)(*(struct.pack('<HB', 3800, 0)*65))
        lib.keyboard_scan_init(C.byref(scan), profile)
        def frame(count=10):
            for _ in range(count):
                lib.keyboard_scan_frame(C.byref(scan), raw, lower, upper, None)
        def key(key_id, down):
            sensor = next(i for i in range(scan.count) if lib.keyboard_key_for_sensor(profile, i) == key_id)
            raw[sensor] = 500 if down else 3800
            frame()
        frame(127)
        assert not scan.ready and not any(scan.engine.report)
        frame(3)
        assert scan.ready and scan.calibrated == scan.count and lib.keyboard_scan_neutral(C.byref(scan))
        key(0x1f, True)  # A
        assert scan.engine.report[2] & 1
        key(0x20, True)  # S, simultaneous NKRO
        assert scan.engine.report[2] & 1 and any(scan.engine.report[3:])
        key(0x1f, False)
        key(0x20, False)
        assert not any(scan.engine.report)
        key(0x3b, True)
        key(0x10, True)
        assert scan.engine.config.mode == 1 and not any(scan.engine.report)
        key(0x10, False)
        key(0x3b, False)
        assert scan.engine.config.mode == 1
        key(0x0b, True)
        key(0x0b, False)
        assert scan.engine.config.actuation == 10
        key(0x6e, True)
        key(0x6e, False)
        assert scan.engine.config.mode == 0 and scan.engine.config.saved_actuation == 10
        key(0x3b, True)
        key(0x1e, True)
        key(0x1e, False)
        key(0x3b, False)
        assert scan.engine.config.mode == 2
        key(0x02, True)
        key(0x02, False)
        assert scan.engine.config.rapid == 1
        key(0x1e, True)
        key(0x1e, False)
        assert scan.engine.config.rapid_enabled == 0
        key(0x6e, True)
        key(0x6e, False)
        assert scan.engine.config.mode == 0 and not any(scan.engine.report)
        assert lib.keyboard_scan_neutral(C.byref(scan))
        raw[0] = 0
        frame(1)
        assert not scan.valid and not lib.keyboard_scan_neutral(C.byref(scan))
        print(f'PASS profile={profile}: raw ASIC frames -> settling/calibration -> NKRO/FN/editors; invalid-frame gate')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    lib = C.CDLL(args.library)
    thresholds(lib, args.reference)
    calibration(lib, args.reference)
    pipeline(lib)
