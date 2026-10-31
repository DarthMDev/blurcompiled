"""Synthetic SDK ZIP fixtures; no network or real SDK required."""
import hashlib
import importlib.util
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("setup_rexglue", Path(__file__).resolve().parents[1] / "tools/setup_rexglue.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class SetupSDKTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "sdk.zip"
        self.target = self.root / "installed"

    def archive_with(self, name="mac-arm64/bin/rexglue", mode=0o100755):
        with zipfile.ZipFile(self.archive, "w") as z:
            info = zipfile.ZipInfo(name)
            info.external_attr = mode << 16
            z.writestr(info, b"synthetic SDK tool")
        return {"platform": "mac-arm64", "archive_sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest()}

    def test_install_verified_archive(self):
        pin = self.archive_with()
        prefix = tool.install(self.archive, self.target, pin)
        self.assertEqual((prefix / "bin/rexglue").read_bytes(), b"synthetic SDK tool")
        with self.assertRaises(FileExistsError):
            tool.install(self.archive, self.target, pin)

    def test_bad_hash_does_not_create_destination(self):
        pin = self.archive_with()
        pin["archive_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            tool.install(self.archive, self.target, pin)
        self.assertFalse(self.target.exists())

    def test_traversal_rejected_before_extraction(self):
        pin = self.archive_with("mac-arm64/../../escaped")
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            tool.install(self.archive, self.target, pin)
        self.assertFalse(self.target.exists())

    def test_symlink_rejected_before_extraction(self):
        pin = self.archive_with(mode=stat.S_IFLNK | 0o777)
        with self.assertRaisesRegex(ValueError, "symlinks"):
            tool.install(self.archive, self.target, pin)
        self.assertFalse(self.target.exists())


if __name__ == "__main__":
    unittest.main()
