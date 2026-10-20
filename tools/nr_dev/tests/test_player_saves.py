"""CPU-only transaction checks using real private temporary save directories."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('player_saves_test', HERE / 'player_saves.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
PASS = {'status': 'PASS_NORMAL_TEARDOWN', 'returncode': 0, 'native_lastuse_proof': True,
        'residuals': [], 'watchdog_expired': False, 'supervisor_interrupted': False}


class Saves(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.user = self.root / 'user'; self.user.mkdir()
        self.source = self.user / 'savedata'; self.source.mkdir()
        (self.source / 'slot').write_text('old')
        self.profile = self.root / 'private'

    def prepare(self, tx):
        metadata = tx.prepare(self.profile)
        self.assertEqual(metadata['source'], str(self.source))
        self.assertEqual(len(metadata['source_inventory_hash']), 64)
        (self.profile / 'savedata/slot').write_text('new')

    def test_normal_sync_preserves_safety_backup_and_private_copy(self):
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            result = tx.commit(self.profile, PASS)
        self.assertEqual(result['status'], 'synced', result)
        self.assertEqual((self.source / 'slot').read_text(), 'new')
        self.assertEqual((Path(result['backup']) / 'savedata/slot').read_text(), 'old')
        self.assertEqual((self.profile / 'savedata/slot').read_text(), 'new')
        with m.save_manager.session_lock(self.user):
            pass

    def test_nopeer_normal_is_accepted_by_exact_proof(self):
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            self.assertEqual(tx.commit(self.profile, PASS | {'peer_count': 0})['status'], 'synced')

    def test_hold_or_incomplete_proof_never_changes_source(self):
        changes = [{'status': 'HOLD'}, {'returncode': True}, {'returncode': 1},
                   {'native_lastuse_proof': False}, {'residuals': [12]},
                   {'watchdog_expired': True}, {'supervisor_interrupted': True}]
        for index, change in enumerate(changes):
            self.profile = self.root / f'private-{index}'
            with self.subTest(change=change), m.PlayerSaves(self.user) as tx:
                self.prepare(tx)
                self.assertEqual(tx.commit(self.profile, PASS | change)['status'], 'retained')
            self.assertEqual((self.source / 'slot').read_text(), 'old')

    def test_empty_and_missing_new_game_never_gets_seed(self):
        (self.source / 'slot').unlink()
        for missing in (False, True):
            if missing:
                self.source.rmdir()
            self.profile = self.root / f'private-{missing}'
            with m.PlayerSaves(self.user) as tx:
                metadata = tx.prepare(self.profile)
                self.assertEqual(metadata['source_files'], {})
                self.assertEqual(list((self.profile / 'savedata').iterdir()), [])
                (self.profile / 'savedata/slot').write_text('first')
                self.assertEqual(tx.commit(self.profile, PASS)['status'], 'synced')
            (self.source / 'slot').unlink()

    def test_symlinks_special_files_and_noncanonical_user_refused(self):
        outside = self.root / 'outside'; outside.write_text('outside')
        (self.source / 'link').symlink_to(outside)
        with self.assertRaises(ValueError):
            with m.PlayerSaves(self.user):
                pass
        (self.source / 'link').unlink()
        import os
        os.mkfifo(self.source / 'fifo')
        with self.assertRaises(ValueError):
            with m.PlayerSaves(self.user):
                pass
        (self.source / 'fifo').unlink()
        alias = self.root / 'alias'; alias.symlink_to(self.user)
        self.assertRaises(ValueError, m.PlayerSaves, alias)

    def test_private_symlink_and_external_source_mutation_refused(self):
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            (self.source / 'slot').write_text('external')
            result = tx.commit(self.profile, PASS)
        self.assertEqual(result['status'], 'error')
        self.assertEqual((self.source / 'slot').read_text(), 'external')
        self.profile = self.root / 'private-two'
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            (self.profile / 'savedata/slot').unlink()
            (self.profile / 'savedata/slot').symlink_to(self.source / 'slot')
            self.assertEqual(tx.commit(self.profile, PASS)['status'], 'error')

    def test_copy_and_stage_failures_leave_original(self):
        for name in ('copytree', 'mkdtemp'):
            self.profile = self.root / name
            with m.PlayerSaves(self.user) as tx:
                self.prepare(tx)
                owner = m.shutil if name == 'copytree' else m.tempfile
                with patch.object(owner, name, side_effect=OSError('injected')):
                    result = tx.commit(self.profile, PASS)
                self.assertEqual(result['status'], 'error')
            self.assertEqual((self.source / 'slot').read_text(), 'old')

    def test_failed_swap_rolls_back(self):
        real_replace = m.os.replace
        def replace(source, target):
            if Path(source).parent.name.startswith('.nrbridge-sync-'):
                raise OSError('injected swap')
            return real_replace(source, target)
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            with patch.object(m.os, 'replace', side_effect=replace):
                result = tx.commit(self.profile, PASS)
        self.assertEqual(result['status'], 'error')
        self.assertEqual((self.source / 'slot').read_text(), 'old')
        self.assertFalse((self.user / '.savedata-previous').exists())

    def test_shared_lock_queues_restore_and_consumes_after_sync(self):
        snapshot = m.save_manager.backup(self.user, reason='manual')
        (self.source / 'slot').write_text('before')
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            self.assertRaises(BlockingIOError, m.save_manager.session_lock(self.user).__enter__)
            answer = m.save_manager.request_restore(self.user, snapshot.name)
            self.assertEqual(answer, 'queued until the game exits')
            self.assertEqual((self.source / 'slot').read_text(), 'before')
            self.assertEqual(tx.commit(self.profile, PASS)['status'], 'synced')
        self.assertEqual((self.source / 'slot').read_text(), 'old')
        self.assertFalse((self.user / '.restore-request.json').exists())

    def test_pending_restore_is_applied_before_prepare(self):
        snapshot = m.save_manager.backup(self.user, reason='manual')
        (self.source / 'slot').write_text('later')
        m.save_manager.atomic_json(self.user / '.restore-request.json', {'snapshot': snapshot.name})
        with m.PlayerSaves(self.user) as tx:
            tx.prepare(self.profile)
            self.assertEqual((self.profile / 'savedata/slot').read_text(), 'old')

    def test_missing_canonical_user_can_start_new_game(self):
        self.user = self.root / 'new-user'
        with m.PlayerSaves(self.user) as tx:
            tx.prepare(self.profile)
            self.assertEqual(list((self.profile / 'savedata').iterdir()), [])
            self.assertEqual(tx.commit(self.profile, PASS)['status'], 'synced')
        self.assertTrue((self.user / 'savedata').is_dir())

    def test_backup_failure_and_double_swap_failure_keep_original_recoverable(self):
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            with patch.object(m.save_manager, 'backup', side_effect=OSError('backup failed')):
                result = tx.commit(self.profile, PASS)
            self.assertEqual(result['status'], 'error')
            self.assertEqual((self.source / 'slot').read_text(), 'old')
            real_replace = m.os.replace
            def replace(source, target):
                path = Path(source)
                if path.parent.name.startswith('.nrbridge-sync-') or path.name == '.savedata-previous':
                    raise OSError('swap/rollback failed')
                return real_replace(source, target)
            with patch.object(m.os, 'replace', side_effect=replace):
                result = tx.commit(self.profile, PASS)
            self.assertEqual(result['status'], 'error')
            self.assertIn('rollback failed', result['error'])
            self.assertEqual((self.user / '.savedata-previous/slot').read_text(), 'old')
        # Original helper recovery on next open restores the original before copying.
        with m.PlayerSaves(self.user):
            self.assertEqual((self.source / 'slot').read_text(), 'old')

    def test_limits_directory_mutation_and_entry_exception_unlock(self):
        with patch.object(m, 'MAX_BYTES', 1), self.assertRaises(ValueError):
            with m.PlayerSaves(self.user):
                pass
        with patch.object(m, 'MAX_ENTRIES', 0), self.assertRaises(ValueError):
            with m.PlayerSaves(self.user):
                pass
        with m.PlayerSaves(self.user) as tx:
            self.prepare(tx)
            (self.source / 'external-empty-dir').mkdir()
            self.assertEqual(tx.commit(self.profile, PASS)['status'], 'error')
        with self.assertRaisesRegex(RuntimeError, 'body'):
            with m.PlayerSaves(self.user):
                raise RuntimeError('body')
        with m.save_manager.session_lock(self.user):
            with self.assertRaises(BlockingIOError):
                with m.PlayerSaves(self.user):
                    pass
        with m.PlayerSaves(self.user):
            pass

    def test_prepare_detects_copy_time_mutation(self):
        original = m.shutil.copytree
        with m.PlayerSaves(self.user) as tx:
            def copy(*args, **kwargs):
                answer = original(*args, **kwargs)
                (self.source / 'slot').write_text('raced')
                return answer
            with patch.object(m.shutil, 'copytree', side_effect=copy):
                self.assertRaisesRegex(ValueError, 'stability', tx.prepare, self.profile)


if __name__ == '__main__':
    unittest.main()
