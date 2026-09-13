"""USB-MIDI peer for offline ARM tests; never accesses physical hardware."""
import midi_sysex as sx


class MidiArmPeer:
    def __init__(self, device):
        self.device = device
        self.session = 0x12345678
        self.sequence = 0
        self.partial = bytearray()
        self.messages = []
        self.streaming = False

    def send(self, kind, payload=b'', sequence=0, cable=1):
        events = sx.usb_events(sx.encode(kind, self.session, sequence, payload), cable)
        for at in range(0, len(events), 64):
            self.device.complete(4, events[at:at+64])

    def command(self, text):
        if text.startswith('stream '): self.streaming = text != 'stream off'
        self.sequence += 1
        self.send(sx.COMMAND, text.encode('ascii'), self.sequence)

    def drain(self):
        dev = self.device
        for _ in range(1024):
            dev.call('debug_service')
            dev.call('debug_service')
            if not dev.u32(dev.packet_entry(5)) & 0x80000000: break
            address, length = dev.packet(5)
            data = bytes(dev.cpu.mem_read(address, length))
            dev.complete(5)
            for at in range(0, length, 4):
                event = data[at:at+4]
                if event[0] >> 4 == 0:
                    if hasattr(dev, 'midi_packets'): dev.midi_packets.append(event)
                    continue
                cin = event[0] & 15
                assert 4 <= cin <= 7
                count = 3 if cin == 4 else cin-4
                self.partial.extend(event[1:1+count])
                if cin != 4:
                    message = sx.decode(self.partial)
                    self.messages.append(message)
                    visible = message[0] in (sx.SNAPSHOT, sx.SAMPLES, sx.DUMP) or (message[0] == sx.LOG and not self.streaming)
                    if visible and hasattr(dev, 'output'):
                        dev.output.extend(message[3])
                    self.partial.clear()

    def hello(self):
        self.send(sx.HELLO)
        self.drain()
        assert any(m[0] == sx.READY for m in self.messages)

    def heartbeat(self):
        self.send(sx.KEEPALIVE)
