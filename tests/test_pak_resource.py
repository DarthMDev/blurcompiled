import io
import random
import struct
import unittest
import zlib
from tools.inspect_pak import inspect
from tools.read_pak_resource import read_resource, decode_zlib, decode_chunks, content_checksum, MAX_DECODED
from test_pak import fixture


def resource_fixture(plain=b'synthetic CSV\r\n1,2,3\r\n'):
    data, index = fixture(version=2, stride=64)
    stored = zlib.compress(plain)
    data[16] = 2
    struct.pack_into('<I', data, index + 8, zlib.crc32(b'\\synthetic.bin') ^ 0xffffffff)
    struct.pack_into('<I', data, index + 20, len(plain))
    struct.pack_into('<II', data, index + 40, content_checksum(plain), len(stored))
    if 6144 + len(stored) > len(data):
        data.extend(b'\0' * (6144 + len(stored) - len(data)))
    data[6144:6144 + len(stored)] = stored
    return data, index


class TestResource(unittest.TestCase):
    def test_direct_zlib_and_summary(self):
        data, _ = resource_fixture()
        stream = io.BytesIO(data)
        plain, report = read_resource(stream, inspect(stream), 0)
        self.assertEqual(plain, b'synthetic CSV\r\n1,2,3\r\n')
        self.assertEqual(report['decoded_size'], len(plain))

    def test_checksum_blocks_reset_and_sum(self):
        plain = bytes(range(256)) * 300
        expected = sum(zlib.crc32(plain[i:i + 32768]) ^ 0xffffffff
                       for i in (0, 32768, 65536)) & 0xffffffff
        self.assertEqual(content_checksum(plain), expected)
        self.assertNotEqual(expected, zlib.crc32(plain) ^ 0xffffffff)
        data, _ = resource_fixture(plain)
        stream = io.BytesIO(data)
        self.assertEqual(read_resource(stream, inspect(stream), 0)[0], plain)

    def test_crc_known_vector(self):
        self.assertEqual(content_checksum(b'123456789'), 0x340bc6d9)

    def test_incomplete_trailing_and_corrupt_streams(self):
        stored = zlib.compress(b'abc')
        for value in (stored[:-1], stored + b'junk', b'bad zlib', stored + stored):
            with self.subTest(value=value), self.assertRaises(ValueError):
                decode_zlib(value, 3)

    def test_output_limits_and_size_mismatch(self):
        stored = zlib.compress(b'a' * 100000)
        for size in (0, -1, 99999, 100001, 10, MAX_DECODED + 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                decode_zlib(stored, size)

    def test_metadata_checksum_failures(self):
        for relative in (8, 40):
            data, index = resource_fixture()
            data[index + relative] ^= 1
            stream = io.BytesIO(data)
            with self.assertRaisesRegex(ValueError, 'CRC|checksum'):
                read_resource(stream, inspect(stream), 0)

    def test_payload_bounds_and_file_size_change(self):
        data, index = resource_fixture()
        struct.pack_into('<I', data, index + 44, 9999)
        stream = io.BytesIO(data)
        with self.assertRaisesRegex(ValueError, 'range'):
            read_resource(stream, inspect(stream), 0)
        data, _ = resource_fixture()
        archive = inspect(io.BytesIO(data))
        with self.assertRaisesRegex(ValueError, 'range'):
            read_resource(io.BytesIO(data[:-1]), archive, 0)

    def test_next_resource_is_a_stored_range_bound(self):
        plain = random.Random(0).randbytes(3100)
        data, index = resource_fixture(plain)
        struct.pack_into('<II', data, index, 2, 128)
        data[index + 72:index + 136] = data[index + 8:index + 72]
        struct.pack_into('<Q', data, index + 96, 4)
        stream = io.BytesIO(data)
        archive = inspect(stream)
        with self.assertRaisesRegex(ValueError, 'next resource'):
            read_resource(stream, archive, 0)

    def test_unsupported_storage_and_absent_entries(self):
        for selector in (0, 3, 255):
            data, _ = resource_fixture()
            data[16] = selector
            stream = io.BytesIO(data)
            with self.assertRaisesRegex(ValueError, 'selectors'):
                read_resource(stream, inspect(stream), 0)
        data, _ = resource_fixture()
        archive = inspect(io.BytesIO(data))
        archive['entries'][0]['payload_offset'] = None
        with self.assertRaisesRegex(ValueError, 'no payload'):
            read_resource(io.BytesIO(data), archive, 0)

    def test_bad_index(self):
        data, _ = resource_fixture()
        stream = io.BytesIO(data)
        archive = inspect(stream)
        for index in (-1, 1):
            with self.assertRaises(ValueError): read_resource(stream, archive, index)

    def test_raw_storage(self):
        data, idx = resource_fixture()
        plain = b'synthetic CSV\r\n1,2,3\r\n'
        data[16] = 1
        struct.pack_into('<I', data, idx + 44, len(plain))
        data[6144:6144 + len(plain)] = plain
        stream = io.BytesIO(data)
        self.assertEqual(read_resource(stream, inspect(stream), 0)[0], plain)
        struct.pack_into('<I', data, idx + 44, len(plain) - 1)
        stream = io.BytesIO(data)
        with self.assertRaisesRegex(ValueError, 'sizes differ'):
            read_resource(stream, inspect(stream), 0)

    def test_chunked_compressed_and_raw_blocks(self):
        plain = bytes(range(256)) * 256 + b'final chunk'
        compressor = zlib.compressobj(wbits=-15)
        last = compressor.compress(plain[65536:]) + compressor.flush()
        stored = struct.pack('<II', 65536, 0xffffffff) + plain[:65536] + struct.pack('<I', len(last)) + last
        self.assertEqual(decode_chunks(stored, len(plain)), plain)
        compressor = zlib.compressobj(wbits=-15)
        first = compressor.compress(plain[:65536]) + compressor.flush()
        stored = struct.pack('<II', 65536, len(first)) + first + struct.pack('<I', len(last)) + last
        self.assertEqual(decode_chunks(stored, len(plain)), plain)

    def test_chunk_failures(self):
        compressor = zlib.compressobj(wbits=-15)
        block = compressor.compress(b'abc') + compressor.flush()
        valid = struct.pack('<II', 65536, len(block)) + block
        for stored, size in ((valid + b'junk', 3), (valid[:-1], 3), (valid, 2),
                             (valid, 4), (valid, MAX_DECODED + 1),
                             (struct.pack('<I', 123), 3), (struct.pack('<II', 65536, 0), 3)):
            with self.subTest(size=size), self.assertRaises(ValueError): decode_chunks(stored, size)
        oversized = struct.pack('<II', 65536, 65537) + b'\0' * 65537
        with self.assertRaises(ValueError): decode_chunks(oversized, 65536)


if __name__ == '__main__':
    unittest.main()
