import io
import struct
import unittest
from unittest.mock import patch
from tools.inspect_pak import inspect, NAME_KEY, ABSENT_SECTOR, MAX_NAMES, MAX_ENTRIES


def fixture(version=2, stride=40, inline=False, absent=False):
    data = bytearray(8192)
    struct.pack_into('<4sII', data, 0, b'2KAP', version, 2048)
    data[17 if version == 2 else 37] = int(stride == 64)
    names = b'\0\\synthetic.bin\0'
    size_off = 56 if inline else 2048
    struct.pack_into('<I', data, size_off, len(names))
    data[size_off + 4:size_off + 4 + len(names)] = bytes(b ^ NAME_KEY[i % len(NAME_KEY)] for i, b in enumerate(names))
    idx = 2048 if inline else 4096
    struct.pack_into('<II', data, idx, 1, stride)
    struct.pack_into('<IIIIQQ', data, idx + 8, 0x12345678, 1, 0xabcdef, 999999, 42, ABSENT_SECTOR if absent else 3)
    if version == 2:
        struct.pack_into('<II', data, idx + 40, 5, 6)
    return data, idx


class TestPak(unittest.TestCase):
    def test_observed_profiles_and_raw_fields(self):
        for version, stride, inline in ((1, 32, True), (2, 40, False), (2, 64, False)):
            with self.subTest(version=version, stride=stride):
                data, idx = fixture(version, stride, inline)
                r = inspect(io.BytesIO(data))
                self.assertEqual(r['index_offset'], idx)
                self.assertEqual(r['record_stride'], stride)
                e = r['entries'][0]
                self.assertEqual(e['name'], '\\synthetic.bin')
                self.assertEqual(e['payload_offset'], 6144)
                self.assertEqual(e['word_00'], 0x12345678)
                self.assertEqual(e['word_08'], 0xabcdef)
                self.assertEqual(e['logical_size'], 999999)  # not a stored payload length
                self.assertEqual('extra_24_hex' in e, stride == 64)

    def test_absent_sector_and_zero_first_word_do_not_change_stride(self):
        data, _ = fixture(absent=True)
        self.assertIsNone(inspect(io.BytesIO(data))['entries'][0]['payload_offset'])
        struct.pack_into('<I', data, 4104, 0)
        self.assertEqual(inspect(io.BytesIO(data))['entries'][0]['word_00'], 0)

    def test_exact_record_boundary_and_multiple_records(self):
        data, idx = fixture(absent=True)
        self.assertEqual(inspect(io.BytesIO(data[:idx + 8 + 40]))['entry_count'], 1)
        struct.pack_into('<II', data, idx, 2, 80)
        struct.pack_into('<IIIIQQII', data, idx + 48, 0, 1, 7, 0, 99, ABSENT_SECTOR, 0, 0)
        entries = inspect(io.BytesIO(data[:idx + 8 + 80]))['entries']
        self.assertEqual([e['word_00'] for e in entries], [0x12345678, 0])
        self.assertEqual([e['raw_time_u64'] for e in entries], [42, 99])

    def test_truncation_and_ranges(self):
        data, idx = fixture()
        for end in (0, 59, 2051, 2056, idx + 7, idx + 47):
            with self.subTest(end=end), self.assertRaises(ValueError):
                inspect(io.BytesIO(data[:end]))

    def test_unsupported_profiles(self):
        for offset, value in ((4, 3), (8, 0), (8, 4096)):
            data, _ = fixture()
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError): inspect(io.BytesIO(data))
        data, _ = fixture()
        data[:4] = b'PAK2'
        with self.assertRaises(ValueError): inspect(io.BytesIO(data))

    def test_allocation_and_count_limits(self):
        for where, value in ((2048, MAX_NAMES + 1), (4096, MAX_ENTRIES + 1), (4100, 0xffffffff)):
            data, _ = fixture()
            struct.pack_into('<I', data, where, value)
            with self.assertRaises(ValueError): inspect(io.BytesIO(data))

    def test_invalid_stride(self):
        for value in (0, 41, 48):
            data, idx = fixture()
            struct.pack_into('<I', data, idx + 4, value)
            with self.assertRaises(ValueError): inspect(io.BytesIO(data))

    def test_selector_mismatch(self):
        for selector in (1, 2):
            data, _ = fixture()
            data[17] = selector
            with self.assertRaisesRegex(ValueError, 'selector'): inspect(io.BytesIO(data))

    def test_bad_name_offsets(self):
        for value in (2, 999999):
            data, idx = fixture()
            struct.pack_into('<I', data, idx + 12, value)
            with self.assertRaises(ValueError): inspect(io.BytesIO(data))

    def test_unterminated_names(self):
        data, _ = fixture()
        data[2052 + len(b'\0\\synthetic.bin\0') - 1] ^= 1
        with self.assertRaises(ValueError): inspect(io.BytesIO(data))

    def test_name_length_and_string_count_limits(self):
        for names, message in ((b'a' * 4097 + b'\0', 'Name exceeds'), (b'\0a\0b\0', 'Too many strings')):
            data, _ = fixture()
            struct.pack_into('<I', data, 2048, len(names))
            data[2052:2052 + len(names)] = bytes(b ^ NAME_KEY[i % len(NAME_KEY)] for i, b in enumerate(names))
            with patch('tools.inspect_pak.MAX_STRINGS', 2), self.assertRaisesRegex(ValueError, message):
                inspect(io.BytesIO(data))

    def test_bad_payload_sectors(self):
        for value in (0, 2, 4, ABSENT_SECTOR - 1):
            data, idx = fixture()
            struct.pack_into('<Q', data, idx + 32, value)
            with self.assertRaises(ValueError): inspect(io.BytesIO(data))


if __name__ == '__main__':
    unittest.main()
