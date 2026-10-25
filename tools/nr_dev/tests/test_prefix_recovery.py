"""Real directory moves under fake boot UUIDs; never a GPU retirement test."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prefix_recovery as recovery

OLD = '11111111-1111-4111-8111-111111111111'
NEW = '22222222-2222-4222-8222-222222222222'


class PrefixRecovery(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.run = self.root / 'runs/failed'
        self.source = self.root / 'runs/original/profile/wine-prefix'
        self.source.parent.mkdir(parents=True)
        self.target = self.run / 'profile/wine-prefix'; self.target.mkdir(parents=True)
        (self.target / 'system.reg').write_text('private CPU prefix')
        self.put(self.run / 'result.json', dict(status='HOLD', native_lastuse_proof=False))
        self.put(self.run / 'manifest.json', dict(session='failed', host_safety=dict(boot_id=OLD)))
        self.prior = dict(status='PASS_NORMAL_TEARDOWN', native_lastuse_proof=True, residuals=[],
                          returncode=0, watchdog_expired=False, supervisor_interrupted=False, received_signals=0)
        self.put(self.source.parent.parent / 'result.json', self.prior)
        self.put(self.run / 'nr-prefix-lease.json', dict(source=str(self.source), target=str(self.target), state='RETAINED_UNPROVED'))

    def put(self, path, value):
        path.write_text(json.dumps(value))

    def test_same_boot_refused_without_altering_any_evidence(self):
        before = {p: p.read_bytes() for p in self.run.rglob('*') if p.is_file()}
        self.assertRaises(ValueError, recovery.recover, self.root, self.run, OLD, lambda: True)
        self.assertEqual(before, {p: p.read_bytes() for p in self.run.rglob('*') if p.is_file()})

    def test_new_boot_reuses_directory_and_preserves_original_hold(self):
        original = (self.run / 'result.json').read_bytes(); inode = self.target.stat().st_ino
        report = recovery.recover(self.root, self.run, NEW, lambda: True)
        self.assertEqual(report['state'], 'RETURNED_AFTER_REBOOT')
        self.assertEqual(self.source.stat().st_ino, inode)
        self.assertEqual((self.run / 'result.json').read_bytes(), original)
        self.assertEqual(list(self.target.iterdir()), [])
        recovery.recover(self.root, self.run, NEW, lambda: True)  # idempotent

    def test_live_owner_or_recreated_source_prevents_reuse(self):
        self.assertRaises(ValueError, recovery.recover, self.root, self.run, NEW, lambda: False)
        self.source.mkdir()
        self.assertRaises(ValueError, recovery.recover, self.root, self.run, NEW, lambda: True)
        self.assertTrue((self.target / 'system.reg').exists())

    def test_interrupted_preparation_is_resumable_without_copying_prefix(self):
        with patch.object(recovery, 'atomic', side_effect=RuntimeError('interrupted')):
            self.assertRaises(RuntimeError, recovery.recover, self.root, self.run, NEW, lambda: True)
        self.assertTrue((self.target / 'system.reg').exists())
        report = recovery.recover(self.root, self.run, NEW, lambda: True)
        self.assertEqual(report['state'], 'RETURNED_AFTER_REBOOT')

    def test_original_source_must_have_strict_normal_exit_proof(self):
        for change in ({'returncode':True}, {'returncode':1}, {'watchdog_expired':True},
                       {'supervisor_interrupted':True}, {'received_signals':1}):
            with self.subTest(change=change):
                self.put(self.source.parent.parent / 'result.json', self.prior | change)
                self.assertRaises(ValueError, recovery.recover, self.root, self.run, NEW, lambda: True)
                self.assertTrue((self.target / 'system.reg').exists())

    def test_prepared_record_rejects_changed_lease_even_for_other_valid_source(self):
        with patch.object(Path, 'rename', side_effect=RuntimeError('interrupted')):
            self.assertRaises(RuntimeError, recovery.recover, self.root, self.run, NEW, lambda: True)
        other = self.root / 'runs/other/profile/wine-prefix'; other.parent.mkdir(parents=True)
        self.put(other.parent.parent / 'result.json', self.prior)
        archive = self.run / 'prefix-recovery-original-lease.json'
        lease = recovery.read(archive); lease['source'] = str(other); self.put(archive, lease)
        self.assertRaises(ValueError, recovery.recover, self.root, self.run, NEW, lambda: True)
        self.assertTrue((self.target / 'system.reg').exists())
        self.assertFalse(other.exists())

    def test_partial_rename_rejects_symlink_placeholder(self):
        real_atomic = recovery.atomic
        def stop(path, value):
            if path.name == 'nr-prefix-lease.json': raise RuntimeError('interrupted')
            real_atomic(path, value)
        with patch.object(recovery, 'atomic', side_effect=stop):
            self.assertRaises(RuntimeError, recovery.recover, self.root, self.run, NEW, lambda: True)
        self.target.rmdir(); empty = self.root / 'empty'; empty.mkdir(); self.target.symlink_to(empty)
        self.assertRaises(ValueError, recovery.recover, self.root, self.run, NEW, lambda: True)
        self.assertTrue((self.source / 'system.reg').exists())


if __name__ == '__main__':
    unittest.main()
