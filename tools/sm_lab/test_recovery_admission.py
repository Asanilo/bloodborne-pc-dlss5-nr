#!/usr/bin/env python3
"""Offline checks for the boot-receipt recovery admission.

A current-boot pending/HOLD receipt stays HOLD; a separate recovery record (a
passing submit probe plus a clean journal) is the only thing that lets a new
daily session start. Nothing here needs a GPU: the probe record is injected.
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

WT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(WT / 'tools/nr_dev'))

import daily_session_guard as guard_mod  # noqa: E402

BOOT = '11111111-2222-4333-8444-555555555555'
PASS_RESULT = {'status': 'PASS_NORMAL_TEARDOWN', 'returncode': 0,
               'native_lastuse_proof': True, 'residuals': [],
               'watchdog_expired': False, 'supervisor_interrupted': False}
PROBE = {'tool': '/tmp/bb-probe', 'sha256': 'a' * 64, 'result': 'PASS',
         'output_sha256': 'b' * 64}


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.guard = guard_mod.SessionGuard(runtime_root=self.root, boot_id=BOOT)
        self.home = self.root / 'home'
        self.receipt = self.guard.begin(self.home, 'failed-session')

    def tearDown(self):
        self.tmp.cleanup()

    def hold(self):
        self.guard.hold(self.receipt)
        return self.guard.read()

    def test_hold_blocks_without_recovery(self):
        self.hold()
        with self.assertRaisesRegex(ValueError, 'pending/HOLD'):
            self.guard.admit()

    def test_recovery_admits(self):
        receipt = self.hold()
        path = self.guard.write_recovery(receipt, PROBE)
        self.assertTrue(path.is_file())
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.guard.admit(), receipt)
        # The failed receipt itself is unchanged.
        self.assertEqual(self.guard.read(), receipt)

    def test_other_session_recovery_does_not_admit(self):
        receipt = self.hold()
        self.guard.write_recovery(receipt, PROBE)
        other = {'schema': guard_mod.SCHEMA, 'uid': receipt['uid'], 'boot_id': BOOT,
                 'state': 'hold', 'root': str(self.home), 'session': 'another-session',
                 'nonce': 'c' * 32}
        self.guard._write(other)
        with self.assertRaisesRegex(ValueError, 'pending/HOLD'):
            self.guard.admit()

    def test_failed_probe_cannot_be_recorded(self):
        receipt = self.hold()
        bad = dict(PROBE, result='FAIL')
        with self.assertRaises(ValueError):
            self.guard.write_recovery(receipt, bad)

    def test_recovery_requires_current_hold(self):
        self.guard.retire(self.receipt, PASS_RESULT)
        with self.assertRaises(ValueError):
            self.guard.write_recovery(self.guard.read(), PROBE)

    def test_symlinked_recovery_refused(self):
        self.hold()
        target = self.root / 'elsewhere.json'
        target.write_text('{}')
        (self.root / guard_mod.RECOVERY_NAME).symlink_to(target)
        with self.assertRaisesRegex(ValueError, 'unreadable or unsafe|symlink'):
            self.guard.read_recovery()

    def test_retired_receipt_admits_without_recovery(self):
        self.guard.retire(self.receipt, PASS_RESULT)
        self.assertIsNotNone(self.guard.admit())


if __name__ == '__main__':
    unittest.main()
