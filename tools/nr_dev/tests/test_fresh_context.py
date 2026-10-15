import importlib.util,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('runner',Path(__file__).parents[1]/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
class FreshContext(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.run=Path(self.temp.name)/'test';self.run.mkdir()
  env={'BB_NR_NATIVE_ASYNC_SCENE_FIRST':'1','BB_NR_NATIVE_ASYNC':'0','BB_NR_NATIVE_ASYNC_PRE_SR':'1','BB_NR_NATIVE_ASYNC_RUN_DIR':str(self.run),'BB_NR_NATIVE_ASYNC_SCENE_ARM':str(self.run/'native-scene-arm')}
  self.put('manifest.json',{'schema':'nr-dev-session-v1','session':'test','environment':env,'artifacts':{'artifact':'digest'}})
  self.result={'status':'HOLD','returncode':23,'watchdog_expired':False,'supervisor_interrupted':False,'received_signals':0,'residuals':[]};self.put('result.json',self.result)
  for name in ('runner.json','game-identity.json','native-loaded-libraries.json'):self.put(name,{})
  self.log='NATIVE_ASYNC_PRE_SR_FRONTEND_READY session=test scene_arm=REQUIRED availability_only=1 no_GPU_proof=1\nUnimplemented PM4 type 0\nSTOP: GPU library assertion failed (see GPU log above)\n';(self.run/'game.log').write_text(self.log)
  self.provenance={'source_commit':'a6a25e5','observed_clean_build':True,'observation':'coordinator witnessed clean build/launch','artifacts':{'artifact':'digest'}}
  self.source=patch.object(r.subprocess,'check_output',side_effect=['std::fflush(stdout); std::_Exit(23);','exact_token(run+"/native-helper-start")','frontend&&frontend->HelperStartRequested() if(!peer&&can_start)']);self.source.start();self.addCleanup(self.source.stop)
 def put(self,name,value):(self.run/name).write_text(json.dumps(value))
 def test_cold_proof_never_changes_result(self):
  before=(self.run/'result.json').read_bytes();self.assertIn('game.log',r.audit_pre_nr_absence(self.run,self.provenance));self.assertEqual(before,(self.run/'result.json').read_bytes())
 def test_control_file_rejected(self):
  (self.run/'native-helper-start').write_text('test');self.assertRaises(ValueError,r.audit_pre_nr_absence,self.run,self.provenance)
 def test_nr_marker_rejected(self):
  (self.run/'game.log').write_text(self.log.replace('Unimplemented','NATIVE_ORIGIN_READY\nUnimplemented'));self.assertRaises(ValueError,r.audit_pre_nr_absence,self.run,self.provenance)
 def test_signal_rejected(self):
  self.result['received_signals']=1;self.put('result.json',self.result);self.assertRaises(ValueError,r.audit_pre_nr_absence,self.run,self.provenance)
 def test_lock_required(self):self.assertRaises(ValueError,r.verify_fresh_context_resolution,self.run,{},None)
 def test_build_observation_cannot_change_artifacts(self):
  self.provenance['artifacts']={};self.assertRaises(ValueError,r.audit_pre_nr_absence,self.run,self.provenance)
if __name__=='__main__':unittest.main()
