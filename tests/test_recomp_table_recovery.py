import unittest
from tools.recomp_table_recovery import check_instruction_spans


class TestTableRecovery(unittest.TestCase):
    def test_gaps_require_separate_cfg_gate_but_preserve_entries(self):
        self.assertEqual(check_instruction_spans([(0x1000, 2), (0x1010, 1)],
            0x1000, 0x1020, baseline=[0x1000], entries=[0x1010],
            tables=[(0x1000, [0x1010, 0x1010])]), {0x1000, 0x1010})

    def test_overlaps_duplicate_and_escaping_instructions_rejected(self):
        for spans in ([(0x1000, 3), (0x1002, 1)], [(0x1000, 1), (0x1000, 1)],
                      [(0x1000, 16)], [(0x1000, 0)], [(0x1000, 2), (0x1020, 1)]):
            with self.subTest(spans=spans), self.assertRaises(ValueError):
                check_instruction_spans(spans, 0x1000, 0x1020)

    def test_missing_baseline_dispatch_or_table_boundary_rejected(self):
        for kw in ({'baseline': [0x1001]}, {'entries': [0x1001]},
                   {'tables': [(0x1000, [0x1001])]},
                   {'tables': [(0x1001, [0x1000])]},
                   {'tables': [(0x1000, [])]}):
            with self.subTest(kw=kw), self.assertRaises(ValueError):
                check_instruction_spans([(0x1000, 2)], 0x1000, 0x1002, **kw)

    def test_larger_audited_extent_requires_explicit_budget(self):
        spans = [(0x1000, 1), (0x3900, 1)]
        with self.assertRaises(ValueError):
            check_instruction_spans(spans, 0x1000, 0x3901)
        self.assertEqual(check_instruction_spans(spans, 0x1000, 0x3901,
            max_bytes=16384), {0x1000, 0x3900})
        for end, budget in [(0x5001, 16384), (0x3901, 65536)]:
            with self.assertRaises(ValueError):
                check_instruction_spans(spans, 0x1000, end, max_bytes=budget)

    def test_invalid_extent_or_missing_primary_rejected(self):
        for spans, end in (([], 0x1002), ([(0x1001, 1)], 0x1002),
                           ([(0x1000, 1)], 0x4000)):
            with self.assertRaises(ValueError):
                check_instruction_spans(spans, 0x1000, end)
