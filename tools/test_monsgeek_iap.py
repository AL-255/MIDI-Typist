"""Offline FUN60 PRO updater boundaries. No USB access, even on failure."""
import struct
import unittest
import monsgeek_iap as iap


def custom_image(size=1027):
    data = bytearray(b'\xff' * size)
    data[:len(iap.HEADER_TAG)] = iap.HEADER_TAG
    data[0x20:0x20+len(iap.CUSTOM_MARKER)] = iap.CUSTOM_MARKER
    struct.pack_into('<II', data, iap.VECTOR_OFFSET, 0x20010000, iap.APP_BASE+0x301)
    return bytes(data)


class Peer:
    def __init__(self):
        self.sent = []
        self.reply = bytes(64)
        self.remaining = 0
        self.data = b''
        self.bad_query = False

    def set_feature(self, data):
        self.sent.append(data)
        if self.remaining:
            self.data += data
            self.remaining -= 1
            return
        opcode, count = struct.unpack_from('<HH', data)
        reply = bytearray(64)
        reply[:4] = bytes((0xab, opcode >> 8)) + count.to_bytes(2, 'little')
        if opcode == iap.START:
            self.remaining = count
        if opcode == iap.QUERY:
            reply[4] = 0xaa if self.bad_query else 0x55
            reply[5:8] = (sum(self.data) & 0xffffff).to_bytes(3, 'little')
        self.reply = bytes(reply)

    def get_feature(self):
        return self.reply


class Tests(unittest.TestCase):
    def test_boot_command(self):
        packet = iap.factory_boot_request()
        self.assertEqual(len(packet), 64)
        self.assertEqual(packet[:8], bytes.fromhex('7f55aa55aa000082'))
        self.assertEqual(sum(packet[:8]) & 255, 255)

    def test_image_boundary(self):
        image = iap.validate_image(custom_image(), 'custom')
        self.assertFalse(image.stripped_bootloader)
        self.assertEqual(len(image.padded) % 64, 0)
        self.assertEqual(image.padded[len(image.data):], b'\xff'*61)
        full = b'\x66'*0x5000 + custom_image()
        stripped = iap.validate_image(full, 'custom')
        self.assertTrue(stripped.stripped_bootloader)
        self.assertEqual(stripped.data, image.data)
        for data in (b'', custom_image(iap.APP_BYTES+1), bytes(1024)):
            with self.assertRaises(iap.IapError):
                iap.validate_image(data, 'custom')
        for stack, reset in ((0, iap.APP_BASE+0x301), (0x20010004, iap.APP_BASE+0x301),
                             (0x20001000, iap.APP_BASE+0x300), (0x20001000, iap.APP_BASE+0x101),
                             (0x20001000, iap.APP_BASE+0x801)):
            data = bytearray(custom_image())
            struct.pack_into('<II', data, iap.VECTOR_OFFSET, stack, reset)
            with self.assertRaises(iap.IapError):
                iap.validate_image(bytes(data), 'custom')

    def test_sku_header_is_not_identity(self):
        data = bytearray(custom_image())
        data[0x20:0x20+len(iap.CUSTOM_MARKER)] = b'\0'*len(iap.CUSTOM_MARKER)
        for dest in ('custom', 'factory', 'razer'):
            with self.assertRaises(iap.IapError):
                iap.validate_image(bytes(data), dest)

    def test_fields_and_replies(self):
        for args in ((iap.START,), (iap.START, 2241), (iap.START, -1),
                     (iap.START, 1, 0x1000000), (iap.PREPARE, 1), (0xc1ba,)):
            with self.assertRaises(iap.IapError):
                iap.command(*args)
        valid = bytes.fromhex('abc2080055563412').ljust(64, b'\0')
        iap.check_reply(valid, iap.QUERY, 8, 0x123456)
        for bad in (valid[:8], b'\0'+valid[1:], valid[:2]+b'\0'+valid[3:],
                    valid[:4]+b'\xaa'+valid[5:], valid[:5]+b'\0'+valid[6:]):
            with self.assertRaises(iap.IapError):
                iap.check_reply(bad, iap.QUERY, 8, 0x123456)

    def test_whole_transfer_and_no_retry(self):
        image = iap.validate_image(custom_image(), 'custom')
        peer = Peer()
        progress = []
        transfer = iap.IapTransfer(peer, sleep=lambda _: None)
        self.assertEqual(transfer.write(image, lambda *p: progress.append(p)), image.checksum)
        self.assertEqual(peer.data, image.padded)
        self.assertEqual(len(peer.sent), len(image.padded)//64+3)
        self.assertEqual(progress[-1], (17, 17))
        with self.assertRaises(iap.IapError):
            transfer.write(image)
        peer = Peer()
        peer.bad_query = True
        transfer = iap.IapTransfer(peer, sleep=lambda _: None)
        with self.assertRaisesRegex(iap.IapError, 'do not retry'):
            transfer.write(image)
        before = list(peer.sent)
        with self.assertRaises(iap.IapError):
            transfer.write(image)
        self.assertEqual(peer.sent, before)

    def test_failed_send_is_not_repeated(self):
        class Broken(Peer):
            def set_feature(self, data):
                super().set_feature(data)
                raise TimeoutError('Completion lost')
        peer = Broken()
        transfer = iap.IapTransfer(peer, sleep=lambda _: None)
        image = iap.validate_image(custom_image(), 'custom')
        with self.assertRaises(TimeoutError):
            transfer.write(image)
        with self.assertRaises(iap.IapError):
            transfer.write(image)
        self.assertEqual(len(peer.sent), 1)

    def test_forged_factory_object_rejected_before_write(self):
        peer = Peer()
        with self.assertRaises(iap.IapError):
            iap.IapTransfer(peer).write(iap.ApplicationImage(custom_image(), True, 'factory'))
        self.assertEqual(peer.sent, [])

    def test_unstripped_custom_object_rejected_before_write(self):
        peer = Peer()
        with self.assertRaisesRegex(iap.IapError, 'exclude the bootloader'):
            iap.IapTransfer(peer).write(iap.ApplicationImage(b'\xff'*0x5000+custom_image(), False, 'custom'))
        self.assertEqual(peer.sent, [])


if __name__ == '__main__':
    unittest.main()
