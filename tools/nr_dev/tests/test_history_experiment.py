"""Reject invalid diagnostic policies before any runtime side effects."""
import contextlib,json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE));import runner

class HistoryExperimentAdmission(unittest.TestCase):
 def test_defaults_and_supported_modes(self):
  with patch.dict(os.environ,{},clear=True):
   runner.validate_history_experiment_env()
   for policy in ('legacy','fade-new'):
    os.environ['BB_NR_VISUAL_TRANSITION']=policy;runner.validate_history_experiment_env()
 def test_intervals(self):
  with patch.dict(os.environ,{},clear=True):
   for value in ('0','1','30','120'):
    os.environ['BB_NR_HISTORY_SAMPLE_INTERVAL']=value;runner.validate_history_experiment_env()
   for value in ('','121','01','nan','-1'):
    os.environ['BB_NR_HISTORY_SAMPLE_INTERVAL']=value
    with self.assertRaises(ValueError):runner.validate_history_experiment_env()
 def test_old_native_is_rejected_only_for_new_feature(self):
  with tempfile.TemporaryDirectory() as root,patch.dict(os.environ,{},clear=True):
   path=Path(root)/'gpu/libbbgpu.so';path.parent.mkdir();path.write_bytes(b'old Native')
   runner.check_history_capability(root)
   os.environ['BB_NR_VISUAL_TRANSITION']='fade-new'
   with self.assertRaises(ValueError):runner.check_history_capability(root)
   path.write_bytes(b'NRBRIDGE_NATIVE_HISTORY_FADE_V1');runner.check_history_capability(root)
 def test_rejected_policy_creates_no_runtime(self):
  with tempfile.TemporaryDirectory() as root:
   env=dict(os.environ,NRBRIDGE_HOME=root,BB_NR_VISUAL_TRANSITION='cadence')
   out=subprocess.run([sys.executable,str(HERE/'play.py'),'--daily'],env=env,capture_output=True,text=True)
   self.assertEqual(out.returncode,2);self.assertIn('cadence A2a was rejected',out.stderr)
   self.assertEqual(list(Path(root).iterdir()),[])

if __name__=='__main__':unittest.main()
