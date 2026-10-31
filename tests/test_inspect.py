import struct
import unittest
from tools.inspect_install import PE


def fixture():
    data = bytearray(0x600)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x14c, 1)
    struct.pack_into('<H', data, 0x94, 224)
    opt = 0x98
    struct.pack_into('<H', data, opt, 0x10b)
    struct.pack_into('<I', data, opt + 16, 0x1000)
    struct.pack_into('<I', data, opt + 28, 0x400000)
    struct.pack_into('<I', data, opt + 60, 0x200)
    struct.pack_into('<I', data, opt + 92, 16)
    struct.pack_into('<II', data, opt + 104, 0x1000, 40)
    struct.pack_into('<8sIIII', data, opt + 224, b'.rdata', 0x800, 0x1000, 0x400, 0x200)
    struct.pack_into('<5I', data, 0x200, 0x1080, 0, 0, 0x1060, 0x1080)
    data[0x260:0x26d] = b'KERNEL32.dll\0'
    struct.pack_into('<III', data, 0x280, 0x10a0, 0x80000007, 0)
    data[0x2a2:0x2ae] = b'ExitProcess\0'
    return data


class TestPE(unittest.TestCase):
    def test_named_and_ordinal_imports(self):
        pe = PE(fixture())
        self.assertEqual(pe.imports(), [{'dll': 'KERNEL32.dll', 'functions': ['ExitProcess', 'ordinal:7']}])

    def test_virtual_padding_is_not_file_data(self):
        with self.assertRaises(ValueError):
            PE(fixture()).offset(0x1500)

    def test_truncation(self):
        with self.assertRaises(ValueError):
            PE(fixture()[:0x250])

    def test_bad_thunk(self):
        data = fixture()
        struct.pack_into('<I', data, 0x280, 0x70000000)
        with self.assertRaises(ValueError):
            PE(data).imports()

    def test_missing_import_terminator(self):
        data = fixture()
        struct.pack_into('<I', data, 0x98 + 108, 20)
        with self.assertRaises(ValueError):
            PE(data).imports()

    def test_not_pe(self):
        with self.assertRaises(ValueError):
            PE(b'not a PE')


if __name__ == '__main__':
    unittest.main()
