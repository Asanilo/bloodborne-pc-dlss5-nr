"""Real filesystem retirement checks; no GPU, Wine or game."""
import importlib.util,json,os,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('generated_retirement_runner',Path(__file__).resolve().parents[1]/'runner.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
GOOD={'status':'PASS_NORMAL_TEARDOWN','returncode':0,'native_lastuse_proof':True,'residuals':[],'watchdog_expired':False,'supervisor_interrupted':False}
class GeneratedRetirement(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.run=self.root/'runs/test';self.out=self.run/'profile/out';self.out.mkdir(parents=True)
  self.p=patch.object(r,'ROOT',self.root);self.p.start()
 def tearDown(self):self.p.stop();self.tmp.cleanup()
 def test_only_allowlisted_generated_files_removed(self):
  for name in ('boot.bin','eboot.elf','entry.bin','boot-libc.bin','boot-linked.bin','content.bin','patches.bin'): (self.out/name).write_bytes(b'generated')
  for name in ('analysis.json','link.json','captured-pixels.bin','user-source.cpp'):(self.out/name).write_bytes(b'unique')
  save=self.run/'profile/user/savedata/boot.bin';save.parent.mkdir(parents=True);save.write_bytes(b'save')
  report=r.retire_generated_inputs(self.run,GOOD);self.assertEqual(report['status'],'RETIRED');self.assertEqual(len(report['removed']),7)
  self.assertEqual(save.read_bytes(),b'save');self.assertEqual(sorted(p.name for p in self.out.iterdir()),['analysis.json','captured-pixels.bin','link.json','user-source.cpp'])
  self.assertTrue(all(len(x['sha256'])==64 for x in report['removed']))
 def test_any_missing_or_negative_retirement_proof_retains(self):
  f=self.out/'boot.bin';f.write_bytes(b'keep')
  for key,bad in [('status','HOLD'),('returncode',1),('returncode',False),('native_lastuse_proof',False),('residuals',[123]),('watchdog_expired',True),('supervisor_interrupted',True)]:
   with self.subTest(key=key,bad=bad):
    result={**GOOD,key:bad};self.assertEqual(r.retire_generated_inputs(self.run,result)['status'],'SKIPPED_NOT_NORMAL_TEARDOWN');self.assertTrue(f.exists())
  for key in GOOD:
   result=GOOD.copy();del result[key];self.assertEqual(r.retire_generated_inputs(self.run,result)['status'],'SKIPPED_NOT_NORMAL_TEARDOWN')
 def test_file_symlink_rejects_whole_batch(self):
  first=self.out/'boot.bin';first.write_bytes(b'keep');target=self.root/'original';target.write_bytes(b'original');(self.out/'eboot.elf').symlink_to(target)
  self.assertEqual(r.retire_generated_inputs(self.run,GOOD)['status'],'RETAINED');self.assertTrue(first.exists());self.assertEqual(target.read_bytes(),b'original')
 def test_hardlink_rejects_whole_batch(self):
  target=self.root/'original';target.write_bytes(b'original');os.link(target,self.out/'boot.bin')
  self.assertEqual(r.retire_generated_inputs(self.run,GOOD)['status'],'RETAINED');self.assertTrue((self.out/'boot.bin').exists())
 def test_directory_symlink_not_followed(self):
  self.out.rmdir();external=self.root/'external';external.mkdir();(external/'boot.bin').write_bytes(b'keep');self.out.symlink_to(external,target_is_directory=True)
  self.assertEqual(r.retire_generated_inputs(self.run,GOOD)['status'],'RETAINED');self.assertTrue((external/'boot.bin').exists())
 def test_missing_output_idempotent_and_wrong_root_refused(self):
  self.out.rmdir();self.assertEqual(r.retire_generated_inputs(self.run,GOOD)['status'],'NOTHING_TO_RETIRE')
  self.assertEqual(r.retire_generated_inputs(self.root/'test',GOOD)['status'],'RETAINED')
if __name__=='__main__':unittest.main()
