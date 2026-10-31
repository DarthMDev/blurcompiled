"""Synthetic XEX-shaped fixtures only; no game content in these tests."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("prepare_xbox_game", Path(__file__).resolve().parents[1] / "tools/prepare_xbox_game.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class PrepareXboxGameTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'user "game" copy'
        self.game.mkdir()
        self.payload = b"XEX2synthetic-not-a-real-executable"
        (self.game / "default.xex").write_bytes(self.payload)
        self.recipe = json.loads((tool.RECIPE / "build-inputs.json").read_text())
        self.recipe.update(executable_sha256=hashlib.sha256(self.payload).hexdigest(), executable_size=len(self.payload))
        self.output = self.root / "local" / "project with spaces"

    def prepare(self, output=None):
        return tool.prepare(self.game, output or self.output, root=self.root, recipe=self.recipe)

    def test_prepares_without_copying_or_changing_game(self):
        project = self.prepare()
        self.assertEqual((self.game / "default.xex").read_bytes(), self.payload)
        self.assertFalse((project / "default.xex").exists())
        self.assertIn('\\"game\\"', (project / "blur_x360_manifest.toml").read_text())
        self.assertTrue((project / "src/main.cpp").is_file())
        self.assertFalse(json.loads((project / "build-status.json").read_text())["game_executable_built"])

    def test_rejects_other_executable_before_creating_outputs(self):
        (self.game / "default.xex").write_bytes(b"XEX2different executable")
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_rejects_non_xbox_input(self):
        (self.game / "default.xex").write_bytes(b"MZnot-an-xbox-executable")
        with self.assertRaisesRegex(ValueError, "XEX2"):
            self.prepare()

    def test_cannot_overwrite_existing_workspace(self):
        self.prepare()
        with self.assertRaises(FileExistsError):
            self.prepare()

    def test_cannot_write_outside_ignored_local_directory(self):
        with self.assertRaisesRegex(ValueError, "ignored local"):
            self.prepare(self.root / "published-source")


if __name__ == "__main__":
    unittest.main()
