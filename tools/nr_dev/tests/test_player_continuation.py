"""CPU-only private continuation and plan/watchdog boundary tests; no game launch."""
import importlib.util,json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
HERE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(HERE));spec=importlib.util.spec_from_file_location('player_runner',HERE/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
class ContinuationTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.patch=patch.object(r,'ROOT',self.root);self.patch.start();self.source=self.root/'runs'/'player-old';save=self.source/'profile/user/savedata';save.mkdir(parents=True);(save/'private-save').write_bytes(b'private progress');(self.source/'profile/nrbridge.json').write_text('{"version":1,"model_style":1}')
  (self.source/'manifest.json').write_text(json.dumps({'schema':'nr-dev-session-v1','session':'player-old','environment':{'BB_USER_DIR':str(self.source/'profile/user'),'BB_DATA_DIR':str(self.source/'profile')}}));self.result={'status':'PASS_NORMAL_TEARDOWN','returncode':0,'watchdog_expired':False,'residuals':[],'supervisor_interrupted':False};self.write_result();self.audit=patch('retirement.audit_session',return_value={'retirement':'CPU_TEST_STUB_NORMAL_RETIREMENT'});self.audit.start()
 def tearDown(self):self.audit.stop();self.patch.stop();self.tmp.cleanup()
 def write_result(self):(self.source/'result.json').write_text(json.dumps(self.result))
 def test_independent_copy_includes_private_config_and_hashes(self):
  dest=self.root/'runs/new/profile';dest.mkdir(parents=True);proof=r.copy_continuation('player-old',dest);p=dest/'user/savedata/private-save';self.assertEqual(p.read_bytes(),b'private progress');self.assertEqual(r.sha(dest/'nrbridge.json'),proof['nrbridge_json_sha256']);p.write_bytes(b'new progress');self.assertEqual((self.source/'profile/user/savedata/private-save').read_bytes(),b'private progress')
 def test_hold_resolution_is_not_normal_source(self):
  self.result['status']='HOLD';self.write_result();(self.source/'hold-resolution.json').write_text('{"normal_retirement_proved":true}')
  with self.assertRaises(ValueError):r.continuation_source('player-old')
 def test_old_or_formal_paths_and_symlinks_rejected(self):
  for name in ('../old-space','/home/arinp/bloodborne','../../nrbridge-design'):
   with self.assertRaises(ValueError):r.continuation_source(name)
  save=self.source/'profile/user/savedata';(save/'bad-link').symlink_to('/tmp')
  with self.assertRaises(ValueError):r.continuation_source('player-old')
 def test_retirement_is_reaudited_not_status_only(self):
  with patch('retirement.audit_session',side_effect=ValueError('unretired model resources')):
   with self.assertRaises(ValueError):r.continuation_source('player-old')
 def test_planned_end_is_not_watchdog_failure(self):
  s=r.SessionSchedule(100,480,600,120);self.assertFalse(s.close_due(579));self.assertTrue(s.close_due(580));s.note_close(580);self.assertFalse(s.expired(580));self.assertFalse(s.expired(699));self.assertTrue(s.expired(700))
 def test_close_grace_independent_and_never_extends_helper_capacity(self):
  s=r.SessionSchedule(0,30,600,90);s.note_close(30);self.assertFalse(s.expired(119));self.assertTrue(s.expired(120));s.note_close(100);self.assertTrue(s.expired(120))
  with self.assertRaises(ValueError):r.SessionSchedule(0,600,600,120)
 def test_runtime_helper_build_manifest_and_binary_both_pinned(self):
  helper=self.root/'build/helper-runtime-model/test/wine_runtime_model.exe';helper.parent.mkdir(parents=True);helper.write_bytes(b'CPU fake PE');manifest=helper.parent/'build.json';manifest.write_text(json.dumps({'status':'BUILD_ONLY_PASS','artifact':str(helper),'artifact_sha256':r.sha(helper)}));h,proof=r.checked_runtime_helper(helper,r.sha(manifest));self.assertEqual(h,helper);helper.write_bytes(b'changed')
  with self.assertRaises(ValueError):r.checked_runtime_helper(helper,proof['sha256'])
if __name__=='__main__':unittest.main()
