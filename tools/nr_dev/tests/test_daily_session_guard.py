"""Real temporary boot receipts across homes; no processes, game or GPU work."""
import ast
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('daily_boot_guard_test', HERE / 'daily_session_guard.py')
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
BOOT = '11111111-1111-4111-8111-111111111111'
NEXT_BOOT = '22222222-2222-4222-8222-222222222222'
PASS = {'status':'PASS_NORMAL_TEARDOWN', 'returncode':0, 'native_lastuse_proof':True,
        'residuals':[], 'watchdog_expired':False, 'supervisor_interrupted':False}


class BootReceipt(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.runtime = self.root / 'runtime'; self.runtime.mkdir(mode=0o700)
        self.a = self.root / 'home-a'; self.a.mkdir()
        self.b = self.root / 'home-b'; self.b.mkdir()
        self.guard = module.SessionGuard(runtime_root=self.runtime, boot_id=BOOT)

    def put(self, data):
        self.guard.path.write_bytes(data)
        self.guard.path.chmod(0o600)

    def test_pending_crash_receipt_blocks_every_home_without_mutation(self):
        receipt = self.guard.begin(self.a, 'game-a')
        self.assertEqual(self.guard.path.stat().st_mode & 0o777, 0o600)
        original = self.guard.path.read_bytes()
        other = module.SessionGuard(runtime_root=self.runtime, boot_id=BOOT)
        self.assertRaisesRegex(ValueError, 'global session pending/HOLD', other.admit)
        self.assertRaises(ValueError, other.begin, self.b, 'game-b')
        self.assertEqual(self.guard.path.read_bytes(), original)
        self.assertEqual(self.guard.read(), receipt)

    def test_only_actual_normal_proof_allows_next_home(self):
        receipt = self.guard.begin(self.a, 'game-a')
        self.guard.retire(receipt, PASS)
        self.assertEqual(self.guard.read()['state'], 'retired')
        other = module.SessionGuard(runtime_root=self.runtime, boot_id=BOOT)
        later = other.begin(self.b, 'game-b')
        self.assertEqual(later['root'], str(self.b))
        self.assertNotEqual(later['nonce'], receipt['nonce'])
        self.assertRaises(ValueError, self.guard.retire, receipt, PASS)
        self.assertEqual(other.read(), later)

    def test_any_incomplete_or_abnormal_proof_keeps_pending(self):
        receipt = self.guard.begin(self.a, 'game-a')
        original = self.guard.path.read_bytes()
        for change in ({'status':'HOLD'}, {'native_lastuse_proof':False}, {'returncode':True},
                       {'returncode':1}, {'residuals':[123]}, {'watchdog_expired':True},
                       {'supervisor_interrupted':True}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.guard.retire(receipt, PASS | change)
            self.assertEqual(self.guard.path.read_bytes(), original)

    def test_hold_is_never_relabelled_retired(self):
        receipt = self.guard.begin(self.a, 'game-a'); self.guard.hold(receipt)
        original = self.guard.path.read_bytes()
        self.assertRaises(ValueError, self.guard.retire, receipt, PASS)
        self.assertRaises(ValueError, self.guard.admit)
        self.assertEqual(self.guard.read()['state'], 'hold')
        self.assertEqual(self.guard.path.read_bytes(), original)

    def test_missing_or_wrong_own_receipt_cannot_claim_retirement(self):
        receipt = self.guard.begin(self.a, 'game-a')
        for change in ({'root':str(self.b)}, {'session':'game-b'}, {'nonce':'a'*32}):
            self.assertRaises(ValueError, self.guard.retire, receipt | change, PASS)
        self.guard.path.unlink()
        self.assertRaises(ValueError, self.guard.retire, receipt, PASS)
        self.assertRaises(ValueError, self.guard.hold, receipt)
        self.assertFalse(self.guard.path.exists())

    def test_new_boot_may_admit_without_claiming_old_retirement(self):
        receipt = self.guard.begin(self.a, 'game-a')
        other = module.SessionGuard(runtime_root=self.runtime, boot_id=NEXT_BOOT)
        self.assertEqual(other.admit(), receipt)
        self.assertEqual(self.guard.read()['state'], 'pending')
        new = other.begin(self.b, 'game-b')
        self.assertEqual(new['boot_id'], NEXT_BOOT)
        self.assertEqual(new['state'], 'pending')

    def test_malformed_unknown_or_duplicate_records_fail_closed(self):
        receipt = self.guard.begin(self.a, 'game-a')
        for value in (b'', b'not json', b'{}', b'[]', b'x'*(module.MAX_BYTES+1),
                      json.dumps(receipt | {'uid':os.getuid()+1}).encode(),
                      json.dumps(receipt | {'unexpected':1}).encode(),
                      json.dumps(receipt).encode()[:-1] + b',"state":"retired"}'):
            with self.subTest(value=value[:40]):
                self.put(value)
                self.assertRaises(ValueError, self.guard.admit)

    def test_wrong_mode_hardlink_symlink_and_special_file_fail_closed(self):
        self.guard.begin(self.a, 'game-a')
        self.guard.path.chmod(0o644)
        self.assertRaises(ValueError, self.guard.admit)
        self.guard.path.chmod(0o600)
        linked = self.root / 'hardlink'; os.link(self.guard.path, linked)
        self.assertRaises(ValueError, self.guard.admit)
        self.guard.path.unlink(); self.guard.path.symlink_to(linked)
        self.assertRaises(ValueError, self.guard.admit)
        self.guard.path.unlink(); os.mkfifo(self.guard.path, 0o600)
        self.assertRaises(ValueError, self.guard.admit)
        self.guard.path.unlink(); self.guard.path.mkdir()
        self.assertRaises(ValueError, self.guard.admit)

    def test_runtime_directory_cannot_follow_another_home_or_relaxed_mode(self):
        linked = self.root / 'runtime-link'; linked.symlink_to(self.runtime)
        self.assertRaises(ValueError, module.SessionGuard, linked, BOOT)
        self.runtime.chmod(0o755)
        self.assertRaises(ValueError, module.SessionGuard, self.runtime, BOOT)
        self.runtime.chmod(0o700)
        self.guard.uid += 1
        self.assertRaises(ValueError, self.guard.admit)

    def test_actual_runner_orders_pending_before_spawn_and_retire_after_audit(self):
        tree = ast.parse((HERE / 'runner.py').read_text())
        launch = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'launch_supervised')
        calls = [node for node in ast.walk(launch) if isinstance(node, ast.Call)]
        begin = next(node for node in calls if ast.unparse(node.func) == 'boot_guard.begin')
        spawn = next(node for node in calls if ast.unparse(node.func) == 'subprocess.Popen')
        audit = next(node for node in calls if ast.unparse(node.func) == 'audit_session')
        retire = next(node for node in calls if ast.unparse(node.func) == 'boot_guard.retire')
        self.assertLess(begin.lineno, spawn.lineno)
        self.assertLess(audit.lineno, retire.lineno)


if __name__ == '__main__':
    unittest.main()
