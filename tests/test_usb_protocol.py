import json
import unittest
import zlib

from prg32.usb_protocol import (
    FLAG_FINAL,
    FLAG_RESTART,
    PERFORMANCE_GET,
    RESULT_DATA,
    STATUS,
    UPLOAD_BEGIN,
    UPLOAD_DATA,
    UPLOAD_END,
    Frame,
    FrameDecoder,
    ProtocolClient,
    cartridge_name,
    encode_frame,
    get_performance,
    upload_cartridge,
)


class FakeDevice:
    """Minimal in-memory firmware model used to exercise the host client."""

    def __init__(self, drop_first_data_reply: bool = False) -> None:
        self.decoder = FrameDecoder()
        self.pending = bytearray()
        self.received = bytearray()
        self.begin: dict = {}
        self.data_requests = 0
        self.drop_first_data_reply = drop_first_data_reply
        self.performance_flags: list[int] = []
        self.performance_chunks = [b'{"cases":', b'[]}']

    def _reply(self, request: Frame, kind: int, payload: bytes, flags: int = FLAG_FINAL) -> None:
        self.pending.extend(encode_frame(Frame(kind, request.request_id, payload, flags)))

    def write(self, data: bytes) -> int:
        for frame in self.decoder.feed(data):
            if frame.message_type == UPLOAD_BEGIN:
                self.begin = json.loads(frame.payload)
                self._reply(frame, STATUS, b'{"ok":true}')
            elif frame.message_type == UPLOAD_DATA:
                self.data_requests += 1
                offset = int.from_bytes(frame.payload[:4], "little")
                chunk = frame.payload[4:]
                if offset == len(self.received):
                    self.received.extend(chunk)
                if self.drop_first_data_reply and self.data_requests == 1:
                    continue
                reply = {"ok": True, "received": len(self.received), "total": self.begin["size"]}
                self._reply(frame, STATUS, json.dumps(reply).encode())
            elif frame.message_type == UPLOAD_END:
                ok = zlib.crc32(self.received) & 0xFFFFFFFF == self.begin["crc32"]
                self._reply(frame, STATUS, json.dumps({"ok": ok, "running": False}).encode())
            elif frame.message_type == PERFORMANCE_GET:
                self.performance_flags.append(frame.flags)
                index = len(self.performance_flags) - 1
                last = index == len(self.performance_chunks) - 1
                self._reply(frame, RESULT_DATA, self.performance_chunks[index], FLAG_FINAL if last else 0)
        return len(data)

    def read(self, size: int) -> bytes:
        data = bytes(self.pending[:size])
        del self.pending[:size]
        return data

    def close(self) -> None:
        pass


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

    def test_upload_reports_progress_and_verifies_crc(self) -> None:
        device = FakeDevice()
        image = bytes(range(256)) * 10
        progress: list[tuple[int, int]] = []
        result = upload_cartridge(
            ProtocolClient(device, timeout=1.0),
            "game.prg32",
            image,
            "cart2",
            command="store",
            chunk_size=1000,
            progress=lambda received, total: progress.append((received, total)),
        )
        self.assertTrue(result["ok"])
        self.assertEqual(bytes(device.received), image)
        self.assertEqual(device.begin["slot"], "cart2")
        self.assertEqual(progress[-1], (len(image), len(image)))
        self.assertEqual(len(progress), 3)

    def test_upload_resends_a_chunk_whose_reply_was_lost(self) -> None:
        device = FakeDevice(drop_first_data_reply=True)
        image = b"\x5a" * 300
        client = ProtocolClient(device, timeout=1.0)
        upload_cartridge(client, "game.prg32", image, "cart0", chunk_size=256)
        self.assertEqual(bytes(device.received), image)
        self.assertEqual(device.data_requests, 3)

    def test_upload_rejects_unknown_command_before_contacting_device(self) -> None:
        device = FakeDevice()
        with self.assertRaises(SystemExit):
            upload_cartridge(ProtocolClient(device), "game.prg32", b"x", "cart0", command="flash")
        self.assertEqual(device.begin, {})

    def test_performance_restarts_stream_then_reads_all_chunks(self) -> None:
        device = FakeDevice()
        data = get_performance(ProtocolClient(device, timeout=1.0))
        self.assertEqual(data, b'{"cases":[]}')
        self.assertEqual(device.performance_flags, [FLAG_RESTART, 0])


if __name__ == "__main__":
    unittest.main()
