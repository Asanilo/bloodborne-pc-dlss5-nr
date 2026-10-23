import importlib.util,tempfile,unittest,json,shutil
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('runner',Path(__file__).parents[1]/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
PRIVATE_ROOT=r.ROOT
class NativeBuild(unittest.TestCase):
 def test_source_checkout_relocation_keeps_private_data_confinement(self):
  with tempfile.TemporaryDirectory() as scratch:
   checkout=Path(scratch)/'bloodborne_pc';tools=checkout/'tools/nr_dev';tools.mkdir(parents=True)
   for name in ('runner.py','dimension_profile.py','fg_config.py','smooth_motion.py','dlssg_backend.py','runtime_inputs.py','native_mouse_scope.py','daily_online.py','daily_session_guard.py','player_settings.py','ui_start.py'):shutil.copyfile(Path(r.__file__).with_name(name),tools/name)
   spec=importlib.util.spec_from_file_location('relocated_runner',tools/'runner.py');relocated=importlib.util.module_from_spec(spec);spec.loader.exec_module(relocated)
   self.assertEqual(relocated.WT,checkout)
   self.assertEqual(relocated.ROOT,PRIVATE_ROOT)
   self.assertRaises(ValueError,relocated.checked_native_build,checkout/'out')
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.patch=patch.object(r,'ROOT',self.root);self.patch.start();self.addCleanup(self.patch.stop);self.head='e'*40
 def make(self,baseline=False):
  p=self.root/'build'/('baselines/'+self.head if baseline else 'native');(p/'gpu').mkdir(parents=True)
  for name in ('bb-probe','gpu/libbbgpu.so','libbbport_dlss.so','libnvidia-ngx-dlss.so.310.9.1'):(p/name).write_bytes(name.encode())
  (p/'source-head.txt').write_text(self.head+'\n');(p/'source-diff.patch').write_text('')
  (p/'artifacts.sha256').write_text(''.join(r.sha(p/n)+'  '+str(self.root/'build/native'/n)+'\n' for n in ('bb-probe','gpu/libbbgpu.so','libbbport_dlss.so','libnvidia-ngx-dlss.so.310.9.1')))
  return p
 def test_native_and_relocated_baseline(self):
  for baseline in (False,True):
   p=self.make(baseline);build,artifacts,proof=r.checked_native_build(p);self.assertEqual(build,p);self.assertEqual(len(artifacts),4);self.assertEqual(proof['source_commit'],self.head)
 def test_dirty_baseline_rejected(self):
  p=self.make(True);(p/'source-diff.patch').write_text('diff');self.assertRaises(ValueError,r.checked_native_build,p)
 def test_dirty_native_recorded(self):
  p=self.make();(p/'source-diff.patch').write_text('diff');self.assertEqual(r.checked_native_build(p)[2]['files']['source-diff.patch'],r.sha(p/'source-diff.patch'))
 def test_sha_drift_rejected(self):
  p=self.make();(p/'bb-probe').write_bytes(b'changed');self.assertRaises(ValueError,r.checked_native_build,p)
 def test_external_directory_rejected(self):self.assertRaises(ValueError,r.checked_native_build,self.root/'external')
 def test_symlink_artifact_rejected(self):
  p=self.make();file=p/'bb-probe';file.unlink();outside=self.root/'other';outside.write_bytes(b'bb-probe');file.symlink_to(outside);self.assertRaises(ValueError,r.checked_native_build,p)
 def test_duplicate_manifest_rejected(self):
  p=self.make();f=p/'artifacts.sha256';f.write_text(f.read_text()+f.read_text().splitlines()[0]+'\n');self.assertRaises(ValueError,r.checked_native_build,p)
 def test_commit_mismatch_rejected(self):
  p=self.make(True);(p/'source-head.txt').write_text('f'*40);self.assertRaises(ValueError,r.checked_native_build,p)
 def test_control_rejects_manifest_build_provenance_drift(self):
  p=self.make(True);build,artifacts,proof=r.checked_native_build(p);run=self.root/'run';run.mkdir()
  manifest={'native_build':str(build),'native_executable':str(build/'bb-probe'),'native_build_provenance':proof,'artifacts':{str(a):r.sha(a) for a in artifacts}}
  manifest['native_build_provenance']['source_commit']='f'*40
  (run/'manifest.json').write_text(json.dumps(manifest))
  self.assertRaises(ValueError,r.current_game,run)
if __name__=='__main__':unittest.main()
