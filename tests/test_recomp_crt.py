"""Synthetic PE fixtures only: no candidate/game binary required for tests."""
import struct
import unittest
from unittest.mock import patch

from tools.recomp_crt_experiment import CRTImage, validate_candidate, safe_output, ROOT


def fixture():
    data = bytearray(0x800)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x14c, 2)
    struct.pack_into("<H", data, 0x94, 224)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x10b)
    struct.pack_into("<I", data, opt + 16, 0x1000)
    struct.pack_into("<I", data, opt + 28, 0x10000000)
    struct.pack_into("<II", data, opt + 56, 0x3000, 0x200)
    struct.pack_into("<I", data, opt + 92, 16)
    struct.pack_into("<II", data, opt + 96, 0x2100, 0x100)
    for i, (name, rva, size, offset, flags) in enumerate([
        (b".text", 0x1000, 0x200, 0x200, 0x60000020),
        (b".data", 0x2000, 0x400, 0x400, 0xc0000040),
    ]):
        section = opt + 224 + 40 * i
        struct.pack_into("<8sIIII", data, section, name, size, rva, size, offset)
        struct.pack_into("<I", data, section + 36, flags)
    data[0x200] = 0xc3
    struct.pack_into("<IIIIII", data, 0x500 + 16, 10, 3, 3, 0x2140, 0x2150, 0x2160)
    struct.pack_into("<III", data, 0x540, 0x1000, 0x2040, 0x21d0)
    struct.pack_into("<III", data, 0x550, 0x2170, 0x2180, 0x2190)
    struct.pack_into("<HHH", data, 0x560, 0, 1, 2)
    for off, name in [(0x570, b"code\0"), (0x580, b"data\0"),
                      (0x590, b"forward\0"), (0x5d0, b"kernel32.Target\0")]:
        data[off:off + len(name)] = name
    return data


class TestCRTImage(unittest.TestCase):
    def test_export_code_data_forwarder_and_ordinals(self):
        exports = CRTImage(fixture()).exports()
        self.assertEqual([r["kind"] for r in exports], ["code", "data_candidate", "forwarder"])
        self.assertEqual([r["ordinal"] for r in exports], [10, 11, 12])
        self.assertEqual(exports[-1]["forwarder"], "kernel32.Target")
        self.assertEqual(exports[1]["rva"], 0x2040)

    def test_export_outside_image(self):
        data = fixture()
        struct.pack_into("<I", data, 0x540, 0x4000)
        with self.assertRaisesRegex(ValueError, "Export RVA"):
            CRTImage(data).exports()

    def test_export_table_crosses_file_backing(self):
        data = fixture()
        struct.pack_into("<I", data, 0x500 + 28, 0x23fc)
        with self.assertRaisesRegex(ValueError, "file-backed"):
            CRTImage(data).exports()

    def test_export_ordinal_outside_table(self):
        data = fixture()
        struct.pack_into("<H", data, 0x560, 9)
        with self.assertRaisesRegex(ValueError, "ordinal"):
            CRTImage(data).exports()

    def test_non_executable_entry(self):
        data = fixture()
        struct.pack_into("<I", data, 0x98 + 16, 0x2040)
        with self.assertRaisesRegex(ValueError, "entry"):
            CRTImage(data)

    def test_section_collision(self):
        data = fixture()
        struct.pack_into("<I", data, 0x98 + 224 + 40 + 12, 0x1100)
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            CRTImage(data)

    def test_directory_outside_image_and_optional_header(self):
        for change, value in [(96, 0x4000), (92, 17)]:
            data = fixture()
            struct.pack_into("<I", data, 0x98 + change, value)
            with self.assertRaises(ValueError):
                CRTImage(data)

    def test_security_directory_is_a_file_offset(self):
        data = fixture()
        struct.pack_into("<II", data, 0x98 + 96 + 4 * 8, 0x700, 0x100)
        self.assertEqual(CRTImage(data).directories[4], (0x700, 0x100))

    def test_unterminated_string_does_not_cross_section(self):
        data = fixture()
        data[0x7fc:] = b"abcd"
        with self.assertRaisesRegex(ValueError, "Unterminated"):
            CRTImage(data).string(0x23fc)

    def test_wraparound_image(self):
        data = fixture()
        struct.pack_into("<I", data, 0x98 + 28, 0xfffff000)
        with self.assertRaisesRegex(ValueError, "nonwrapping"):
            CRTImage(data)

    def test_static_tls_and_arena_are_hard_gates(self):
        image = CRTImage(fixture())
        with patch("tools.recomp_crt_experiment.IMAGE_SIZE", image.image_size), \
             patch.object(image, "entry_rva", 0xd140):
            validate_candidate(image)
            with self.assertRaisesRegex(ValueError, "fit"):
                validate_candidate(image, 0x10000000)
            image.directories[9] = (0x2200, 24)
            with self.assertRaisesRegex(ValueError, "TLS"):
                validate_candidate(image)

    def test_only_ignored_output_roots_allowed(self):
        self.assertEqual(safe_output(ROOT / "reports/crt-test.json"), ROOT / "reports/crt-test.json")
        with self.assertRaisesRegex(ValueError, "ignored"):
            safe_output(ROOT / "docs/generated.json")


if __name__ == "__main__":
    unittest.main()
