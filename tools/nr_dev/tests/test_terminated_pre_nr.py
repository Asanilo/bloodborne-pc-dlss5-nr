"""Strict new-context-only exception; no GPU, process signals, or lock mutations."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC=importlib.util.spec_from_file_location('terminated_runner',Path(__file__).parents[1]/'runner.py')
runner=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(runner)

class TerminatedPreNr(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
  self.root=Path(self.tmp.name);self.run=self.root/'runs/dev-transition-cold-01';self.run.mkdir(parents=True)
  self.root_patch=patch.object(runner,'ROOT',self.root);self.root_patch.start();self.addCleanup(self.root_patch.stop)
  r=self.run;head=runner.TERMINATED_PRE_NR_HEAD
  env={'BB_NR_NATIVE_ASYNC_SCENE_FIRST':'1','BB_NR_NATIVE_ASYNC':'0','BB_NR_NATIVE_ASYNC_PRE_SR':'1','BB_NR_NATIVE_ASYNC_RUN_DIR':str(r),'BB_NR_NATIVE_ASYNC_SCENE_ARM':str(r/'native-scene-arm')}
  self.manifest={'schema':'nr-dev-session-v1','session':r.name,'environment':env,'native_build':'/example/build','artifacts':{'/example/build/bb-probe':'a'*64,'/example/helper.exe':'b'*64}}
  b=r/'build-provenance';b.mkdir();(b/'source-head.txt').write_text(head+'\n');(b/'source-diff.patch').write_bytes(b'');(b/'artifacts.sha256').write_text('a'*64+'  /example/build/bb-probe\n')
  self.manifest['native_build_provenance']={'source_commit':head,'files':{p.name:runner.sha(p) for p in b.iterdir()}}
  self.write('manifest.json',self.manifest)
  self.result={'status':'HOLD','returncode':137,'watchdog_expired':True,'supervisor_interrupted':False,'received_signals':0,'residuals':[],'native_lastuse_proof':False};self.write('result.json',self.result)
  self.identity={'pid':1234,'start_ticks':5678,'exe':'/example/build/bb-probe'}
  self.write('game-identity.json',self.identity);self.write('runner.json',{'runner':self.identity});self.write('native-loaded-libraries.json',{})
  for name in ('exit-thread-stacks-user.txt','frozen-registers.txt'):(r/name).write_text('frozen diagnostic\n')
  self.auth={'scope':'explicit user-authorized termination of one frozen private test; NOT normal GPU retirement','identity':self.identity,'signals':['SIGTERM','SIGKILL'],'identity_remaining':False,'original_hold_sha256':'7be3c9c44e16262b34d181b093970aaa0f9c04a6acdf76188cd80b3bba717345','monotonic_ns':1,'completed_monotonic_ns':2,'diagnostic_sha256':{n:runner.sha(r/n) for n in ('exit-thread-stacks-user.txt','frozen-registers.txt')}}
  self.write('authorized-abnormal-termination.json',self.auth)
  self.log='NATIVE_ASYNC_PRE_SR_FRONTEND_READY session='+r.name+' scene_arm=REQUIRED availability_only=1 no_GPU_proof=1\nNATIVE_ASYNC_HELPER_START_REQUEST session='+r.name+' scene_first=1 scene_proof=REQUIRES_ROOT GPU_proof=NONE\nNATIVE_ASYNC_EXIT_PENDING actual_owner_completion=WAITING forced_free=0\n'
  (r/'game.log').write_text(self.log)
  self.observation={'source_commit':head,'artifacts':self.manifest['artifacts'],'observed_clean_build':True,'observation':'fixture observed pinned clean build'}
 def write(self,name,obj): (self.run/name).write_text(json.dumps(obj))
 def audit(self):return runner.audit_terminated_pre_nr_freeze(self.run,self.observation)
 def test_exact_frozen_context_allows_late_start_not_retirement(self):
  result=self.audit();self.assertIn('authorized-abnormal-termination.json',result);self.assertIn('build-provenance/source-diff.patch',result)
  with self.assertRaisesRegex(ValueError,'normal retired'):runner.continuation_source(self.run.name)
 def test_owning_artifacts_forbidden_even_empty(self):
  for name in ('native-scene-arm','async-config.json','frames','runtime-job-model-0.json','async-native-launch.json','native.abort'):
   p=self.run/name;p.touch()
   with self.assertRaises(ValueError,msg=name):self.audit()
   p.unlink()
 def test_raw_sr_allowed_but_cache_and_unknown_nr_logs_rejected(self):
  raw='NATIVE_ASYNC_PRE_SR_SR_READ session='+self.run.name+' cache_selected=0 request_origin=0 candidate_job=0\n'
  (self.run/'game.log').write_text(self.log+raw);self.audit()
  for bad in (raw.replace('cache_selected=0','cache_selected=1'),raw.replace('request_origin=0','request_origin=4'),'PERSISTENT_HISTORY_TRANSITION_BEGIN source=1\n','NATIVE_ORIGIN_INPUT_PUBLISHED job=0\n','NR_RUNTIME_MODEL_COMMITTED params=2\n','NATIVE_ASYNC_PRE_SR_INIT native_snapshots=15\n'):
   (self.run/'game.log').write_text(self.log+bad)
   with self.assertRaises(ValueError):self.audit()
 def test_bad_termination_identity_signal_or_hash_rejected(self):
  for key,value in (('identity',dict(self.identity,start_ticks=5679)),('signals',['SIGKILL']),('identity_remaining',True),('original_hold_sha256','0'*64),('completed_monotonic_ns',0)):
   auth=copy.deepcopy(self.auth);auth[key]=value;self.write('authorized-abnormal-termination.json',auth)
   with self.assertRaises(ValueError,msg=key):self.audit()
  self.write('authorized-abnormal-termination.json',self.auth)
  (self.run/'frozen-registers.txt').write_text('changed')
  with self.assertRaises(ValueError):self.audit()
 def test_result_and_observation_fail_closed(self):
  for key,value in (('status','PASS_NORMAL_TEARDOWN'),('returncode',0),('watchdog_expired',False),('residuals',[1234]),('native_lastuse_proof',True),('received_signals',1)):
   value_result=dict(self.result);value_result[key]=value;self.write('result.json',value_result)
   with self.assertRaises(ValueError,msg=key):self.audit()
  self.write('result.json',self.result);self.observation['observed_clean_build']=False
  with self.assertRaises(ValueError):self.audit()
 def test_source_provenance_and_scene_arm_binding(self):
  m=copy.deepcopy(self.manifest);m['environment']['BB_NR_NATIVE_ASYNC_SCENE_FIRST']='0';self.write('manifest.json',m)
  with self.assertRaises(ValueError):self.audit()
  self.write('manifest.json',self.manifest);(self.run/'build-provenance/source-diff.patch').write_text('dirty')
  with self.assertRaises(ValueError):self.audit()
 def test_exception_cannot_generalize_to_other_run_or_old_kind(self):
  with self.assertRaises(ValueError):runner.audit_terminated_pre_nr_freeze(self.run.parent/'another-run',self.observation)
  with self.assertRaises(ValueError):runner.audit_pre_nr_absence(self.run,self.observation)

if __name__=='__main__':unittest.main()
