"""Daily admission, real command dispatch and control boundaries; never launches a game."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location('daily_test_runner', HERE / 'runner.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class DailyRunner(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.addCleanup(patch.stopall)
        patch.object(r, 'ROOT', self.root).start()
        runtime = self.root / 'runtime'; runtime.mkdir(mode=0o700)
        self.guard = r.daily_session_guard.SessionGuard(runtime_root=runtime)
        patch.object(r.daily_session_guard, 'SessionGuard', return_value=self.guard).start()
        self.inputs = str(self.root / 'localinputs/version.json')

    def args(self, *extra):
        return r.parse_args(['start', 'daily-test', '--daily', '--local-inputs', self.inputs, *extra])

    def test_default_daily_native_and_explicit_compatibility(self):
        native = self.args()
        self.assertTrue(native.native_mouse)
        self.assertEqual((native.seconds, native.watchdog_seconds, native.source_limit), (0, 0, 0))
        self.assertFalse(self.args('--joystick-mouse').native_mouse)
        legacy = r.parse_args(['start', 'diagnostic'])
        self.assertEqual((legacy.seconds, legacy.watchdog_seconds, legacy.source_limit), (480, 600, 36000))
        self.assertFalse(legacy.native_mouse)

    def test_diagnostic_or_unbound_daily_rejected_before_setup(self):
        bad = [ ['--context-recovery', '/scope'], ['--reboot-diagnostic', '/scope'],
                ['--pm4-cold-trace'], ['--pm4-producer-context'], ['--history-diagnostic'],
                ['--native-mouse', '--joystick-mouse'], ['--close-grace', '0'] ]
        for extra in bad:
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.args(*extra)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            r.parse_args(['preflight', '--daily'])
        args = self.args(); args.local_inputs = None
        with patch.object(r, 'checked_run') as checked:
            self.assertRaisesRegex(ValueError, 'explicit versioned', r.launch_supervised, args, None)
            checked.assert_not_called()

    def test_unbounded_total_play_retains_independent_close_grace(self):
        schedule = r.SessionSchedule(10, 0, 0, 120, daily=True)
        for now in (10, 601, 10**12):
            self.assertFalse(schedule.close_due(now))
            self.assertFalse(schedule.expired(now))
        schedule.note_close(10**12)
        schedule.note_close(10**12 + 100)  # A repeated request never extends grace.
        self.assertFalse(schedule.expired(10**12 + 119))
        self.assertTrue(schedule.expired(10**12 + 120))
        self.assertRaises(ValueError, r.SessionSchedule, 0, 1, 0, 120, True)

    def test_main_daily_preflight_uses_daily_admission_under_lock(self):
        args = r.parse_args(['preflight', '--daily', '--local-inputs', self.inputs])
        with patch.object(r, 'parse_args', return_value=args), patch.object(r.runtime_inputs, 'load', return_value={'bound': True}), patch.object(r, 'daily_preflight', return_value={'daily': True}) as daily, patch.object(r, 'preflight') as legacy, patch.object(r.fcntl,'flock'), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(r.main(), 0)
            daily.assert_called_once()
            self.assertEqual(daily.call_args.args[1], {'bound': True})
            legacy.assert_not_called()

    def preflight(self, snapshot, journal=None):
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        journal = journal or json.dumps({'_BOOT_ID': boot.replace('-', ''), 'MESSAGE': 'CPU fixture'})
        with patch.object(r.os, 'readlink', return_value='/tmp/nrbridge-poc-gpu.lock'), patch.object(r.fcntl, 'flock', side_effect=[None, BlockingIOError()]), patch.object(r, 'holders', return_value=[]), patch.object(r, 'held_run_snapshot', return_value=snapshot), patch.object(r, 'verify_runtime_inputs') as verify, patch.object(r.subprocess, 'check_output', return_value=journal):
            result = r.daily_preflight(MagicMock(), {'bound': True})
            verify.assert_called_once_with({'bound': True})
            return result

    def test_current_boot_hold_or_kernel_error_refused_old_hold_preserved(self):
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        snapshot = {'historical': {'boot_id': 'previous-boot'}}
        original = json.dumps(snapshot)
        self.assertFalse(self.preflight(snapshot)['historical_faults_resolved'])
        self.assertEqual(json.dumps(snapshot), original)
        self.assertRaisesRegex(ValueError, 'current-boot HOLD', self.preflight, {'live': {'boot_id': boot}})
        bad = json.dumps({'_BOOT_ID': boot.replace('-', ''), 'MESSAGE': 'NVRM: Xid GPU failure'})
        self.assertRaisesRegex(ValueError, 'GPU driver error', self.preflight, snapshot, bad)

    def test_global_pending_other_home_blocks_actual_daily_preflight(self):
        self.guard.begin(self.root / 'other-home', 'other-session')
        original = self.guard.path.read_bytes()
        self.assertRaisesRegex(ValueError, 'global session pending/HOLD', self.preflight, {})
        self.assertEqual(self.guard.path.read_bytes(), original)

    def test_global_pending_blocks_finite_and_both_recovery_scopes_before_branch(self):
        receipt = self.guard.begin(self.root / 'other-home', 'other-session')
        original = self.guard.path.read_bytes()
        recovery = [None, argparse.Namespace(context_recovery='/scope'),
                    argparse.Namespace(context_recovery=None, reboot_diagnostic='/scope')]
        with patch.object(r,'holders') as holders, patch.object(r,'reboot_diagnostic_preflight') as reboot, patch('context_recovery.check_launch') as isolated:
            for request in recovery:
                with self.subTest(request=request), self.assertRaisesRegex(ValueError,'global session pending/HOLD'):
                    r.preflight(MagicMock(), request)
            holders.assert_not_called(); reboot.assert_not_called(); isolated.assert_not_called()
        self.assertEqual(self.guard.path.read_bytes(), original)
        self.guard.hold(receipt)
        with patch('context_recovery.check_launch') as isolated:
            self.assertRaisesRegex(ValueError,'global session pending/HOLD',r.preflight,MagicMock(),recovery[1])
            isolated.assert_not_called()

    def test_finite_recovery_may_proceed_only_after_boot_changes(self):
        receipt = self.guard.begin(self.root / 'other-home', 'other-session')
        previous_boot = '11111111-1111-4111-8111-111111111111'
        receipt['boot_id'] = previous_boot
        self.guard.path.write_text(json.dumps(receipt)); self.guard.path.chmod(0o600)
        request = argparse.Namespace(context_recovery='/scope')
        with patch.object(r,'holders',return_value=[]), patch('context_recovery.check_launch',return_value={'scope':'CPU'}) as isolated:
            self.assertEqual(r.preflight(MagicMock(),request),{'scope':'CPU'})
            isolated.assert_called_once()
        self.assertEqual(self.guard.read()['state'],'pending')

    def test_current_boot_daily_missing_result_cannot_disappear_from_admission(self):
        run = self.root / 'runs/interrupted'; run.mkdir(parents=True)
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        (run / 'manifest.json').write_text(json.dumps({'daily': True, 'host_safety': {'boot_id': boot}}))
        self.assertRaisesRegex(ValueError, 'retirement not proved', self.preflight, {})
        r.record_pending_daily_retirement(run)
        result = json.loads((run / 'result.json').read_text())
        self.assertEqual(result['status'], 'HOLD')
        self.assertFalse(result['native_lastuse_proof'])
        self.assertRaisesRegex(ValueError, 'retirement not proved', self.preflight, {})

    def test_daily_resource_failure_or_capture_error_requests_hold(self):
        capture = argparse.Namespace(state={'failure_latched': False, 'error': None})
        self.assertIsNone(r.daily_failure_reason(self.root, capture))
        capture.state['failure_latched'] = True
        self.assertIn('resource/GPU failure', r.daily_failure_reason(self.root, capture))
        capture.state.update(failure_latched=False, error='storage failure')
        self.assertIn('evidence incomplete', r.daily_failure_reason(self.root, capture))
        capture.state['error'] = None
        (self.root / 'native.abort').write_text('unknown GPU state')
        self.assertIn('quarantined', r.daily_failure_reason(self.root, capture))

    def test_portable_root_rejects_relative_symlink_and_shared_home(self):
        linked = self.root / 'linked'; linked.symlink_to(self.root)
        for value in ('relative', str(self.root / '..'), str(linked), '/', str(Path.home()), str(self.root) + '\n'):
            with self.subTest(value=value), patch.dict(os.environ, NRBRIDGE_HOME=value):
                self.assertRaises(ValueError, r.workspace_root)
        with patch.dict(os.environ, NRBRIDGE_HOME=str(self.root)):
            self.assertEqual(r.workspace_root(), self.root)

    def test_daily_control_rechecks_runtime_scene_and_gpu_readiness(self):
        run = self.root / 'runs/daily-test'; run.mkdir(parents=True)
        (run / 'profile/user').mkdir(parents=True)
        binding = {'bound': True}; helper = run / 'helper-runtime.json'; helper.write_text('{}')
        env = {'BB_NR_DAILY': '1', 'NRBRIDGE_HOME': str(self.root), 'BB_NATIVE_MOUSE': '1','BB_FRAME_STATS':'0',
               'BB_NR_NATIVE_ASYNC_SOURCE_LIMIT': '0', 'BB_DATA_DIR': str(run / 'profile'),
               'BB_USER_DIR': str(run / 'profile/user'), 'BB_GPU_USER_DIR': str(run / 'profile/user')}
        manifest = {'schema': 'nr-dev-session-v1', 'session': run.name, 'daily': True,
                    'native_build': str(self.root / 'build/native'),
                    'planned_seconds': 0, 'watchdog_seconds': 0, 'runtime_inputs': binding,
                    'environment': env, 'host_safety': {'daily': True},
                    'helper_runtime_config': {'path': str(helper), 'sha256': r.sha(helper)}}
        (run / 'manifest.json').write_text(json.dumps(manifest))
        game = {'pid': 123, 'start_ticks': 456}
        with patch.object(r, 'current_game', return_value=game), patch.object(r.runtime_inputs, 'recheck') as recheck, patch.object(r.runtime_inputs, 'daily_capability'), patch.object(r.runtime_inputs, 'helper_config', return_value={}), patch('context_recovery.check_control', side_effect=AssertionError('diagnostic scope reached')):
            args = argparse.Namespace(session=run.name, command='start-helper')
            self.assertRaises(FileNotFoundError, r.control, args)
            self.assertFalse((run / 'native-helper-start').exists())
            (run / 'scene-ready.json').write_text(json.dumps({'game': game}))
            r.control(args)
            self.assertEqual((run / 'native-helper-start').read_text(), run.name + '\n')
            args.command = 'arm'
            self.assertRaises(FileNotFoundError, r.control, args)
            ready = run / ('async-fd-retired-' + run.name + '-g0.json')
            ready.write_text(json.dumps({'session': run.name, 'status': 'FD_RETIRED', 'completed_GPU': False, 'export_raw_fd_owned': 0}))
            self.assertRaisesRegex(ValueError, 'actual GPU', r.control, args)
            ready.write_text(ready.read_text().replace('false', 'true'))
            r.control(args)
            self.assertTrue((run / 'native-scene-arm').exists())
            recheck.side_effect = ValueError('pins drift')
            self.assertRaisesRegex(ValueError, 'pins drift', r.control, args)


if __name__ == '__main__':
    unittest.main()
