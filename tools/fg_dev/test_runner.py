"""CPU admission/retirement fixtures; never GPU evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'nr_dev'))
import runner
import peer_retirement


class FgRunnerTests(unittest.TestCase):
    def test_capture_requires_its_native_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            build=Path(directory);(build/'gpu').mkdir()
            library=build/'gpu/libbbgpu.so'
            args=runner.parse_args(['start','cpu-capture','--daily','--fg-native',
                                   '--local-inputs','/inputs','--experimental-fg','dlssg-capture'])
            library.write_bytes(b'FG_EXIT_PROOF BB_FRAME_GEN_NATIVE')
            with self.assertRaisesRegex(ValueError,'rebuilt Native diagnostic'):
                runner.check_fg_capability(args,build)
            library.write_bytes(b'FG_EXIT_PROOF BB_FRAME_GEN_NATIVE DLSSG_INPUT_CAPTURE')
            runner.check_fg_capability(args,build)

    def test_old_installed_runtime_rejects_fg_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / 'gpu').mkdir()
            library = build / 'gpu/libbbgpu.so'
            library.write_bytes(b'legacy Native')
            args = runner.parse_args(['start', 'cpu-fg', '--daily', '--fg-native',
                                      '--local-inputs', '/inputs', '--experimental-fg', 'fsr31-hudless'])
            with self.assertRaisesRegex(ValueError, 'rebuilt M1'):
                runner.check_fg_capability(args, build)
            library.write_bytes(b'FG_EXIT_PROOF BB_FRAME_GEN_NATIVE')
            runner.check_fg_capability(args, build)
            args.experimental_fg = None
            library.unlink()
            runner.check_fg_capability(args, build)

    def test_fg_requires_explicit_sr_only(self):
        args = runner.parse_args(['start', 'cpu-fg', '--daily', '--sr-only',
                                  '--local-inputs', '/inputs', '--experimental-fg', 'fsr31'])
        self.assertEqual(args.experimental_fg, 'fsr31')
        args.sr_only = False
        with self.assertRaisesRegex(ValueError, 'daily SR-only'):
            runner.validate_daily_args(args)
        args.sr_only, args.daily = True, False
        with self.assertRaisesRegex(ValueError, 'daily SR-only'):
            runner.validate_daily_args(args)

    def test_native_integration_is_explicit(self):
        args = runner.parse_args(['start', 'cpu-native-fg', '--daily', '--fg-native',
                                  '--local-inputs', '/inputs', '--experimental-fg', 'fsr31-hudless'])
        self.assertFalse(args.sr_only)
        self.assertTrue(args.fg_native)
        args.experimental_fg = None
        with self.assertRaisesRegex(ValueError, 'requires daily experimental FG'):
            runner.validate_daily_args(args)

    def test_nr_no_peer_proof_is_insufficient_for_fg(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'manifest.json').write_text(json.dumps({'session': 'cpu-fg', 'environment': {'BB_FRAME_GEN': '1'}}))
            text = ('NATIVE_ASYNC_PRE_SR_EXIT_PROOF actual_source_cache_SR_GPU_lastuse_complete=1 managed_owner_joined=1 peer_retirement=OWNER_PROOF\n'
                    'NATIVE_ASYNC_PRE_SR_EXIT_NO_PEER session=cpu-fg actual_no_owner=1 actual_no_snapshot=1 actual_no_pending=1 maximum_native_use_tick=0 preparation_joined=1 no_peer_created=1 no_NR_claim=1 scope=native_async_only\n')
            (run / 'game.log').write_text(text)
            termination = {'returncode': 0, 'residuals': [], 'watchdog_expired': False}
            with self.assertRaisesRegex(ValueError, 'FG requires actual'):
                peer_retirement.audit_session(run, termination)
            (run / 'game.log').write_text(text + 'FG_EXIT_PROOF draw_GPU_complete=1 present_GPU_complete=1 resources_released=1\n')
            self.assertEqual(peer_retirement.audit_session(run, termination)['path'], 'NO_PEER_NORMAL_EXIT')

    def test_daily_fg_proof_in_previous_rotation_remains_unique(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'manifest.json').write_text(json.dumps({
                'session': 'cpu-fg', 'daily': True, 'environment': {'BB_FRAME_GEN': '1'}}))
            fg = 'FG_EXIT_PROOF draw_GPU_complete=1 present_GPU_complete=1 resources_released=1\n'
            nr = ('NATIVE_ASYNC_PRE_SR_EXIT_PROOF actual_source_cache_SR_GPU_lastuse_complete=1 managed_owner_joined=1 peer_retirement=OWNER_PROOF\n'
                  'NATIVE_ASYNC_PRE_SR_EXIT_NO_PEER session=cpu-fg actual_no_owner=1 actual_no_snapshot=1 actual_no_pending=1 maximum_native_use_tick=0 preparation_joined=1 no_peer_created=1 no_NR_claim=1 scope=native_async_only\n')
            (run / 'game-log-state.json').write_text(json.dumps({
                'schema': 'nr-daily-game-log-v1', 'complete': True,
                'failure_latched': False, 'error': None, 'bytes_seen': 8192,
                'rotations': 1, 'limit_bytes': 4096, 'retained_files': 2}))
            (run / 'game.log.1').write_text(fg)
            (run / 'game.log').write_text(nr)
            termination = {'returncode': 0, 'residuals': [], 'watchdog_expired': False}
            out = peer_retirement.audit_session(run, termination)
            self.assertEqual(out['path'], 'NO_PEER_NORMAL_EXIT')
            self.assertEqual(out['daily_game_log']['order'], ['game.log.1', 'game.log'])
            # Joining rotations must never deduplicate or manufacture completion.
            (run / 'game.log').write_text(nr + fg)
            with self.assertRaisesRegex(ValueError, 'FG requires actual'):
                peer_retirement.audit_session(run, termination)
            (run / 'game.log.1').write_text(fg.replace('draw_GPU_complete=1', 'draw_GPU_complete=0'))
            (run / 'game.log').write_text(nr)
            with self.assertRaisesRegex(ValueError, 'FG requires actual'):
                peer_retirement.audit_session(run, termination)


if __name__ == '__main__':
    unittest.main()
