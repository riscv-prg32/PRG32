import json
import unittest
import zlib

from prg32.usb_protocol import (
    Frame,
    FrameDecoder,
    STATUS,
    encode_frame,
    cartridge_name,
)


class UsbProtocolTests(unittest.TestCase):
    def test_decoder_recovers_frame_after_console_noise(self) -> None:
        encoded = encode_frame(Frame(STATUS, 17, b'{"ok":true}'))
        decoder = FrameDecoder()
        self.assertEqual(decoder.feed(b"ordinary boot log\n" + encoded[:11]), [])
        frames = decoder.feed(encoded[11:])
        self.assertEqual(frames, [Frame(STATUS, 17, b'{"ok":true}')])

    def test_decoder_discards_corrupt_payload_and_resynchronises(self) -> None:
        damaged = bytearray(encode_frame(Frame(STATUS, 1, b"bad")))
        damaged[-1] ^= 0x80
        good = encode_frame(Frame(STATUS, 2, b"good"))
        self.assertEqual(
            FrameDecoder().feed(bytes(damaged) + good),
            [Frame(STATUS, 2, b"good")],
        )

    def test_cartridge_name_reads_fixed_header_name(self) -> None:
        image = bytearray(100)
        image[:4] = b"PRG2"
        image[40:40 + len(b"DeviceDemo")] = b"DeviceDemo"
        self.assertEqual(cartridge_name(bytes(image), "fallback.prg32"), "DeviceDemo")


if __name__ == "__main__":
    unittest.main()
