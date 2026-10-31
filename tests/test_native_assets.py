"""Synthetic native codec/index/DDS regression tests; also registered with CTest."""
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
PROBE = ROOT / 'build' / 'blur-asset-probe'
if __name__ == '__main__' and len(sys.argv) == 2:
    PROBE = pathlib.Path(sys.argv.pop())
from tools.read_pak_resource import content_checksum
from test_pak_resource import resource_fixture


def dds_fixture(w=2, h=1, depth=2):
    header = bytearray(128)
    header[:4] = b'DDS '
    for offset, value in ((4, 124), (8, 0x801007), (12, h), (16, w), (24, depth),
                          (76, 32), (80, 0x40), (88, 32), (92, 0xff0000),
                          (96, 0xff00), (100, 0xff), (108, 0x1000), (112, 0x200000)):
        struct.pack_into('<I', header, offset, value)
    pixels = bytes((3, 2, 1, 99, 6, 5, 4, 88, 9, 8, 7, 77, 12, 11, 10, 66))
    return header + pixels


@unittest.skipUnless(PROBE.is_file(), 'Build blur-asset-probe to run native comparisons')
class TestNativeAssets(unittest.TestCase):
    def codec(self, mode, stored, plain, size=None, checksum=None):
        return subprocess.run([str(PROBE), '--codec', str(mode), str(len(plain) if size is None else size),
                               str(content_checksum(plain) if checksum is None else checksum)],
                              input=stored, capture_output=True)

    def test_zlib_matches_python_across_checksum_boundaries(self):
        plain = bytes(range(256)) * 300
        r = self.codec(2, zlib.compress(plain), plain)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, plain)

    def test_raw_and_chunked_roundtrip(self):
        plain = bytes(range(256)) * 256 + b'last partial block'
        chunks = []
        for block in (plain[:65536], plain[65536:]):
            compressor = zlib.compressobj(wbits=-15)
            encoded = compressor.compress(block) + compressor.flush()
            chunks.append(struct.pack('<I', len(encoded)) + encoded)
        encoded = struct.pack('<I', 65536) + b''.join(chunks)
        for mode, stored in ((1, plain), (4, encoded),
                             (4, struct.pack('<II', 65536, 0xffffffff) + plain[:65536] + chunks[1])):
            r = self.codec(mode, stored, plain)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout, plain)

    def test_corrupt_truncated_trailing_and_size_limits(self):
        plain = b'a' * 100000
        encoded = zlib.compress(plain)
        cases = [(2, encoded[:-1], len(plain)), (2, encoded+b'junk', len(plain)),
                 (2, encoded+encoded, len(plain)), (2, b'invalid', len(plain)),
                 (2, encoded, 10), (2, encoded, len(plain)+1), (2, encoded, 33554433),
                 (2, encoded, 0), (1, b'abc', 100), (3, encoded, len(plain)),
                 (4, struct.pack('<II', 65536, 0), 1),
                 (4, struct.pack('<II', 65536, 65537)+b'\0'*65537, 65536)]
        for mode, stored, size in cases:
            with self.subTest(mode=mode, size=size):
                self.assertNotEqual(self.codec(mode, stored, plain, size=size).returncode, 0)
        self.assertNotEqual(self.codec(2, encoded, plain, checksum=0).returncode, 0)

    def test_native_index_and_resource_fixture(self):
        data, index = resource_fixture()
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'synthetic.pak'
            path.write_bytes(data)
            r = subprocess.run([str(PROBE), '--resource', str(path), '0'], capture_output=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(r.stdout, b'synthetic CSV\r\n1,2,3\r\n')
            mutations = ((0, b'PAK2'), (4, struct.pack('<I', 1)),
                         (2048, struct.pack('<I', 0xffffffff)),
                         (index, struct.pack('<I', 100001)),
                         (index+4, struct.pack('<I', 63)),
                         (index+12, struct.pack('<I', 2)),
                         (index+32, struct.pack('<Q', 0xfffffffffffffffe)),
                         (index+44, struct.pack('<I', 0xffffffff)))
            for offset, value in mutations:
                bad = bytearray(data); bad[offset:offset+len(value)] = value; path.write_bytes(bad)
                with self.subTest(offset=offset):
                    r = subprocess.run([str(PROBE), '--resource', str(path), '0'], capture_output=True)
                    self.assertNotEqual(r.returncode, 0)

    def test_native_next_sector_boundary(self):
        data, index = resource_fixture(random.Random(0).randbytes(3100))
        struct.pack_into('<II', data, index, 2, 128)
        data[index+72:index+136] = data[index+8:index+72]
        struct.pack_into('<Q', data, index+96, 4)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'overlap.pak'; path.write_bytes(data)
            r = subprocess.run([str(PROBE), '--resource', str(path), '0'], capture_output=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(b'boundary', r.stderr)

    def test_dds_volume_atlas_channels_and_slice_order(self):
        r = subprocess.run([str(PROBE), '--dds'], input=dds_fixture(), capture_output=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, bytes((1,2,3,255,4,5,6,255,7,8,9,255,10,11,12,255)))

    def test_dds_slice_row_wrap_and_unused_cells(self):
        header = dds_fixture(w=1, h=1, depth=9)[:128]
        source = b''.join(bytes((z, z+10, z+20, 99)) for z in range(9))
        r = subprocess.run([str(PROBE), '--dds'], input=header+source, capture_output=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        expected = b''.join(bytes((z+20, z+10, z, 255)) for z in range(9)) + b'\0' * (7*4)
        self.assertEqual(r.stdout, expected)

    def test_dds_rejects_unsupported_and_bad_bounds(self):
        good = dds_fixture()
        cases = [good[:127], good[:-1], good+b'x', b'DDS ']
        for offset, value in ((4, 123), (24, 0), (24, 65), (16, 0xffffffff),
                              (20, 3), (28, 2), (80, 4), (92, 0xff), (104, 0xff000000), (112, 0)):
            bad = bytearray(good); struct.pack_into('<I', bad, offset, value); cases.append(bad)
        for data in cases:
            with self.subTest(length=len(data)):
                r = subprocess.run([str(PROBE), '--dds'], input=data, capture_output=True)
                self.assertNotEqual(r.returncode, 0)


if __name__ == '__main__':
    unittest.main()
