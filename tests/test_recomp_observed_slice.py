from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from recomp_observed_slice import check_closed_cfg, checked_entries, check_extent, emitted_runtime_traps, baseline_entries, checked_local_tables


class TestObservedCFG(unittest.TestCase):
    def test_local_tables_require_finite_complete_instruction_targets(self):
        key = (0x1000, 0x1005)
        insns = [SimpleNamespace(addr=0x1000), SimpleNamespace(addr=0x1005),
                 SimpleNamespace(addr=0x1010), SimpleNamespace(addr=0x1020)]
        targets = {key: [0x1010, 0x1020]}
        self.assertEqual(checked_local_tables({key: 0x2000}, targets, set(), insns, True),
                         {0x1005: [0x1010, 0x1020]})
        for decoded, inferred, allow in [(targets, set(), False), (targets, {key}, True),
                                          ({}, set(), True), ({key: [0x1011]}, set(), True),
                                          ({key: [0x9000]}, set(), True)]:
            with self.assertRaises(ValueError):
                checked_local_tables({key: 0x2000}, decoded, inferred, insns, allow)

    def test_baseline_dispatch_entries_come_from_raw_prototypes(self):
        text = "void fn_00401000(X86 *c);\nvoid fn_00401010(X86 *c);\n#define FN_00402000 other\nvoid FN(00403000)(X86 *c);\n"
        self.assertEqual(baseline_entries(text), {0x401000, 0x401010})

    def test_only_explicit_unconditional_external_tails_are_allowed(self):
        jump = SimpleNamespace(addr=0x1000, mnem='JMP', target=0x9000)
        check_closed_cfg([jump], [0x1005], lambda i: i.target, set(), {0x9000})
        for mnem, target in [('JMP', 0x9001), ('JMP', None), ('JNZ', 0x9000)]:
            insns = [SimpleNamespace(addr=0x1000, mnem=mnem, target=target)]
            with self.assertRaises(ValueError):
                check_closed_cfg(insns, [0x1005], lambda i: i.target, {'JNZ'}, {0x9000})

    def test_deliberate_runtime_traps_are_not_reported_as_modelled(self):
        insns = [SimpleNamespace(addr=0x401000, raw='00401000  CMPNLESS XMM3,XMM2')]
        body = ['recomp_unmodelled(c, 0x401000u); return;',
                'recomp_unmodelled(c, 0x401000u); return;']
        self.assertEqual(emitted_runtime_traps(body, insns, 0x400000),
                         [dict(va='0x401000', rva='0x1000', instruction=insns[0].raw)])
        self.assertEqual(emitted_runtime_traps(['recomp_cvtps2pd(c, 1, 0);'],
                                              insns, 0x400000), [])

    def test_explicit_larger_bound_preserves_extent_checks(self):
        check_extent(0x1000, 0x3907, 0x1000, 16384)
        check_extent(0x1000, 0x2328, 0x1000, 8192)
        check_extent(0x1000, 0x2000, 0x1000, 4096)
        for first, end, bound in [(0x1000, 0x2328, 4096),
                                  (0x1000, 0x3001, 8192),
                                  (0x1000, 0x5001, 16384),
                                  (0x1000, 0x1000, 8192),
                                  (0x1000, None, 8192),
                                  (0x1001, 0x2000, 8192),
                                  (0x1000, 0x2000, 65536)]:
            with self.assertRaises(ValueError):
                check_extent(first, end, 0x1000, bound)

    def test_alternate_entries_must_be_instruction_boundaries(self):
        insns = [SimpleNamespace(addr=0x1000), SimpleNamespace(addr=0x1005)]
        self.assertEqual(checked_entries(insns, 0x1000, [0x1005, 0x1000, 0x1005]), [0x1005])
        for address in (0x1001, 0x0fff, 0x1006):
            with self.assertRaises(ValueError):
                checked_entries(insns, 0x1000, [address])

    def test_closed_loop_and_external_call(self):
        insns = [SimpleNamespace(addr=0x1000, mnem='CALL', target=0x9000),
                 SimpleNamespace(addr=0x1005, mnem='JNZ', target=0x1000),
                 SimpleNamespace(addr=0x1007, mnem='RET', target=None)]
        check_closed_cfg(insns, [0x1005, 0x1007, 0x1008], lambda i: i.target, {'JNZ'})

    def test_branch_into_instruction_and_unknown_jump_refused(self):
        for target in (0x1001, None, 0x9000):
            insns = [SimpleNamespace(addr=0x1000, mnem='JMP', target=target)]
            with self.assertRaises(ValueError):
                check_closed_cfg(insns, [0x1002], lambda i: i.target, set())

    def test_missing_call_fallthrough_refused(self):
        insns = [SimpleNamespace(addr=0x1000, mnem='CALL'),
                 SimpleNamespace(addr=0x1006, mnem='RET')]
        with self.assertRaises(ValueError):
            check_closed_cfg(insns, [0x1005, 0x1007], lambda i: None, set())


if __name__ == '__main__':
    unittest.main()
