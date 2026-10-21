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


if __name__ == '__main__':
    unittest.main()
