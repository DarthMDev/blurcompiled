import unittest

from tools.recomp_finite_tables import finite_targets


class FiniteTableTests(unittest.TestCase):
    def test_remapping_preserves_case_order_duplicates_and_sparse_indices(self):
        indices = {0x2000: 7, 0x2001: 0, 0x2002: 7}
        pointers = {0x3000: 0x1010, 0x301c: 0x1020}
        self.assertEqual(finite_targets(indices.get, pointers.get, 0x3000, 3,
                                       0x1000, 0x1100, 0x2000),
                         [0x1020, 0x1010, 0x1020])

    def test_flat_table_and_exclusive_owner_end(self):
        pointers = {0x3000: 0x1000, 0x3004: 0x10ff}
        self.assertEqual(finite_targets(None, pointers.get, 0x3000, 2,
                                       0x1000, 0x1100), [0x1000, 0x10ff])
        for target in (None, 0x0fff, 0x1100):
            with self.assertRaises(ValueError):
                finite_targets(None, lambda _: target, 0x3000, 1, 0x1000, 0x1100)

    def test_bounds_and_unreadable_indices_are_not_inferred(self):
        for count in (0, 257):
            with self.assertRaises(ValueError):
                finite_targets(None, lambda _: 0x1000, 0x3000, count, 0x1000, 0x1100)
        for index in (None, -1, 256):
            with self.assertRaises(ValueError):
                finite_targets(lambda _: index, lambda _: 0x1000, 0x3000, 1,
                               0x1000, 0x1100, 0x2000)


if __name__ == '__main__':
    unittest.main()
