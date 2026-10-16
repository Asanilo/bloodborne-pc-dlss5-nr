import copy
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('context_recovery', Path(__file__).parents[1] / 'context_recovery.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class Scope(unittest.TestCase):
    def setUp(self):
        self.record = copy.deepcopy(c.EXAMPLE_RECORD)
        self.record.update(boot_id='new', previous_boot_id='old')
        self.snapshot = {'failure': {'boot_id': 'old', 'files': {'result.json': 'hash'}}}
        self.record['held_runs'] = copy.deepcopy(self.snapshot)
        self.req = copy.deepcopy(self.record['request'])

    def verify(self):
        c.validate_scope(self.record, self.req, 'new', self.snapshot)

    def test_valid_stage_keeps_historical_fault_open(self):
        original = copy.deepcopy(self.record)
        self.verify()
        self.assertEqual(self.record, original)
        for sky, mode in [('persistent', 'skipEvaluate'), ('persistent', 'resident')]:
            self.req.update(sky=sky, mode=mode)
            self.record['request'] = copy.deepcopy(self.req)
            self.verify()

    def test_new_hold_changed_hold_same_boot_rejected(self):
        for snapshot in [{}, {'new': {'boot_id': 'new'}},
                         {'failure': {'boot_id': 'old', 'files': {'result.json': 'changed'}}}]:
            with self.subTest(snapshot=snapshot), self.assertRaises(ValueError):
                c.validate_scope(self.record, self.req, 'new', snapshot)
        self.record['previous_boot_id'] = 'new'
        self.assertRaises(ValueError, self.verify)

    def test_resolution_and_schema_drift_rejected(self):
        for field, value in [('historical_faults_resolved', True), ('schema', 'nr-reboot-sr-diagnostic-v1'),
                             ('authorization_basis', '')]:
            original = self.record[field]
            self.record[field] = value
            self.assertRaises(ValueError, self.verify)
            self.record[field] = original

    def test_unbounded_wrong_stage_continuation_and_type_rejected(self):
        for key, value in [('seconds', 241), ('seconds', True), ('watchdog_seconds', 361),
                           ('source_limit', 36001), ('source_limit', 0), ('continue_session', 'old'),
                           ('mode', 'resident'), ('history_diagnostic', 1)]:
            with self.subTest(key=key, value=value):
                req = self.req | {key: value}
                record = self.record | {'request': req}
                self.assertRaises(ValueError, c.validate_scope, record, req, 'new', self.snapshot)

    def test_request_mismatch_rejected(self):
        self.req['source_limit'] -= 1
        self.assertRaises(ValueError, self.verify)


class FilesAndControls(unittest.TestCase):
    def test_symlink_and_escape_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / 'inputs').mkdir()
            (root / 'real.json').write_text('{}')
            (root / 'inputs/link.json').symlink_to(root / 'real.json')
            self.assertRaises(ValueError, c.regular, root / 'inputs/link.json', root / 'inputs')
            self.assertRaises(ValueError, c.regular, root / 'real.json', root / 'inputs')
            self.assertRaises(ValueError, c.regular, Path('relative.json'))

    def test_input_stop_are_never_blocked(self):
        for command in ('input', 'stop', 'focus', 'dump'):
            with patch.object(c, 'load', side_effect=AssertionError('must not read evidence')):
                c.check_control(None, None, None, command)

    def test_changed_recovery_rejected_before_control(self):
        with tempfile.TemporaryDirectory() as d:
            run = Path(d) / 'session'
            run.mkdir()
            manifest = {'host_safety': {'context_recovery': {'path': '/record', 'sha256': 'old'}}}
            (run / 'manifest.json').write_text(json.dumps(manifest))
            r = SimpleNamespace(sha=lambda p: 'changed')
            with patch.object(c, '_record', return_value=(Path('/record'), {})), \
                 patch.object(c, '_supervisor') as supervisor:
                self.assertRaises(ValueError, c.check_control, r, run, {}, 'arm')
                supervisor.assert_not_called()

    def test_common_always_rechecks_live_kernel(self):
        r = SimpleNamespace(held_run_snapshot=lambda: {})
        with patch.object(c, 'validate_scope'), patch.object(c, '_identities'), \
             patch.object(c, '_predecessor'), patch.object(c, '_kernel', side_effect=ValueError('GPU error')) as kernel:
            self.assertRaises(ValueError, c._common, r, {}, {})
            kernel.assert_called_once()

    def test_supervisor_requires_exact_live_identity_and_actual_write_flock(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            run = root / 'run'
            run.mkdir()
            identity = {'pid': 123, 'start_ticks': 456, 'exe': '/python'}
            (run / 'runner.json').write_text(json.dumps({'runner': identity}))
            proc = root / 'proc/123'
            (proc / 'fd').mkdir(parents=True)
            (proc / 'fdinfo').mkdir()
            (proc / 'fd/7').touch()
            info = proc / 'fdinfo/7'
            r = SimpleNamespace(live=lambda pid: identity)
            real_path = Path
            with patch.object(c, 'Path', side_effect=lambda p: root / 'proc' if p == '/proc' else real_path(p)), \
                 patch.object(c.os, 'readlink', return_value='/tmp/nrbridge-poc-gpu.lock'):
                info.write_text('lock:\t1: FLOCK ADVISORY WRITE 123 00:01:42 0 EOF\n')
                c._supervisor(r, run)
                for text in ['', 'lock: 1: POSIX ADVISORY WRITE 123 00:01:42 0 EOF\n',
                             'lock: 1: FLOCK ADVISORY READ 123 00:01:42 0 EOF\n',
                             'lock: 1: FLOCK ADVISORY WRITE 999 00:01:42 0 EOF\n']:
                    info.write_text(text)
                    self.assertRaises(ValueError, c._supervisor, r, run)
                r.live = lambda pid: identity | {'start_ticks': 457}
                self.assertRaises(ValueError, c._supervisor, r, run)

    def test_off_skip_does_not_accept_peer_or_failed_retirement(self):
        # Production auditor is always called; no cached retirement JSON decides admission.
        with patch.object(c, '_retirement', side_effect=ValueError('actual last use missing')) as audit:
            with tempfile.TemporaryDirectory() as d:
                run = Path(d)
                record = copy.deepcopy(c.EXAMPLE_RECORD)
                record.update(session='new-session', boot_id='boot')
                record['predecessor'] = {'session': 'old-session', 'files': {}}
                result = {'status': 'PASS_NORMAL_TEARDOWN', 'returncode': 0, 'native_lastuse_proof': True,
                          'residuals': [], 'watchdog_expired': False, 'supervisor_interrupted': False, 'received_signals': 0}
                manifest = {'session': 'old-session', 'host_safety': {'boot_id': 'boot'},
                            'sky': 'off', 'mode': 'skipEvaluate',
                            'native_build_provenance': record['native_build']['provenance'], 'artifacts': {}}
                observation = {'boot_id': 'boot', 'session': 'old-session', 'project_holders': [],
                               'driver_errors': [], 'result_sha256': 'hash', 'kernel_journal_sha256': 'hash'}
                for name, value in [('manifest.json', manifest), ('result.json', result),
                                    ('post-exit-observation.json', observation)]:
                    (run / name).write_text(json.dumps(value))
                (run / 'game.log').write_text('')
                (run / 'post-exit-kernel.jsonl').write_text('kernel')
                record['predecessor']['files'] = {p.name: 'hash' for p in run.iterdir()}
                r = SimpleNamespace(checked_run=lambda session: run, sha=lambda p: 'hash',
                                    check_kernel_journal=lambda text, boot: None)
                self.assertRaises(ValueError, c._predecessor, r, record)
                audit.assert_called_once_with(run)
                audit.side_effect = None
                audit.return_value = {'path': 'NO_PEER_NORMAL_EXIT'}
                c._predecessor(r, record)
                audit.return_value = {'path': 'PEER_NORMAL_RETIREMENT'}
                self.assertRaisesRegex(ValueError, 'SR-only predecessor', c._predecessor, r, record)
                (run / 'unbound-job.json').write_text('{}')
                audit.reset_mock()
                self.assertRaisesRegex(ValueError, 'not fully pinned', c._predecessor, r, record)
                audit.assert_not_called()


class PeerAndIdentity(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.run = self.root / 'prior'
        self.run.mkdir()
        self.record = copy.deepcopy(c.EXAMPLE_RECORD)
        self.record.update(session='next', boot_id='boot')
        self.record['native_build'] = {'path': str(self.root / 'build/native'),
                                      'provenance': {'source_commit': 'build'}, 'artifacts': {}}
        self.record['helper'] = {'path': str(self.root / 'build/helper-runtime-model/x/wine_runtime_model.exe'),
                                 'sha256': 'helper', 'build_manifest': {'path': str(self.root / 'build/helper-runtime-model/x/build.json'),
                                                                      'sha256': 'manifest', 'artifact_sha256': 'helper'}}
        self.r = SimpleNamespace(ROOT=self.root, checked_run=lambda session: self.run,
                                 sha=lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest(),
                                 check_kernel_journal=lambda text, boot: None)

    def put(self, name, value):
        path = self.run / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))

    def pin(self):
        self.record['predecessor'] = {'session': 'prior', 'files': {
            str(p.relative_to(self.run)): self.r.sha(p) for p in self.run.rglob('*') if p.is_file()}}

    def peer(self, sky='off', mode='skipEvaluate'):
        helper = self.record['helper']
        manifest = {'session': 'prior', 'host_safety': {'boot_id': 'boot'}, 'sky': sky, 'mode': mode,
                    'native_build_provenance': self.record['native_build']['provenance'],
                    'artifacts': {helper['path']: helper['sha256']}, 'runtime_model': True,
                    'helper_build_manifest': helper['build_manifest']}
        self.put('manifest.json', manifest)
        self.put('result.json', {'status': 'PASS_NORMAL_TEARDOWN', 'returncode': 0,
                                'native_lastuse_proof': True, 'residuals': [], 'watchdog_expired': False,
                                'supervisor_interrupted': False, 'received_signals': 0})
        (self.run / 'game.log').write_text('NATIVE_ASYNC_PRE_SR_EXIT_PROOF actual_source_cache_SR_GPU_lastuse_complete=1 managed_owner_joined=1 peer_retirement=OWNER_PROOF\n')
        self.put('async-native-launch.json', {})
        counts = {'finalized_jobs': 1, 'captured_jobs': int(mode == 'resident'),
                  'obsolete_jobs': 0, 'skip_jobs': int(mode == 'skipEvaluate')}
        self.put('async-wine-result.json', counts | {'session': 'prior', 'status': 'PASS', 'normal_release': True,
                 'normal_shutdown': True, 'feature_create_calls': 1, 'feature_release_calls': 1})
        self.put('async-native-helper-exit.json', {'normal_exit': True, 'wait_status': 0})
        terminal = counts | {'session': 'prior', 'source_frame_valid': True}
        self.put('async-native-released-prior-g0.json', terminal | {
            'native_last_use_proof': 'ACTUAL_NATIVE_GPU_LAST_USE', 'native_last_use_tick': 10,
            'native_last_use_observed_tick': 10, 'completed_GPU': True,
            'native_imported_images_owned': 0, 'native_raw_duplicate_fd_owned': 0,
            'native_origin_leases_owned': 0, 'native_cache_owners_owned': 0})
        self.put('async-retired-prior-g0.json', terminal | {'normal_shared_retirement': True,
                 'shared_images_owned': 0, 'export_raw_fd_owned': 0})
        self.put('async-native-audited-prior-g0.json', terminal | {'tracked_NR_FD_remaining': 0})
        self.put('async-meta-g0.json', {'initial_release_GPU_serial': 2})
        tuple_ = {'session': 'prior', 'generation': 0, 'resource_set': 0, 'job': 0,
                  'origin_source': 1, 'origin_epoch': 1, 'origin_history': 1, 'params_version': 1}
        prefix = 'frames/bucket-000000/'
        self.put(prefix + 'async-input-prior-g0-f0.json', tuple_ | {'native_input_GPU_complete': True, 'native_input_tick': 5})
        self.put(prefix + 'async-complete-prior-g0-f0.json', tuple_ | {'native_input_tick_echo': 5,
                 'helper_GPU_complete': True, 'helper_gpu_serial': 3, 'mode': mode,
                 'evaluate_calls': int(mode == 'resident'), 'output_valid': mode == 'resident',
                 'evaluate_parameters': {'Style': 0, 'Intensity': 1.0} if mode == 'resident' else {}, 'gpu_times': {}})
        self.put(prefix + 'async-consumed-prior-g0-f0.json', tuple_ | {'helper_gpu_serial_echo': 3,
                 'native_shared_GPU_complete': True, 'native_shared_last_use_tick': 10,
                 'mode': mode, 'disposition': 'captured' if mode == 'resident' else 'expected_skip'})
        sidecar = {'schema': 'nr-runtime-job-model-v1', 'session': 'prior', 'job': 0,
                   'origin_source': 1, 'origin_epoch': 1, 'origin_history': 1,
                   'model_generation': 0, 'params': 1, 'style': 0, 'intensity': 1.0}
        self.put('runtime-job-model-0.json', sidecar)
        self.put('runtime-job-model-ack-0.json', sidecar)
        (self.run / 'post-exit-kernel.jsonl').write_text('kernel')
        self.put('post-exit-observation.json', {'boot_id': 'boot', 'session': 'prior',
                 'project_holders': [], 'driver_errors': [], 'result_sha256': self.r.sha(self.run / 'result.json'),
                 'kernel_journal_sha256': self.r.sha(self.run / 'post-exit-kernel.jsonl')})
        self.pin()

    def test_formal_peer_off_skip_precedes_persistent_skip_but_not_resident(self):
        self.peer()
        self.record['request'].update(sky='persistent', mode='skipEvaluate')
        c._predecessor(self.r, self.record)
        self.record['request'].update(mode='resident', source_limit=120)
        self.assertRaises(ValueError, c._predecessor, self.r, self.record)

    def test_first_resident_is_limited_to_120_actual_admitted_packets(self):
        self.peer(sky='persistent')
        self.record['request'].update(sky='persistent', mode='resident', source_limit=120)
        c._predecessor(self.r, self.record)
        self.record['request']['source_limit'] = 121
        self.assertRaisesRegex(ValueError, '120 admitted', c._predecessor, self.r, self.record)

    def test_resident_continuation_requires_actual_capture_and_7200_bound(self):
        self.peer(sky='persistent', mode='resident')
        self.record['request'].update(sky='persistent', mode='resident', source_limit=7200)
        c._predecessor(self.r, self.record)
        self.record['request']['source_limit'] = 7201
        self.assertRaises(ValueError, c._predecessor, self.r, self.record)
        self.record['request']['source_limit'] = 7200
        # Still formally retired, but skipped work cannot establish actual NR capture.
        self.peer(sky='persistent', mode='skipEvaluate')
        manifest = c.load(self.run / 'manifest.json')
        manifest['mode'] = 'resident'
        self.put('manifest.json', manifest)
        self.pin()
        self.assertRaisesRegex(ValueError, 'actual Helper mode/Evaluate', c._predecessor, self.r, self.record)

    def test_job_disposition_and_evaluate_cannot_contradict_terminal_skip_counts(self):
        self.peer()
        self.record['request'].update(sky='persistent', mode='skipEvaluate')
        consumed = 'frames/bucket-000000/async-consumed-prior-g0-f0.json'
        original = c.load(self.run / consumed)
        for disposition in ('captured', 'obsolete_discard', 'unknown'):
            self.put(consumed, original | {'disposition': disposition})
            self.pin()
            self.assertRaisesRegex(ValueError, 'per-job dispositions', c._predecessor, self.r, self.record)
        self.put(consumed, original)
        complete = 'frames/bucket-000000/async-complete-prior-g0-f0.json'
        original = c.load(self.run / complete)
        for changes in ({'mode': 'resident'}, {'evaluate_calls': 1}, {'output_valid': True}):
            self.put(complete, original | changes)
            self.pin()
            self.assertRaisesRegex(ValueError, 'actual Helper mode/Evaluate', c._predecessor, self.r, self.record)

    def test_removed_control_binding_fails_closed_when_recovery_markers_remain(self):
        for safety in ({'request': self.record['request']}, {'historical_faults_resolved': False}):
            self.put('manifest.json', {'host_safety': safety})
            with patch.object(c, '_supervisor') as supervisor:
                self.assertRaises(ValueError, c.check_control, self.r, self.run, {}, 'arm')
                supervisor.assert_not_called()

    def test_predecessor_build_and_helper_drift_rejected(self):
        self.peer()
        self.record['request'].update(sky='persistent')
        for key, value in [('native_build_provenance', {'source_commit': 'wrong'}),
                           ('helper_build_manifest', {'sha256': 'wrong'})]:
            manifest = c.load(self.run / 'manifest.json')
            old = manifest[key]
            self.put('manifest.json', manifest | {key: value})
            self.pin()
            self.assertRaises(ValueError, c._predecessor, self.r, self.record)
            self.put('manifest.json', manifest | {key: old})

    def test_runtime_pin_inventory_and_bytes_drift(self):
        helper = self.record['helper']
        for path in [Path(helper['path']), Path(helper['build_manifest']['path']), self.root / 'inputs/runtime-pins.json']:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('{}')
        pin = self.root / 'runtime.so'
        pin.write_text('runtime')
        pins = self.root / 'inputs/runtime-pins.json'
        pins.write_text(json.dumps({'pins': [{'path': str(pin), 'sha256': self.r.sha(pin)}]}))
        helper['sha256'] = self.r.sha(Path(helper['path']))
        self.record['runtime_pins_sha256'] = self.r.sha(pins)
        self.r.checked_native_build = lambda path: (Path(path), [], self.record['native_build']['provenance'])
        self.r.checked_runtime_helper = lambda path, digest: (Path(path), helper['build_manifest'])
        c._identities(self.r, self.record)
        pin.write_text('drift')
        self.assertRaisesRegex(ValueError, 'runtime pin drift', c._identities, self.r, self.record)
        pin.write_text('runtime')
        pins.write_text('{}')
        self.assertRaisesRegex(ValueError, 'pin inventory drift', c._identities, self.r, self.record)

    def test_control_mode_boot_scope_drift_rejected(self):
        self.run = self.root / 'next'
        self.run.mkdir()
        req = self.record['request']
        safety = {'context_recovery': {'path': '/record', 'sha256': 'hash'}, 'boot_id': 'boot',
                  'historical_faults_resolved': False, 'request': req}
        manifest = {'session': 'next', 'host_safety': safety, 'mode': req['mode'], 'sky': req['sky'],
                    'planned_seconds': req['seconds'], 'watchdog_seconds': req['watchdog_seconds'],
                    'continuation': None, 'environment': {'BB_NR_NATIVE_ASYNC_SOURCE_LIMIT': str(req['source_limit'])},
                    'native_build': self.record['native_build']['path'],
                    'native_build_provenance': self.record['native_build']['provenance'], 'runtime_model': True,
                    'helper_build_manifest': self.record['helper']['build_manifest'],
                    'artifacts': {self.record['helper']['path']: self.record['helper']['sha256']}}
        self.put('manifest.json', manifest)
        game = {'pid': 1, 'start_ticks': 2}
        self.r.live = lambda pid: game
        self.r.holders = lambda: []
        with patch.object(c, '_record', return_value=(Path('/record'), self.record)), \
             patch.object(self.r, 'sha', return_value='hash'), patch.object(c, '_supervisor'), \
             patch.object(c, '_common') as common:
            c.check_control(self.r, self.run, game, 'arm')
            common.assert_called_once()
            for field, value in [('boot_id', 'old'), ('historical_faults_resolved', True)]:
                changed = copy.deepcopy(manifest)
                changed['host_safety'][field] = value
                self.put('manifest.json', changed)
                self.assertRaises(ValueError, c.check_control, self.r, self.run, game, 'arm')
            self.put('manifest.json', manifest | {'mode': 'resident'})
            self.assertRaises(ValueError, c.check_control, self.r, self.run, game, 'start-helper')


if __name__ == '__main__':
    unittest.main()
