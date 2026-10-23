"""SM selection and actual-retirement proof fixtures; no GPU evidence."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'nr_dev'))
import smooth_motion
import runner
import peer_retirement


class SmoothMotionTests(unittest.TestCase):
    def test_only_daily_and_exclusive_mode(self):
        a = runner.parse_args(['start', 'cpu-sm', '--daily', '--local-inputs', '/inputs', '--smooth-motion'])
        self.assertTrue(a.smooth_motion)
        for key, value in [('experimental_fg', 'fsr31'), ('fg_native', True), ('daily', False)]:
            b = argparse.Namespace(**vars(a)); setattr(b, key, value)
            with self.assertRaisesRegex(ValueError, 'without application FG'):
                smooth_motion.validate_args(b)

    def test_environment_drift_rejected(self):
        run = Path('/private/run')
        env = smooth_motion.environment(run) | {'VK_LOADER_LAYERS_DISABLE': '~implicit~'}
        manifest = {'smooth_motion': True, 'environment': env}
        smooth_motion.validate_manifest(run, manifest)
        for key, value in [('BB_FRAME_GEN', '1'), ('VK_LOADER_LAYERS_ALLOW', '*'),
                           ('NVPRESENT_LOG_FILE', '/other/log')]:
            with self.assertRaises(ValueError):
                smooth_motion.validate_manifest(run, manifest | {'environment': env | {key: value}})
        with self.assertRaises(ValueError):
            smooth_motion.validate_manifest(run, {'environment': env})
        smooth_motion.validate_manifest(run, {})  # old sessions remain auditable

    def test_old_native_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory); (p/'gpu').mkdir(); lib = p/'gpu/libbbgpu.so'
            a = argparse.Namespace(smooth_motion=True)
            lib.write_bytes(b'FG_EXIT_PROOF')
            with self.assertRaisesRegex(ValueError, 'driver-layer retirement'):
                smooth_motion.check_capability(a, p)
            lib.write_bytes(b'SM_EXIT_PROOF')
            smooth_motion.check_capability(a, p)

    def test_sm_requires_its_own_teardown(self):
        with tempfile.TemporaryDirectory() as directory:
            r = Path(directory)
            (r/'manifest.json').write_text(json.dumps({'session':'cpu-sm','smooth_motion':True}))
            nr = ('NATIVE_ASYNC_PRE_SR_EXIT_PROOF actual_source_cache_SR_GPU_lastuse_complete=1 managed_owner_joined=1 peer_retirement=OWNER_PROOF\n'
                  'NATIVE_ASYNC_PRE_SR_EXIT_NO_PEER session=cpu-sm actual_no_owner=1 actual_no_snapshot=1 actual_no_pending=1 maximum_native_use_tick=0 preparation_joined=1 no_peer_created=1 no_NR_claim=1 scope=native_async_only\n'
                  'FG_EXIT_PROOF draw_GPU_complete=1 present_GPU_complete=1 resources_released=1\n')
            sm = 'SM_EXIT_PROOF device_idle_before=1 swapchain_destroy_returned=1 device_idle_after=1\n'
            term = {'returncode':0,'residuals':[],'watchdog_expired':False}
            for suffix in ['', sm.replace('after=1', 'after=0'), sm+sm]:
                (r/'game.log').write_text(nr+suffix)
                with self.assertRaisesRegex(ValueError, 'Smooth Motion requires actual'):
                    peer_retirement.audit_session(r, term)
            (r/'game.log').write_text(nr+sm)
            self.assertEqual(peer_retirement.audit_session(r, term)['path'], 'NO_PEER_NORMAL_EXIT')

if __name__ == '__main__': unittest.main()
