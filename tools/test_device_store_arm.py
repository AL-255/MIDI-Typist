#!/usr/bin/env python3
"""Whole-profile persistence through actual compiled scan/menu/SysEx/flash paths."""
import argparse
from test_calibration_arm import Live, SLOTS, record
from test_keyboard_mode_arm import snapshot
from keyboard_labels import sensor_labels

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf');parser.add_argument('--reference',required=True)
    args=parser.parse_args()
    dev=Live(args.elf,args.reference);dev.service(750)
    labels=sensor_labels()[61]
    s=snapshot(dev,'stream gui')
    assert s.storage_flags==1 and s.storage_generation==1 and not s.calibration_error
    def combo(key):
        dev.raw[labels.index('Fn')]=500;dev.raw[labels.index(key)]=500;dev.service(20)
        dev.raw=[4000]*61;dev.service(350)
    combo('Ent');combo('J')
    s=snapshot(dev);assert s.performance_mode==1 and s.flags&64 and s.storage_flags==1
    assert snapshot(dev,'cfg all 901 2300 3100').result==1
    assert snapshot(dev,'cfg velocity 902 8').result==1
    dev.service(350);s=snapshot(dev)
    assert s.storage_flags==1 and not s.calibration_error
    generation=s.storage_generation
    saved={a:bytes(p) for a,p in dev.flash.pages.items()}
    assert all(p[:4]==b'MTP1' for p in saved.values())
    reboot=Live(args.elf,args.reference,saved);reboot.service(750)
    s=snapshot(reboot,'stream gui')
    assert s.performance_mode==1 and s.flags&64 and s.press==(2300,)*61 and s.release==(3100,)*61
    assert s.velocity_start==8 and s.storage_generation==generation and s.storage_flags==1
    assert not reboot.flash.touched and not any(any(p) for p in reboot.reports)
    # Partial/CRC-invalid newest page must fall back; both invalid initialize.
    newest=SLOTS[s.storage_slot]
    damaged=dict(saved);damaged[newest]=saved[newest][:100]+b'\xff'*412
    fallback=Live(args.elf,args.reference,damaged);fallback.service(750)
    state=snapshot(fallback,'stream gui');assert state.storage_flags==1 and state.storage_generation==generation-1
    assert not fallback.flash.touched
    bad={a:b'corrupt!'+b'\xa5'*504 for a in SLOTS}
    cold=Live(args.elf,args.reference,bad);cold.service(750)
    state=snapshot(cold,'stream gui')
    assert state.performance_mode==0 and not state.flags&64 and state.press==(3500,)*61
    assert state.storage_flags==1 and state.storage_generation==1 and not state.calibration_error
    # A recognizable but unsupported record version is cold-start data,
    # never a source of migrated thresholds or calibration.
    import struct, zlib
    unsupported=bytearray(record(7));unsupported[4]=255
    struct.pack_into('<I',unsupported,508,zlib.crc32(unsupported[:508]))
    rejected=Live(args.elf,args.reference,{SLOTS[0]:bytes(unsupported)});rejected.service(750)
    state=snapshot(rejected,'stream gui')
    assert state.calibration_generation==0 and state.calibration_flags==4 and state.storage_flags==1
    assert rejected.flash.touched=={SLOTS[0]}
    print('PASS ARM MIDI+Janko, settings and velocity persist; no rewrite on boot; unsupported/corrupt/empty recovery and journal fallback; only tail pages written')

if __name__=='__main__':main()
