"""Receipt correction boundaries only; CPU fixtures never prove GPU completion."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from daily_session_guard import SessionGuard
import retirement_reaudit as audit


class ReauditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        runtime = self.root / 'runtime'; runtime.mkdir(mode=0o700)
        home = self.root / 'home'; self.run = home / 'runs' / 'test'
        self.run.mkdir(parents=True)
        self.guard = SessionGuard(runtime_root=runtime)
        receipt = self.guard.begin(home, 'test'); self.guard.hold(receipt)
        self.receipt = self.guard.read()
        self.result = dict(status='HOLD', returncode=0, residuals=[], watchdog_expired=False,
                           supervisor_interrupted=False, received_signals=0, native_lastuse_proof=False)
        self.put('result.json', self.result)
        self.put('retirement-audit.json', dict(retirement='NOT_PROVED', error=audit.OMISSION))
        self.put('manifest.json', dict(session='test', daily=True,
                 host_safety=dict(boot_id=self.guard.boot_id),
                 environment=dict(BB_FRAME_GEN_BACKEND='dlssg')))
        self.put('dlssg-generations.json', {})
        (self.run / 'dlssg-generations').mkdir()
        self.proof = dict(retirement='PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP')

    def put(self, name, value):
        (self.run / name).write_text(json.dumps(value))

    def execute(self, no_holders=lambda: True):
        with patch.object(audit.peer_retirement, 'audit_session', return_value=self.proof):
            return audit.reconcile(self.run, self.guard, no_holders)

    def test_verified_correction_preserves_original_and_retires_exact_receipt(self):
        original = (self.run / 'result.json').read_bytes()
        self.execute()
        self.assertEqual((self.run / audit.ORIGINAL_RESULT).read_bytes(), original)
        self.assertEqual(self.guard.read(), self.receipt | {'state': 'retired'})
        self.assertEqual(audit.read(self.run / 'result.json')['status'], 'PASS_NORMAL_TEARDOWN')

    def test_actual_audit_failure_keeps_hold_and_original_result(self):
        original = (self.run / 'result.json').read_bytes()
        with patch.object(audit.peer_retirement, 'audit_session', side_effect=ValueError('unknown owner')):
            self.assertRaises(ValueError, audit.reconcile, self.run, self.guard, lambda: True)
        self.assertEqual(self.guard.read(), self.receipt)
        self.assertEqual((self.run / 'result.json').read_bytes(), original)

    def test_abnormal_exit_or_other_error_never_uses_correction(self):
        for change in ({'returncode':True}, {'returncode':1}, {'watchdog_expired':True},
                       {'received_signals':1}, {'residuals':[123]}, {'supervisor_interrupted':True}):
            with self.subTest(change=change):
                self.put(audit.ORIGINAL_RESULT, self.result | change)
                self.assertRaises(ValueError, self.execute)
                self.assertEqual(self.guard.read(), self.receipt)
        self.put(audit.ORIGINAL_RESULT, self.result)
        self.put(audit.ORIGINAL_AUDIT, dict(retirement='NOT_PROVED', error='GPU timeout'))
        self.assertRaises(ValueError, self.execute)

    def test_quarantine_or_live_owners_block_correction(self):
        self.assertRaises(ValueError, self.execute, lambda: False)
        (self.run / 'native.abort').write_text('unknown GPU ownership')
        self.assertRaises(ValueError, self.execute)
        self.assertEqual(self.guard.read(), self.receipt)

    def test_wrong_session_and_evidence_drift_cannot_retire_hold(self):
        self.put('manifest.json', dict(session='other'))
        self.assertRaises(ValueError, self.execute)
        self.assertEqual(self.guard.read(), self.receipt)

    def test_interrupt_after_result_publication_keeps_hold_until_full_recheck(self):
        with patch.object(self.guard, 'retire_reaudited', side_effect=RuntimeError('interrupted')):
            self.assertRaises(RuntimeError, self.execute)
        self.assertEqual(self.guard.read(), self.receipt)
        self.assertEqual(audit.read(self.run / 'result.json'), self.result)
        self.put('dlssg-generations.json', {'changed':True})
        self.assertRaises(ValueError, self.execute)
        self.assertEqual(self.guard.read(), self.receipt)

    def test_second_full_audit_failure_never_publishes_pass(self):
        with patch.object(audit.peer_retirement, 'audit_session',
                          side_effect=[self.proof, ValueError('second audit failed')]):
            self.assertRaises(ValueError, audit.reconcile, self.run, self.guard, lambda: True)
        self.assertEqual(audit.read(self.run / 'result.json'), self.result)
        self.assertEqual(self.guard.read(), self.receipt)

    def test_prepared_interruption_can_resume_only_with_identical_evidence(self):
        with patch.object(audit.peer_retirement, 'audit_session',
                          side_effect=[self.proof, ValueError('interrupted')]):
            self.assertRaises(ValueError, audit.reconcile, self.run, self.guard, lambda: True)
        self.execute()
        self.assertEqual(self.guard.read()['state'], 'retired')


if __name__ == '__main__':
    unittest.main()
