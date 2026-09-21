"""Offline C-to-Python test; no USB access or fixture firmware flashing."""
import subprocess
import sys
from keyboard_gui_model import decode,transport_text
from keyboard_boards import get_board, M1_TARGET, DEFAULT_TARGET
import midi_sysex as sx


def main():
    message = subprocess.check_output([sys.argv[1]])
    kind, session, sequence, payload = sx.decode(message)
    assert (kind, session, sequence) == (sx.SNAPSHOT, 123, 0)
    snapshot = decode(payload)
    assert len(payload) == 1508
    assert snapshot.count == 82 and len(snapshot.report) == 30
    assert snapshot.sample_hz == 8000 and snapshot.sequence == 42
    assert snapshot.ack == 9 and snapshot.result == 1
    assert snapshot.keyboard_mapping[81] == 135
    assert snapshot.press[81] == 2500 and snapshot.release[81] == 2700
    assert snapshot.midi_mapping[81] == 60
    assert snapshot.raw == tuple(4096-((4096-(3900+i))*4095+1548)//3096 for i in range(82))
    assert snapshot.storage_generation == 0x12345678
    assert snapshot.transport==3 and snapshot.transport_flags==1
    assert transport_text(snapshot)=='Bluetooth 2: ready'
    assert get_board(M1_TARGET).validates_wire(snapshot)
    assert not get_board(DEFAULT_TARGET).validates_wire(snapshot)
    print('M1: actual C command mailbox, 82-key encoder, SysEx stream and GUI decoder passed')


if __name__ == '__main__':
    main()
