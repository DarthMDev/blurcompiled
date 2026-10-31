import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from recomp_blur_probe import observations, probe


class TestBlurProbe(unittest.TestCase):
    def test_black_frames_and_exit_zero_do_not_establish_gameplay(self):
        result = observations('presented frames: 5\nguest exit code: 0\n'
                              'content: every presented frame was uniform\n'
                              'call to unknown target 00560000 (ESP=00000000)\n')
        self.assertEqual(result['presented_frames'], 5)
        self.assertEqual(result['guest_exit'], 0)
        self.assertTrue(result['uniform_frames'])
        self.assertEqual(result['missing_dispatch'][0]['rva'], '0x160000')
        self.assertFalse(result['gameplay_established'])
        self.assertFalse(result['parity_established'])

    def test_crash_has_no_fabricated_completion_counters(self):
        result = observations('crt.guard.unknown_call target=00560000\nSIGNAL 6\n')
        self.assertIsNone(result['presented_frames'])
        self.assertIsNone(result['guest_exit'])
        self.assertEqual(len(result['guards']), 1)
        self.assertEqual(result['missing_dispatch'][0]['va'], '0x00560000')

    def test_output_escape_and_reuse_are_refused_before_execution(self):
        with self.assertRaises(ValueError):
            probe('/tmp/blur-probe', '/nonexistent')
        root = Path(__file__).resolve().parents[1]
        with self.assertRaises(ValueError):
            probe(root / 'local', '/nonexistent')
        with self.assertRaises(ValueError):
            probe(root / 'local/recomp-blur-review/../../outside', '/nonexistent')

    def test_signal_context_does_not_invent_exact_fault_instruction(self):
        result = observations('[host] SIGNAL 11: fault_addr=0x70fffffff4\n'
                              '[host] SIGSEGV in guest thread 6: EIP=00af12e2 EAX=fffffff4\n')
        self.assertEqual(result['host_signals'], [11])
        self.assertEqual(result['last_guest_fault_context']['last_eip'], '0x00af12e2')
        self.assertIsNone(result['guest_exit'])


if __name__ == '__main__':
    unittest.main()
