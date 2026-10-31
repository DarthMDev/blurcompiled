"""Synthetic candidate/manifest/process tests; no original DLL fixtures."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from tools import recomp_crt_compare as c
from tests.test_recomp_crt import fixture


class TestCRTComparison(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # A fresh clone has no ignored local workspace yet. These synthetic
        # fixtures need that directory, not any installed game or SDK input.
        (c.ROOT / 'local').mkdir(exist_ok=True)

    def test_candidate_hash_and_metadata_are_independent_gates(self):
        data=fixture()
        desc=dict(sha256=hashlib.sha256(data).hexdigest(),image_base=0x10000000,image_size=0x3000,entry_rva=0x1000)
        self.assertEqual(c.checked_candidate(data,desc).entry_rva,0x1000)
        with self.assertRaisesRegex(ValueError,'SHA-256'):
            c.checked_candidate(data,dict(desc,sha256='0'*64))
        with self.assertRaisesRegex(ValueError,'metadata'):
            c.checked_candidate(data,dict(desc,image_size=0x4000))

    def test_map_is_half_open_and_high_base_does_not_authorize_execution(self):
        m=c.guest_map(0x78520000,0xa3000)
        self.assertEqual(m['arena_size'],0x785c3000)
        self.assertFalse(m['execution_authorized'])
        self.assertTrue(all(a['end']<=b['begin'] for a,b in zip(m['regions'],m['regions'][1:])))
        for base,size in [(0x0ff00000,0x1000),(0xfffff000,0x2000),(0x10000000,0)]:
            with self.assertRaises(ValueError): c.guest_map(base,size)

    def test_changed_manifest_input_refused(self):
        with tempfile.TemporaryDirectory(dir=c.ROOT/'local') as tmp:
            path=Path(tmp)/'fixture';path.write_bytes(b'original')
            inputs=c.prior.fingerprint([path]);c.verify_files(inputs)
            path.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'changed'): c.verify_files(inputs)

    def test_timeout_preserves_log_and_reaps_group(self):
        with tempfile.TemporaryDirectory(dir=c.ROOT/'local') as tmp:
            log=Path(tmp)/'timeout.log'
            r=c.run_bounded([c.sys.executable,'-u','-c','import time; print("partial"); time.sleep(20)'],log,.1)
            self.assertTrue(r['timed_out']);self.assertNotEqual(r['exit_code'],0)
            self.assertIn('partial',log.read_text())
            with self.assertRaises(FileExistsError): c.run_bounded(['true'],log,1)

    def test_exit_zero_is_not_coverage(self):
        clean={k:[] for k in ('failures','unmodelled','recover_errors','table_gaps','table_sites_undecoded')}
        clean.update(unmodelled_allowed=None,table_gaps_allowed=None,functions_total=2,functions_ok=2)
        self.assertTrue(c.strict_gate(0,clean,[]))
        self.assertFalse(c.strict_gate(0,None,[]))
        self.assertFalse(c.strict_gate(1,clean,[]))
        self.assertFalse(c.strict_gate(0,clean,['entry']))
        self.assertFalse(c.strict_gate(0,dict(clean,functions_ok=1),[]))
        for key in ('failures','unmodelled','recover_errors','table_gaps','table_sites_undecoded'):
            self.assertFalse(c.strict_gate(0,dict(clean,**{key:['issue']}),[]))
