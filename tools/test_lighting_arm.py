#!/usr/bin/env python3
"""Real SDK I2C IRQs + optical DMA + USB on ARM, with synthetic external devices."""
import argparse
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from test_optical_bus_arm import ScanArm
from lighting_reference_tables import recover

I2C = 0x40087000


class LightingArm(ScanArm):
    def __init__(self, elf, reference, profile=1):
        super().__init__(elf, reference, profile)
        self.put32(I2C + 0xff8, 0x40)  # I2C peripheral present
        self.i2c_regs = {I2C + 0x804: 1}
        self.packet_bytes = bytearray()
        self.transactions = []
        self.stall_i2c = False
        self.nack_i2c = False
        self.cpu.hook_add(UC_HOOK_MEM_READ, self.i2c_read, begin=I2C+0x800, end=I2C+0xeff)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.i2c_write, begin=I2C+0x800, end=I2C+0xeff)
        # Every booted application can mirror Fn-menu settings into the two
        # authorized tail pages, so the controller model is always present.
        # Imported lazily: the calibration suite imports this module.
        from test_calibration_arm import FlashModel
        self.flash = FlashModel(self.cpu)

    def i2c_read(self, cpu, access, address, size, value, _):
        self.put32(address, self.i2c_regs.get(address, 0))

    def i2c_write(self, cpu, access, address, size, value, _):
        if address == I2C + 0x804:
            value = self.i2c_regs.get(address, 0) & ~(value & 0x3000050)
        elif address == I2C + 0x808:
            value |= self.i2c_regs.get(address, 0)
        elif address == I2C + 0x80c:
            self.i2c_regs[I2C+0x808] = self.i2c_regs.get(I2C+0x808, 0) & ~value
        elif address == I2C + 0x820:
            assert value in (1, 2, 4), ('unexpected I2C/DMA control', value)
            if value == 2:
                assert not self.packet_bytes
                self.packet_bytes.append(self.i2c_regs[I2C+0x828])
                self.i2c_regs[I2C+0x804] = 7 if self.nack_i2c else 5
            elif value == 1:
                self.packet_bytes.append(self.i2c_regs[I2C+0x828])
                self.i2c_regs[I2C+0x804] = 5
            else:
                self.transactions.append((self.call_time(), bytes(self.packet_bytes)))
                self.packet_bytes.clear()
                self.i2c_regs[I2C+0x804] = 1
        self.i2c_regs[address] = value

    def call_time(self):
        return self.u32(self.symbols['s_milliseconds'])

    def pump_i2c(self, limit=512):
        if self.stall_i2c:
            return
        for _ in range(limit):
            if not self.i2c_regs.get(I2C+0x808, 0): return
            self.call('FLEXCOMM1_IRQHandler')
        if limit == 512: raise AssertionError('I2C IRQ state machine did not finish')

    def service(self, count=1):
        for _ in range(count):
            super().service(1)
            if hasattr(self, 'i2c_regs'): self.pump_i2c()


def expected_program(ops):
    return [bytes([address << 1, reg]) + bytes([fill])*size for address, reg, size, fill, delay in ops]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf')
    parser.add_argument('--reference', required=True)
    args = parser.parse_args()
    maps, programs = recover(args.reference)
    for profile in (1, 2, 3):
        dev = LightingArm(args.elf, args.reference, profile)
        dev.service(400)
        assert b'phase=5' in dev.command('light status'), bytes(dev.output)
        assert b'errors=0' in dev.command('light status')
        init = programs['primary'] + (programs['secondary'] if profile == 3 else [])
        assert [data for _, data in dev.transactions[:len(init)]] == expected_program(init)
        if profile != 3: assert all(data[0] == 0xa0 for _, data in dev.transactions)
        else:
            t = dev.transactions[14][0]
            assert dev.transactions[15][0] - t >= 5
        assert dev.u32(0x40001034) == dev.u32(0x40001038) == 0x101
        # Verify actual initialized master timing is <= 400 kHz.
        divider = dev.i2c_regs[I2C+0x814] + 1
        timing = dev.i2c_regs[I2C+0x824]
        period = (timing & 7) + 2 + ((timing >> 4) & 7) + 2
        assert 12000000 / divider / period == 400000
        assert all(not any(report) for report in dev.reports)
        for raw in (3800, 2300, 830):
            dev.raw = [3800] * dev.count
            dev.raw[32] = raw  # selected raw sensor; map, not physical-row guess
            dev.service(100)
            payload = next(data[2:] for _, data in reversed(dev.transactions) if len(data) == 194)
            c, r, g, b = maps[profile-1][32]
            pwm = 255 if raw >= 3800 else 255-((3800-raw)*255 + 1485)//2970
            assert (payload[r], payload[g], payload[b]) == (pwm,)*3
            # Keyboard/MIDI candidate adds the persistent green Enter marker.
            indicator = int('s_midi' in dev.symbols)
            primary_keys=sum(c==0 for c,_,_,_ in maps[profile-1])
            assert sum(v != 0 for v in payload) == 3*primary_keys-(0 if pwm else 3)-2*indicator
        if profile == 1:
            dev.raw[32] = 2300
            dev.service(100)
            dev.stall_i2c = True
            for _ in range(50):
                dev.service(1)
                if dev.i2c_regs.get(I2C+0x808, 0): break
            else: raise AssertionError('no primary transfer began')
            # A new pressed sample must not mutate the already-owned payload.
            dev.raw[32] = 830
            dev.service(3)
            dev.stall_i2c = False
            dev.pump_i2c()
            payload = dev.transactions[-1][1][2:]
            _, r, g, b = maps[0][32]
            assert (payload[r], payload[g], payload[b]) == (126,)*3
            dev.service(60)
            payload = next(data[2:] for _, data in reversed(dev.transactions) if len(data) == 194)
            assert (payload[r], payload[g], payload[b]) == (0,)*3
            # Invalid ADC input blanks the entire next lighting snapshot.
            dev.raw[0] = 0
            dev.service(60)
            assert not any(next(data[2:] for _, data in reversed(dev.transactions) if len(data) == 194))
            dev.raw[0] = 3800
            print('PASS immutable pending I2C payload, next-frame freshness, invalid-sample blanking', flush=True)
        dev.command('light off'); dev.service(50)
        assert not any(next(data[2:] for _, data in reversed(dev.transactions) if len(data) == 194))
        dev.command('light on'); dev.service(1300)
        frames = [data for _, data in dev.transactions]
        maintenance = expected_program(programs['maintenance'])
        assert any(frames[i:i+len(maintenance)] == maintenance for i in range(len(frames)))
        dev.command('scan stop'); dev.service(150)
        assert not any(next(data[2:] for _, data in reversed(dev.transactions) if len(data) == 194))
        assert b'host=0' in dev.command('keys status')
        assert not dev.reset_requests
        print(f'PASS profile {profile}: auto scan/LED startup, exact production I2C programs, '
              'linear per-key output, maintenance, off/on/stale blanking, neutral host', flush=True)

    for failure in ('nack', 'stall'):
        dev = LightingArm(args.elf, args.reference)
        dev.nack_i2c = failure == 'nack'
        dev.stall_i2c = failure == 'stall'
        dev.service(400)
        assert b'phase=7' in dev.command('light status')
        assert b'errors=1' in dev.command('light status')
        transfers = len(dev.transactions)
        writes = len(dev.writes)
        dev.service(200)
        assert len(dev.transactions) == transfers
        assert not any(a in (0x4008c008, 0x4008c01a) for a, v in dev.writes[writes:])
        assert b'errors=0' in dev.command('scan status'), bytes(dev.output)
        # A LED-only fault must not disable the standalone keyboard mode.
        expected_host = b'KEYS host=1' if 's_raw' in dev.symbols else b'KEYS host=0'
        assert expected_host in dev.command('keys status')
        assert not dev.reset_requests
        print(f'PASS I2C {failure}: one fault, no retry/GPIO cycling, scanner and USB/CDC remain serviced', flush=True)


if __name__ == '__main__': main()
