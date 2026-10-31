import tempfile
from pathlib import Path
import unittest
from tools.verify_recomp_generation import verify


class TestGenerationCoverage(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.table = self.root / 'table.c'
        self.chunk = self.root / 'chunk_000.c'
        self.table.write_text('void (*const recomp_aux_raw_ptrs[])(X86 *) = {\n'
                              ' fn_12340000, fn_12340010,\n};\n')
        self.chunk.write_text('void fn_12340000(X86 *c) { helper(c); }\n'
                              'void fn_12340010(X86 *c) { body_12340000(c, 1); }\n')

    def test_complete_primary_and_internal_entry(self):
        result = verify(self.root)
        self.assertTrue(result['structural_check_passed'])
        self.assertEqual(result['entries'], 2)
        self.assertEqual(result['definitions'], 2)

    def test_prototype_cannot_hide_missing_internal_entry(self):
        self.chunk.write_text('void fn_12340000(X86 *c) { helper(c); }\n'
                              'void fn_12340010(X86 *c);\n')
        result = verify(self.root)
        self.assertFalse(result['structural_check_passed'])
        self.assertEqual(result['missing_definitions'], ['12340010'])

    def test_duplicate_definitions_are_rejected(self):
        (self.root / 'chunk_001.c').write_text(self.chunk.read_text())
        result = verify(self.root)
        self.assertFalse(result['structural_check_passed'])
        self.assertEqual(result['duplicate_definitions'], ['12340000', '12340010'])

    def test_repeated_dispatch_entry_is_rejected(self):
        self.table.write_text(self.table.read_text().replace('fn_12340010,', 'fn_12340000,'))
        self.assertEqual(verify(self.root)['repeated_entries'], ['12340000'])

    def test_missing_or_empty_raw_table_is_rejected(self):
        for text in ('void (*const other[])(X86 *) = { fn_12340000 };',
                     'void (*const recomp_raw_ptrs[])(X86 *) = {};'):
            with self.subTest(text=text):
                self.table.write_text(text)
                with self.assertRaises(ValueError):
                    verify(self.root)
