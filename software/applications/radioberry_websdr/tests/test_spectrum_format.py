"""Wire-format regressions independent of the firmware frame builder."""
import struct
import unittest

from spectrum_format import decode, newer


class SpectrumFormatTests(unittest.TestCase):
    def setUp(self):
        self.frame = bytes.fromhex(
            "52425331 0101 3000 ffffffff 02000000 78563412 e8030000 "
            "60566c00 80bb0000 80bb0000 0002 0000 32ce 3200 0000 00 00"
        ) + bytes(range(256)) * 2

    def test_synthetic_frame_and_quantization(self):
        header, levels = decode(self.frame)
        self.assertEqual(header["sequence"], 0xffffffff)
        self.assertEqual(header["stream_id"], 0x12345678)
        self.assertEqual(header["center_hz"], 7100000)
        self.assertEqual(header["capture_ms"], 1000)
        self.assertEqual(len(levels), 512)
        self.assertEqual(levels[:2], [-127.5, -127.0])
        self.assertEqual(levels[255], 0)

    def test_real_frame_with_gap(self):
        frame = bytearray(self.frame)
        frame[5] = 2
        struct.pack_into("<H", frame, 38, 512)
        struct.pack_into("<H", frame, 44, 9)
        frame[46] = 1
        header, _ = decode(frame)
        self.assertEqual(header["average_count"], 9)
        self.assertEqual(header["flags"], 2)

    def test_rejects_bad_length_header_and_profile(self):
        for frame in (self.frame[:-1], self.frame + b"\0"):
            with self.assertRaises(ValueError):
                decode(frame)
        # Magic, version, flags, header size, stream ID, frequency, sample
        # rate, span, bins, source FFT, scale, averages, window and reserved.
        for offset, fmt, value in (
            (0, "B", 0), (4, "B", 2), (5, "B", 16), (6, "H", 47),
            (16, "I", 0), (24, "I", 0), (28, "I", 44100), (32, "I", 24000),
            (36, "H", 256), (38, "H", 512), (40, "h", -10000),
            (42, "H", 0), (44, "H", 1), (46, "B", 1), (47, "B", 1),
            (5, "B", 0),
        ):
            with self.subTest(offset=offset, value=value):
                frame = bytearray(self.frame)
                struct.pack_into("<" + fmt, frame, offset, value)
                with self.assertRaises(ValueError):
                    decode(frame)

    def test_sequence_wraparound_and_stale_frames(self):
        self.assertTrue(newer(0, 0xffffffff))
        self.assertTrue(newer(11, 10))
        self.assertFalse(newer(0xffffffff, 0))
        self.assertFalse(newer(10, 10))
        self.assertFalse(newer(10, 11))
        self.assertFalse(newer(0x80000000, 0))


if __name__ == "__main__":
    unittest.main()
