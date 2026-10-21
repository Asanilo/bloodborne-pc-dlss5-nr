"""Actual bounded request reads; no game, GPU or Helper processes."""
import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('scene_ui_start', HERE / 'ui_start.py')
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


class RequestFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.run = self.root / 'runs/test-scene'
        self.run.mkdir(parents=True)
        self.build = self.root / 'build'
        (self.build / 'gpu').mkdir(parents=True)
        (self.build / 'gpu/libbbgpu.so').write_bytes(b'ELF fixture\0' + ui.CAPABILITY)
        self.manifest = dict(session=self.run.name, daily=True, nr_enabled=True,
            native_build=str(self.build), environment={
                'BB_NR_DAILY': '1', 'BB_NR_NATIVE_ASYNC_RUN_DIR': str(self.run),
                'BB_NR_NATIVE_ASYNC_SESSION': self.run.name, ui.ENVIRONMENT: str(self.run / ui.NAME)})
        self.path = self.run / ui.NAME
        self.token = (self.run.name + '\n').encode()

    def write(self, data=None):
        self.path.write_bytes(self.token if data is None else data)
        self.path.chmod(0o600)


class RequestTests(RequestFixture):
    def test_absent_and_completed_request(self):
        self.assertFalse(ui.requested(self.run, self.manifest))
        self.write()
        self.assertTrue(ui.requested(self.run, self.manifest))

    def test_incomplete_never_grants_start(self):
        for data in (b'', b'test-', self.token[:-1]):
            self.write(data)
            self.assertFalse(ui.requested(self.run, self.manifest))

    def test_wrong_session_extra_bytes_and_oversized_refused(self):
        for data in (b'other-session\n', self.token + b'\n', b'x' * 100000):
            self.write(data)
            with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)

    def test_symlink_and_hardlink_refused(self):
        other = self.root / 'token'
        other.write_bytes(self.token)
        other.chmod(0o600)
        self.path.symlink_to(other)
        with self.assertRaises(OSError):ui.requested(self.run, self.manifest)
        self.path.unlink()
        os.link(other, self.path)
        with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)

    def test_fifo_and_directory_are_not_read(self):
        os.mkfifo(self.path, 0o600)
        with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)
        self.path.unlink()
        self.path.mkdir()
        with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)

    def test_mode_owner_and_session_scope_refused(self):
        self.write()
        self.path.chmod(0o644)
        with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)
        self.path.chmod(0o600)
        with patch.object(ui.os, 'getuid', return_value=os.getuid() + 1):
            with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)
        for key, value in (('daily', False), ('daily', 1), ('nr_enabled', False), ('nr_enabled', 1),
                           ('session', 'other')):
            manifest = self.manifest | {key: value}
            with self.assertRaises(ValueError):ui.requested(self.run, manifest)

    def test_environment_scope_and_native_capability_refused(self):
        for key in self.manifest['environment']:
            manifest = copy.deepcopy(self.manifest)
            manifest['environment'][key] = '/not-the-scope'
            with self.assertRaises(ValueError):ui.requested(self.run, manifest)
        (self.build / 'gpu/libbbgpu.so').write_bytes(b'old Native')
        with self.assertRaises(ValueError):ui.requested(self.run, self.manifest)

    def test_only_new_daily_nr_build_gets_capability(self):
        for daily, sr_only, expected in ((True, False, True), (True, True, False), (False, False, False)):
            self.assertEqual(ui.enabled(argparse.Namespace(daily=daily, sr_only=sr_only), self.build), expected)
        self.assertFalse(ui.capable(self.root / 'missing'))


class CoordinatorTests(RequestFixture):
    def setUp(self):
        super().setUp()
        import sys
        sys.path.insert(0, str(HERE))
        import play
        self.play = play
        self.args = argparse.Namespace(ingame_nr_start=True)
        self.child = Mock()
        self.child.poll.return_value = None
        (self.run / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_ui_confirmation_needs_bound_manifest_and_live_native(self):
        self.write()
        with patch.object(self.play.runner, 'validate_daily_manifest') as validate, \
             patch.object(self.play.runner, 'current_game', return_value={'pid': 123}) as game:
            self.assertEqual(self.play.wait_for_scene(self.args, self.run, self.child), 'game')
            validate.assert_called_once_with(self.run, self.manifest)
            game.assert_called_once_with(self.run)

    def test_live_native_rejection_never_reaches_helper_control(self):
        self.write()
        with patch.object(self.play.runner, 'validate_daily_manifest'), \
             patch.object(self.play.runner, 'current_game', side_effect=ValueError('identity drift')), \
             patch.object(self.play.runner, 'control') as control:
            with self.assertRaisesRegex(ValueError, 'identity drift'):
                self.play.wait_for_scene(self.args, self.run, self.child)
            control.assert_not_called()

    def test_closed_stdin_keeps_ui_waiting_until_game_exits(self):
        stream = Mock()
        stream.readline.return_value = ''
        self.child.poll.side_effect = [None, None, 0]
        with patch.object(self.play.select, 'select', return_value=([stream], [], [])), \
             patch.object(self.play.time, 'sleep'):
            self.assertIsNone(self.play.wait_for_scene(self.args, self.run, self.child, stream))
        stream.readline.assert_called_once()

    def test_legacy_launcher_still_confirms_and_eof_still_closes(self):
        args = argparse.Namespace(ingame_nr_start=False)
        stream = Mock()
        with patch.object(self.play.select, 'select', return_value=([stream], [], [])):
            stream.readline.return_value = '\n'
            self.assertEqual(self.play.wait_for_scene(args, self.run, self.child, stream), 'launcher')
            stream.readline.return_value = ''
            with self.assertRaises(EOFError):self.play.wait_for_scene(args, self.run, self.child, stream)


if __name__ == '__main__':
    unittest.main()
