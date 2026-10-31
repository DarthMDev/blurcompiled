from pathlib import Path
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from recomp_trap_batch import scan_chunk, covers, nonconflicting, BLUR_SHA
from recomp_recover_batch import check_exported_boundaries


class TrapInventoryTests(unittest.TestCase):
    def test_independent_export_must_match_every_byte_boundary_and_extent(self):
        insns = [SimpleNamespace(address=0x1000, size=3), SimpleNamespace(address=0x1003, size=1)]
        check_exported_boundaries([0x1000, 0x1003], insns, 0x1004)
        for addresses, body, end in (([], [], 0x1004), ([0x1000], insns, 0x1004),
                                      ([0x1000, 0x1002], insns, 0x1004),
                                      ([0x1000, 0x1003], insns, 0x1005)):
            with self.assertRaises(ValueError):
                check_exported_boundaries(addresses, body, end)

    def test_dispatch_conflicts_reject_both_owners_and_active_entries(self):
        rows = [dict(va=1, entries=[1, 2]), dict(va=3, entries=[3, 2]),
                dict(va=4, entries=[4]), dict(va=5, entries=[5])]
        accepted, conflicts = nonconflicting(rows, {4})
        self.assertEqual([r['va'] for r in accepted], [5])
        self.assertEqual([r['va'] for r in conflicts], [1, 3, 4])

    def test_owner_boundaries_duplicates_and_alternate_wrappers(self):
        text = '''/* FUN_00401000  3 insns  00401000..00401010  entries: 00401008 */
recomp_unmodelled(c, 0x401004u); recomp_unmodelled(c, 0x401004u);
void fn_00401008(X86 *c) { body_00401000(c, 0x401008u); }
/* recovered_00402000  2 insns  00402000..00402004 */
recomp_unmodelled(c, 0x402001u);
'''
        rows = scan_chunk(text)
        self.assertEqual([(r['va'], r['sites']) for r in rows],
                         [(0x401000, [0x401004]), (0x402000, [0x402001])])
        self.assertEqual(rows[0]['entries'], [0x401008])

    def test_orphan_outside_owner_and_missing_wrapper_fail_closed(self):
        for text in ('recomp_unmodelled(c, 0x401000u);',
                     '/* FUN_00401000  2 insns  00401000..00401004 */\nrecomp_unmodelled(c, 0x402000u);',
                     '/* FUN_00401000  2 insns  00401000..00401004  entries: 00401003 */\nrecomp_unmodelled(c, 0x401001u);'):
            with self.assertRaises(ValueError):
                scan_chunk(text)

    def test_coverage_requires_actual_replacement_and_no_retained_trap(self):
        report = dict(executable_sha256=BLUR_SHA, replaces_existing_entry=True,
                      va='0x401000', byte_size=16, alternate_entries=[], unmodelled=[])
        self.assertTrue(covers(0x401000, [0x401004], report))
        for changed in (dict(replaces_existing_entry=False), dict(executable_sha256='other'),
                        dict(unmodelled=[dict(va='0x401004')]), dict(byte_size=4)):
            self.assertFalse(covers(0x401000, [0x401004], {**report, **changed}))
        self.assertFalse(covers(0x401008, [0x401009], report))


if __name__ == '__main__':
    unittest.main()
