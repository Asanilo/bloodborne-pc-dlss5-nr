"""Public play entry request binding; all process creation is forbidden in tests."""
import contextlib
import copy
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location('public_play_entry', HERE / 'play.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
import context_recovery as c


class PublicPlayEntry(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build/baselines' / ('a' * 40)
        self.helper = self.root / 'build/helper-runtime-model/new/wine_runtime_model.exe'
        self.helper_build = {'path': str(self.helper.parent / 'build.json'), 'sha256': 'b' * 64,
                             'artifact_sha256': 'c' * 64}
        self.binding = {'path': str(self.root / 'localinputs/runtime.json'),
                        'config': {'helper': {'path': str(self.helper)}},
                        'helper_build': self.helper_build}
        old = '11111111-1111-4111-8111-111111111111'
        self.snapshot = {'old-hold': {'boot_id': old, 'files': {'result.json': 'd' * 64}}}
        self.record = {
            'schema': 'nr-isolated-context-v2', 'session': 'next-private',
            'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
            'historical_boot_ids': [old], 'previous_boot_id': old,
            'historical_faults_resolved': False, 'authorization_basis': 'existing bounded context',
            'held_runs': self.snapshot, 'producer_context': True, 'cold_trace': True,
            'request': {'mode': 'resident', 'sky': 'persistent', 'seconds': 240,
                        'watchdog_seconds': 360, 'source_limit': 7200,
                        'history_diagnostic': False, 'continue_session': None},
            'native_build': {'path': str(self.build)}, 'runtime_inputs': self.binding,
            'helper': {'path': str(self.helper), 'build_manifest': self.helper_build},
        }
        (self.root / 'inputs').mkdir()
        self.scope = self.root / 'inputs/context-next-private.json'
        self.write_record()
        self.mocks = {}
        for name, target, value in (
            ('root', (p.runner, 'ROOT'), self.root),
            ('helper_path', (p, 'DEFAULT_HELPER'), self.helper),
            ('native', (p.runner, 'checked_native_build'), (self.build, [], {})),
            ('legacy_helper', (p.runner, 'checked_runtime_helper'), (self.helper, self.helper_build)),
            ('local', (p.runner.runtime_inputs, 'load'), self.binding),
            ('capability', (p.runner.runtime_inputs, 'native_capability'), None),
            ('daily_capability', (p.runner.runtime_inputs, 'daily_capability'), None),
            ('holds', (p.runner, 'held_run_snapshot'), self.snapshot),
            ('identities', (c, '_identities'), None),
            ('predecessor', (c, '_predecessor'), None),
        ):
            if name in ('root', 'helper_path'):
                patcher = patch.object(*target, value)
            else:
                patcher = patch.object(*target, return_value=value)
            self.mocks[name] = patcher.start();self.addCleanup(patcher.stop)
        spawn = patch.object(p.subprocess, 'Popen', side_effect=AssertionError('CPU test must not spawn'))
        self.spawn = spawn.start();self.addCleanup(spawn.stop)

    def write_record(self):
        self.scope.write_text(json.dumps(self.record))

    def args(self, extra=()):
        return p.parse_args(['--session', 'next-private', '--native-build', str(self.build), *extra])

    def scoped_args(self, extra=()):
        return self.args(['--local-inputs', self.binding['path'], '--context-recovery', str(self.scope),
                          '--seconds', '240', '--watchdog-seconds', '360', '--source-limit', '7200',
                          '--pm4-cold-trace', '--pm4-producer-context', *extra])

    def test_existing_defaults_and_explicit_legacy_remain_compatible(self):
        for extra in ([], ['--legacy-inputs']):
            run, command = p.launch_command(self.args(extra))
            self.assertEqual(command[command.index('--seconds') + 1], '480')
            self.assertEqual(command[command.index('--watchdog-seconds') + 1], '600')
            self.assertEqual(command[command.index('--source-limit') + 1], '36000')
            self.assertIn('--runtime-model-helper', command)
            self.assertNotIn('--context-recovery', command)
            self.assertFalse(run.exists())
        self.mocks['identities'].assert_not_called()
        self.mocks['predecessor'].assert_not_called()
        self.spawn.assert_not_called()

    def test_explicit_local_configuration_never_selects_legacy_helper(self):
        _, command = p.launch_command(self.args(['--local-inputs', self.binding['path']]))
        self.assertIn('--local-inputs', command)
        self.assertNotIn('--runtime-model-helper', command)
        self.mocks['legacy_helper'].assert_not_called()
        self.mocks['capability'].assert_called_once_with(self.build)

    def test_gpu_validation_forwards_for_sr_only_and_keeps_legacy_fg_constraint(self):
        args = self.args(['--daily', '--sr-only', '--gpu-validation', '--local-inputs', self.binding['path']])
        with patch.object(p.runner, 'prepared_gpu_validation', return_value=self.root):
            _, command = p.launch_command(args)
        self.assertIn('--gpu-validation', command)
        self.assertIn('--sr-only', command)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            self.args(['--daily', '--sr-only', '--fg-validation', '--local-inputs', self.binding['path']])

    def test_explicit_validation_dependency_is_checked_before_launch(self):
        args = self.args(['--gpu-validation'])
        with patch.object(p.runner, 'WT', self.root), self.assertRaisesRegex(ValueError, 'dependency missing'):
            p.runner.prepared_gpu_validation(args)
        self.assertIsNone(p.runner.prepared_gpu_validation(self.args()))

    def test_daily_command_default_native_and_compatibility_are_explicit(self):
        for extra, native in (([], True), (['--joystick-mouse'], False)):
            args = self.args(['--daily', '--local-inputs', self.binding['path'], *extra])
            _, command = p.launch_command(args)
            self.assertIn('--daily', command)
            self.assertEqual('--native-mouse' in command, native)
            self.assertEqual('--joystick-mouse' in command, not native)
            for flag in ('--seconds', '--watchdog-seconds', '--source-limit'):
                self.assertEqual(command[command.index(flag) + 1], '0')
            self.assertNotIn('--context-recovery', command)
            self.assertNotIn('--pm4-cold-trace', command)
        self.spawn.assert_not_called()

    def test_daily_online_forwarding_never_puts_credentials_in_command(self):
        account = str(self.root / 'settings.json')
        args = self.args(['--daily','--local-inputs',self.binding['path'],
                          '--online-pool','thd','--online-account-config',account])
        _, command = p.launch_command(args)
        self.assertEqual(command[command.index('--online-pool')+1], 'thd')
        self.assertEqual(command[command.index('--online-account-config')+1], account)
        self.assertNotIn('--bbsc', command)
        self.spawn.assert_not_called()

    def test_matching_resident_continuation_scope_is_forwarded_exactly(self):
        original = self.scope.read_bytes()
        run, command = p.launch_command(self.scoped_args())
        for flag, value in (('--context-recovery', str(self.scope)), ('--source-limit', '7200'),
                            ('--watchdog-seconds', '360'), ('--seconds', '240')):
            self.assertEqual(command[command.index(flag) + 1], value)
        self.assertIn('--pm4-cold-trace', command)
        self.assertIn('--pm4-producer-context', command)
        self.mocks['identities'].assert_called_once()
        self.mocks['predecessor'].assert_called_once()
        self.assertEqual(self.scope.read_bytes(), original)
        self.assertFalse(run.exists())
        self.spawn.assert_not_called()

    def test_request_and_session_drift_rejected_before_spawn(self):
        for extra in (['--seconds', '239'], ['--source-limit', '120'],
                      ['--watchdog-seconds', '359'], ['--session', 'another'],
                      ['--continue-session', 'old-private']):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                p.launch_command(self.scoped_args(extra))
        self.spawn.assert_not_called()
        self.mocks['native'].assert_not_called()

    def test_main_rejects_mismatched_scope_before_any_process_creation(self):
        args = self.scoped_args(['--seconds', '239'])
        with patch.object(p, 'parse_args', return_value=args):
            self.assertRaisesRegex(ValueError, 'request drift', p.main)
        self.spawn.assert_not_called()
        self.assertFalse((self.root / 'runs/next-private').exists())

    def test_bad_boot_new_hold_and_resolved_historical_scope_rejected(self):
        original = copy.deepcopy(self.record)
        for changes in ({'boot_id': self.record['previous_boot_id']}, {'held_runs': {}},
                        {'historical_faults_resolved': True}, {'schema': 'unknown'}):
            self.record = original | changes;self.write_record()
            self.assertRaises(ValueError, p.launch_command, self.scoped_args())
        self.spawn.assert_not_called()

    def test_trace_flags_must_be_explicit_and_match_scope(self):
        args = self.scoped_args();args.pm4_cold_trace = False
        self.assertRaisesRegex(ValueError, 'trace request drift', p.launch_command, args)
        args = self.scoped_args();args.pm4_producer_context = False
        self.assertRaisesRegex(ValueError, 'trace request drift', p.launch_command, args)
        self.mocks['native'].assert_not_called()

    def test_selected_native_helper_or_local_binding_drift_rejected(self):
        original = copy.deepcopy(self.record)
        for changes in ({'native_build': {'path': '/different/build'}},
                        {'runtime_inputs': None}, {'helper': {'path': '/wrong.exe', 'build_manifest': self.helper_build}}):
            self.record = original | changes;self.write_record()
            self.assertRaisesRegex(ValueError, 'build/runtime inputs drift', p.launch_command, self.scoped_args())
        self.spawn.assert_not_called()

    def test_identity_and_real_predecessor_rejection_happen_before_spawn(self):
        for name in ('identities', 'predecessor'):
            mock = self.mocks[name];mock.side_effect = ValueError('actual evidence drift')
            self.assertRaisesRegex(ValueError, 'actual evidence drift', p.launch_command, self.scoped_args())
            mock.side_effect = None
        self.spawn.assert_not_called()
        self.assertFalse((self.root / 'runs/next-private').exists())

    def test_consumed_session_and_noncanonical_scope_rejected(self):
        run = self.root / 'runs/next-private';run.mkdir(parents=True)
        self.assertRaisesRegex(ValueError, 'already exists', p.launch_command, self.scoped_args())
        run.rmdir()
        linked = self.root / 'inputs/linked.json';linked.symlink_to(self.scope)
        self.assertRaisesRegex(ValueError, 'canonical regular', p.launch_command,
                               self.scoped_args(['--context-recovery', str(linked)]))
        self.spawn.assert_not_called()

    def test_parameter_limits_and_mutually_exclusive_inputs_rejected(self):
        for extra in (['--seconds', '600'], ['--watchdog-seconds', '601'],
                      ['--watchdog-seconds', '480'], ['--source-limit', '0'],
                      ['--source-limit', '216001'], ['--pm4-producer-context'],
                      ['--legacy-inputs', '--local-inputs', self.binding['path']]):
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.args(extra)
        self.spawn.assert_not_called()


if __name__ == '__main__':
    unittest.main()
