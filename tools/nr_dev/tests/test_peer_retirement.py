#!/usr/bin/env python3
"""Synthetic CPU protocol fixtures; never evidence of GPU completion."""
import importlib.util,json,pathlib,tempfile,unittest
SPEC=importlib.util.spec_from_file_location('peer_retirement',pathlib.Path(__file__).resolve().parents[1]/'peer_retirement.py')
ret=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(ret)
def write(root,name,j):
 p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(j))
def fixture(root,runtime=True):
 session='cpu-fixture';count=3;counts=dict(finalized_jobs=count,captured_jobs=count,obsolete_jobs=0,skip_jobs=0)
 write(root,'async-meta-g0.json',{'initial_release_GPU_serial':2})
 write(root,'async-wine-result.json',dict(session=session,status='PASS',normal_release=True,normal_shutdown=True,feature_create_calls=2 if runtime else 1,feature_release_calls=2 if runtime else 1,**counts))
 write(root,'async-native-helper-exit.json',dict(normal_exit=True,wait_status=0))
 base=dict(session=session,source_frame_valid=True,**counts)
 release=dict(base,native_last_use_proof='ACTUAL_NATIVE_GPU_LAST_USE',native_last_use_tick=30,native_last_use_observed_tick=30,completed_GPU=True,native_imported_images_owned=0,native_raw_duplicate_fd_owned=0,native_origin_leases_owned=0,native_cache_owners_owned=0)
 write(root,f'async-native-released-{session}-g0.json',release)
 write(root,f'async-retired-{session}-g0.json',dict(base,normal_shared_retirement=True,shared_images_owned=0,export_raw_fd_owned=0))
 write(root,f'async-native-audited-{session}-g0.json',dict(base,tracked_NR_FD_remaining=0))
 if runtime:
  request=dict(schema='nr-runtime-model-v1',session=session,revision=2,previous_generation=0,generation=1,previous_params=1,params=2,style=2,intensity=.75,finalized_jobs=1,native_target_tick=10,native_observed_tick=10,packet_leases=0,shared_job_finalized=True,native_GPU_complete=True)
  write(root,'runtime-model-request-1.json',request)
  write(root,'runtime-model-ack-1.json',dict(request,op='model_ack',status='FEATURE_REBUILT',helper_gpu_serial=4,helper_GPU_complete=True,feature_create_calls=2,feature_release_calls=1))
 for n in range(count):
  fields=dict(session=session,generation=0,resource_set=session+'-g0',job=n,origin_source=100+n,origin_epoch=1,origin_history=1,params_version=1)
  serial=3 if n==0 else 4+n
  prefix=f'frames/bucket-000000/async-'
  write(root,prefix+f'input-{session}-g0-f{n}.json',dict(fields,native_input_GPU_complete=True,native_input_tick=n+1))
  write(root,prefix+f'complete-{session}-g0-f{n}.json',dict(fields,native_input_tick_echo=n+1,helper_GPU_complete=True,helper_gpu_serial=serial,gpu_times={},evaluate_parameters={'Style':2 if runtime and n else 0,'Intensity':.75 if runtime and n else 1.0}))
  write(root,prefix+f'consumed-{session}-g0-f{n}.json',dict(fields,helper_gpu_serial_echo=serial,native_shared_GPU_complete=True,native_shared_last_use_tick=9+n,disposition='captured'))
  if runtime:
   identity=dict(schema='nr-runtime-job-model-v1',session=session,job=n,origin_source=100+n,origin_epoch=1,origin_history=1,model_generation=1 if n else 0,params=2 if n else 1,style=2 if n else 0,intensity=.75 if n else 1.0)
   write(root,f'runtime-job-model-{n}.json',identity);write(root,f'runtime-job-model-ack-{n}.json',identity)
 return session
class Audit(unittest.TestCase):
 def audit(self,root,session,runtime):
  out={};ret.audit_origin_jobs_and_retirement({'actual_helper_starts':1},root,session,out,expected_jobs=3,continuous_max_jobs=20,runtime_model=runtime);return out
 def test_runtime_generation_jobs_and_shutdown(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);session=fixture(root);out=self.audit(root,session,True);self.assertEqual(out['feature_create_release_count'],2)
 def test_legacy_not_relaxed(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);session=fixture(root,False);self.audit(root,session,False)
   j=ret.load(root/'async-wine-result.json');j.update(feature_create_calls=2,feature_release_calls=2);write(root,'async-wine-result.json',j)
   with self.assertRaises(ValueError):self.audit(root,session,False)
 def test_missing_or_false_ack_proof(self):
  for field,value in [('helper_GPU_complete',False),('feature_create_calls',1),('params',3),('helper_gpu_serial',2)]:
   with self.subTest(field=field),tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);session=fixture(root);j=ret.load(root/'runtime-model-ack-1.json');j[field]=value;write(root,'runtime-model-ack-1.json',j)
    with self.assertRaises(ValueError):self.audit(root,session,True)
 def test_wrong_job_generation_or_params(self):
  for field,value in [('model_generation',0),('params',1),('style',0),('intensity',1.0),('origin_source',999)]:
   with self.subTest(field=field),tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);session=fixture(root)
    for name in ('runtime-job-model-1.json','runtime-job-model-ack-1.json'):
     j=ret.load(root/name);j[field]=value;write(root,name,j)
    with self.assertRaises(ValueError):self.audit(root,session,True)
 def test_rebuild_before_drain_or_before_shared_lastuse(self):
  for field,value in [('packet_leases',1),('native_target_tick',8),('native_observed_tick',9),('finalized_jobs',0)]:
   with self.subTest(field=field),tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp);session=fixture(root)
    for name in ('runtime-model-request-1.json','runtime-model-ack-1.json'):
     j=ret.load(root/name);j[field]=value;write(root,name,j)
    with self.assertRaises(ValueError):self.audit(root,session,True)
if __name__=='__main__':unittest.main()
