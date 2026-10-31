"""Synthetic fixture normalization tests; never embeds the game's shaders."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("prepare_metal_fixtures", TOOLS / "prepare_metal_fixtures.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
sys.path.pop(0)


class FixtureNormalizationTests(unittest.TestCase):
    def test_big_endian_input_normalizes_to_native_little_endian(self):
        raw = struct.pack(">II", 0x12345678, 0xABCDEF12)
        native = struct.pack("<II", 0x12345678, 0xABCD0012)
        shader = {"name": "synthetic", "word_count": 2, "xor_words": {"1": 0xEF00},
                  "template_sha256": hashlib.sha256(raw).hexdigest(),
                  "ucode_sha256": hashlib.sha256(native).hexdigest()}
        words, result = fixtures.normalize(raw, shader)
        self.assertEqual(words, [0x12345678, 0xABCD0012])
        self.assertEqual(result, native)
        with self.assertRaises(ValueError):
            fixtures.normalize(bytes(8), shader)
        shader["xor_words"] = {}
        with self.assertRaises(ValueError):
            fixtures.normalize(raw, shader)
