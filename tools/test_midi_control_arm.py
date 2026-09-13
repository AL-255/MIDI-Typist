#!/usr/bin/env python3
"""Offline bidirectional USB-MIDI SysEx control tests at both USB speeds."""
import argparse
import re
from pathlib import Path
import midi_sysex as sx
from midi_arm_peer import MidiArmPeer
from test_usb_arm import UsbArm

class MidiControlArm(UsbArm):
    def __init__(self, elf, high_speed):
        super().__init__(elf)
        self.output = bytearray()
        self.call('debug_init')
        self.call('keyboard_live_init')
        self.call('usb_composite_init')
        self.call('midi_control_command_handler', self.symbols['keyboard_live_command'])
        self.configure(high_speed)

    def configure(self, high_speed):
        self.reset(high_speed)
        self.control_out(bytes.fromhex('00 05 07 00 00 00 00 00'))
        self.control_out(bytes.fromhex('00 09 01 00 00 00 00 00'))
        self.peer = MidiArmPeer(self)
        self.peer.hello()

    def drain(self):
        self.output.clear()
        self.peer.drain()
        return bytes(self.output)

    def command(self, text, fragmented=False):
        self.output.clear()
        self.peer.command(text.strip())
        self.peer.drain()
        return bytes(self.output)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    args = parser.parse_args()
    for speed in (False, True):
        dev = MidiControlArm(args.elf, speed)
        ready = next(m[3] for m in dev.peer.messages if m[0] == sx.READY)
        git = ready.split(b' git=',1)[1]
        assert re.fullmatch(rb'(?:[0-9a-f]{40}|[0-9a-f]{64}) state=(?:clean|dirty)|unknown state=unknown',git),ready
        header=(Path(args.elf).parent/'generated/git_identity.h').read_text()
        commit=re.search(r'MT_GIT_COMMIT "([^"]+)"',header)[1]
        state=re.search(r'MT_GIT_STATE "([^"]+)"',header)[1]
        assert git == f'{commit} state={state}'.encode()
        dev.peer.command('git'); dev.peer.drain()
        assert dev.peer.messages[-1][0] == sx.ACK
        assert dev.peer.messages[-1][3] == b'git='+git
        # The query remains available during streaming and does not change mode.
        dev.peer.command('stream gui'); dev.peer.drain()
        dev.peer.command('git'); dev.peer.drain()
        assert dev.peer.messages[-1][3] == b'git='+git
        assert dev.call('scan_stream_gui_enabled')
        dev.peer.command('stream off'); dev.peer.drain()
        assert b'build=' in dev.command('version')
        before = len(dev.peer.messages)
        dev.peer.send(sx.COMMAND, b'version', dev.peer.sequence+1, cable=0)
        dev.peer.drain()
        assert len(dev.peer.messages) == before, 'performance cable executed a control command'
        wire = bytearray(sx.encode(sx.COMMAND, dev.peer.session, dev.peer.sequence+1, b'version'))
        wire[-2] ^= 1
        dev.complete(4, sx.usb_events(wire))
        dev.peer.drain()
        assert len(dev.peer.messages) == before, 'bad CRC accepted'
        dev.peer.command('version')
        dev.peer.drain()
        assert dev.peer.messages[-1][0] in (sx.ACK, sx.LOG)
        dev.peer.send(sx.CLOSE)
        dev.peer.drain()
        assert not dev.call('midi_control_ready')
        dev.peer.hello()
        assert dev.call('midi_control_ready')
        dev.put32(dev.symbols['s_milliseconds'],dev.call('board_millis')+3000)
        dev.peer.drain()
        assert not dev.call('midi_control_ready'), 'expired GUI lease remained active'
        dev.peer.hello()
        dev.peer.sequence = 0
        dev.peer.command('version'); dev.peer.drain()
        before = sum(m[0] == sx.LOG for m in dev.peer.messages)
        dev.peer.send(sx.COMMAND,b'version',1); dev.peer.drain()
        assert dev.peer.messages[-1][0] == sx.ERROR
        assert sum(m[0] == sx.LOG for m in dev.peer.messages) == before, 'duplicate command executed'
        assert not dev.reset_requests
        print(f'PASS {"HS" if speed else "FS"}: SysEx handshake, commands, CRC rejection, cable isolation, close/reconnect')

if __name__ == '__main__': main()
