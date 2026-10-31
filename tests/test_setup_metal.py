"""Synthetic source archive boundary checks; no game fixtures."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("setup_metal", Path(__file__).resolve().parents[1] / "tools/setup_metal.py")
metal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metal)


class SourceArchiveTests(unittest.TestCase):
    def archive(self, root, entries):
        archive = root / "source.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            for name, value, kind in entries:
                member = tarfile.TarInfo(name)
                if kind == "link":
                    member.type = tarfile.SYMTYPE
                    member.linkname = value
                    tar.addfile(member)
                else:
                    data = value.encode()
                    member.size = len(data)
                    tar.addfile(member, io.BytesIO(data))
        return archive

    def test_source_and_relative_link(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = self.archive(root, [("revision/include/a.h", "header", "file"), ("revision/link", "include/a.h", "link")])
            metal.extract(archive, root / "out")
            self.assertEqual((root / "out/link").read_text(), "header")

    def test_reject_escape_and_special_members(self):
        for entries in [[("revision/../escape", "x", "file")], [("revision/link", "../escape", "link")], [("/absolute", "x", "file")], [("one/a", "x", "file"), ("two/b", "x", "file")]]:
            with self.subTest(entries=entries), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                archive = self.archive(root, entries)
                with self.assertRaises(ValueError):
                    metal.extract(archive, root / "out")
                self.assertFalse((root / "out").exists())

    def test_reject_write_through_link(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = self.archive(root, [("revision/link", "elsewhere", "link"), ("revision/link/child", "x", "file")])
            with self.assertRaises(ValueError):
                metal.extract(archive, root / "out")

    def test_refuse_nonempty_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = self.archive(root, [("revision/a", "x", "file")])
            (root / "out").mkdir()
            (root / "out/keep").write_text("keep")
            with self.assertRaises(FileExistsError):
                metal.extract(archive, root / "out")
