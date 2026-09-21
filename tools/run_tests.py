#!/usr/bin/env python3
"""Parallel, offline-only test suite; never opens, flashes or resets a keyboard."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
REFERENCE = ROOT.parent / 'extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin'


@dataclass(frozen=True)
class Test:
    name: str
    group: str
    command: tuple


def manifest(elf, library, reference, jobs):
    """One entry per independent audit: no repeated dependency chains."""
    tests = [Test('native', 'native', ('ctest', '--preset', 'host-tests', '-j', str(jobs)))]
    tests.append(Test('m1-native','m1',('ctest','--test-dir','build-m1-host','--output-on-failure')))

    def add(name, group, *args):
        tests.append(Test(name, group, (sys.executable, '-B', '-u', str(ROOT/'tools'/f'{name}.py'), *map(str,args))))

    add('test_m1_hal_arm','m1',ROOT/'build-m1-hal/m1_hal_audit.elf')
    add('test_m1_usb_arm','m1',ROOT/'build-m1-hal/m1_usb_audit.elf')
    add('test_m1_live_arm','m1',ROOT/'build-m1-hal/m1_live_audit.elf')
    add('test_m1_storage_arm','m1',ROOT/'build-m1-hal/m1_storage_audit.elf')
    add('test_m1_save_arm','m1',ROOT/'build-m1-hal/m1_save_audit.elf')
    add('test_m1_boot_arm','m1',ROOT/'build-m1-hal/m1_boot_audit.elf')
    for name in ('usb', 'usb_startup', 'usb_chirp'):
        add('test_'+name+'_arm', 'usb', elf)
    add('test_flash_dump_arm', 'dump', elf)
    add('test_midi_control_arm', 'keyboard', elf)
    for name, group in (('keyboard_mode','keyboard'), ('scan_stream','keyboard'),
                        ('lighting','lighting'), ('calibration','calibration'), ('device_store','calibration'), ('keyboard_menu','menu')):
        add('test_'+name+'_arm', group, elf, '--reference', reference)
    for domain in ('keyboard', 'lighting'):
        group = 'reference-'+domain
        add(domain+'_reference_tables', group, reference, '--check',
            ROOT/'firmware/boards/huntsman_v3_pro_mini/src'/f'{domain}_reference_tables.c')
        names = ('keyboard_config','optical_key','keyboard_scan') if domain == 'keyboard' else ('lighting',)
        for name in names:
            add('test_'+name, group, library, '--reference', reference)
    add('test_keyboard_gui_tk', 'gui')
    return tests


def execute(test, deadline, logs):
    """Bound the entire subprocess tree, including CTest's test children."""
    start = time.monotonic()
    log = logs / (test.name+'.log')
    with log.open('w') as output:
        remaining = deadline-start
        if remaining <= 0:
            output.write('Suite deadline expired before this task started.\n')
            return test, False, 0.0, log
        try:
            process = subprocess.Popen(test.command, cwd=ROOT, stdout=output,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                ok = process.wait(timeout=remaining) == 0
            except (subprocess.TimeoutExpired, KeyboardInterrupt):
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                output.write('\nSuite deadline or interruption: subprocess group stopped.\n')
                ok = False
        except OSError as error:
            output.write(str(error)+'\n')
            ok = False
    return test, ok, time.monotonic()-start, log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--timeout', type=float, default=300, help='total seconds, including builds (default 300)')
    parser.add_argument('--no-build', action='store_true', help='use already-built artifacts (CMake audit aliases)')
    parser.add_argument('--elf', type=Path, default=ROOT/'build-huntsman/huntsman_firmware.elf')
    parser.add_argument('--library', type=Path, default=ROOT/'build-host/libkeyboard_logic.so')
    parser.add_argument('--reference', type=Path, default=REFERENCE)
    parser.add_argument('--group', action='append', choices=('native','usb','dump','keyboard','lighting',
                        'calibration','menu','reference-keyboard','reference-lighting','gui','m1'))
    args = parser.parse_args()
    if args.jobs < 1 or not 0 < args.timeout < float('inf'):
        parser.error('jobs and timeout must be positive and finite')
    start = time.monotonic(); deadline = start+args.timeout
    tests = manifest(args.elf, args.library, args.reference, args.jobs)
    tests = [test for test in tests if not args.group or test.group in args.group]
    logs = ROOT/'build-test-logs'; logs.mkdir(exist_ok=True)

    def report(result):
        test, ok, seconds, path = result
        print(f'{"PASS" if ok else "FAIL"} {test.name}: {seconds:.2f}s', flush=True)
        if not ok:
            print(path.read_text(), flush=True)
        return ok

    if not args.no_build:
        groups = {test.group for test in tests}
        presets = []
        if groups & {'native','reference-keyboard','reference-lighting'}: presets.append('host-tests')
        if groups & {'usb','dump','keyboard','lighting','calibration','menu'}: presets.append('huntsman')
        for preset in presets:
            for action, command in (('configure', ('cmake','--preset',preset)),
                                    ('build', ('cmake','--build','--preset',preset,'-j',str(args.jobs)))):
                if not report(execute(Test(action+'-'+preset,'build',command), deadline, logs)):
                    return 1
        if 'm1' in groups:
            for build_dir, extra in (
                    ('build-m1-host', ('-DCMAKE_BUILD_TYPE=Debug',)),
                    ('build-m1-hal', ('-DCMAKE_BUILD_TYPE=Release',
                                     '-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake'))):
                for action,command in (
                    ('configure',('cmake','-S','.','-B',build_dir,'-G','Ninja',
                                  '-DMT_BOARD=monsgeek_m1_v5_tmr',*extra)),
                    ('build',('cmake','--build',build_dir,'-j',str(args.jobs)))):
                    if not report(execute(Test(action+'-'+build_dir,'build',command),deadline,logs)):
                        return 1
    # Each audit owns its emulator/PTY; only immutable artifacts are shared.
    # Native CTest dispatches its independent cases in parallel too.
    failures = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(execute,test,deadline,logs) for test in tests]
        for future in as_completed(futures):
            if not report(future.result()): failures += 1
    elapsed = time.monotonic()-start
    print(f'{len(tests)-failures}/{len(tests)} audit groups passed in {elapsed:.2f}s '
          f'(budget {args.timeout:g}s); logs: {logs}', flush=True)
    return int(bool(failures) or elapsed > args.timeout)


if __name__ == '__main__':
    sys.exit(main())
