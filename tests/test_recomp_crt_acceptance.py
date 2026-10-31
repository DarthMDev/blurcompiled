"""Synthetic review gate; unknown diagnostics must never be accepted."""
import hashlib
import unittest
from copy import deepcopy
from tools import recomp_crt_acceptance as a
from tests.test_recomp_crt import fixture


class TestAcceptance(unittest.TestCase):
    def setUp(self):
        data=fixture();self.image=a.c.prior.CRTImage(data)
        sha=hashlib.sha256(data).hexdigest()
        self.inventory=dict(candidate_sha256=sha,recovery_audit=[],export_root_audit=[],withdrawn_root_audit=[],exact_recovery_diagnostics=[])
        stats=dict(failures=[],unmodelled=[],table_gaps=[],table_sites_undecoded=[],table_gaps_allowed=None,unmodelled_allowed=None,recover_errors=[],blocks_withdrawn=[],functions_total=2,functions_ok=2)
        self.report=dict(identity={'candidate_sha256':sha},step={'exit_code':0,'timed_out':False},statistics=stats,root_gaps=[])

    def test_exact_clean_synthetic_report(self):
        self.assertTrue(a.review(self.report,self.inventory,self.image,a.c.prior.ROOT)['translation_gate'])

    def test_missing_roots_failures_and_permissive_flags_rejected(self):
        for key in ('failures','unmodelled','table_gaps','table_sites_undecoded','table_gaps_allowed','unmodelled_allowed'):
            r=deepcopy(self.report);r['statistics'][key]=['unexpected']
            with self.assertRaises(ValueError):a.review(r,self.inventory,self.image,a.c.prior.ROOT)
        r=deepcopy(self.report);r['root_gaps']=[0x10001000]
        with self.assertRaisesRegex(ValueError,'dispatch gap'):a.review(r,self.inventory,self.image,a.c.prior.ROOT)

    def test_changed_identity_diagnostic_and_withdrawal_rejected(self):
        for change in ('identity','diagnostic','withdrawal','body'):
            r=deepcopy(self.report)
            if change=='identity':r['identity']['candidate_sha256']='0'*64
            if change=='diagnostic':r['statistics']['recover_errors']=['new error']
            if change=='withdrawal':r['statistics']['blocks_withdrawn']=[['10001000','data','1234']]
            if change=='body':r['statistics']['functions_ok']=1
            with self.assertRaises(ValueError):a.review(r,self.inventory,self.image,a.c.prior.ROOT)
