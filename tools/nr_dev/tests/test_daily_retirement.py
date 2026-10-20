"""CPU fixtures validate failure rejection; never evidence of actual GPU use."""
import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('daily_retirement',Path(__file__).parents[1]/'peer_retirement.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
def fixture(root):
 session='daily-fixture';n=20000
 common=dict(session=session,generation=0,resource_set=session+'-g0',protocol_version=2,transport_layout='frame-ring-64-daily-v2',mode='resident')
 terminal=dict(common,finalized_jobs=n,captured_jobs=n,obsolete_jobs=0,skip_jobs=0,job=n,next_job=n,origin_source=100000,origin_epoch=1,origin_history=1,source_frame_valid=True)
 release=dict(terminal,status='NATIVE_RELEASED',output_valid=False,model_valid=False,native_last_use_proof='ACTUAL_NATIVE_GPU_LAST_USE',native_last_use_tick=500000,native_last_use_observed_tick=500000,completed_GPU=True,native_imported_images_owned=0,native_raw_duplicate_fd_owned=0,native_origin_leases_owned=0,native_cache_owners_owned=0)
 docs={'async-config.json':common,'async-meta-g0.json':common,'async-wine-result.json':dict(terminal,op='result',status='PASS',normal_release=True,normal_shutdown=True,completed_GPU=True,feature_create_calls=2,feature_release_calls=2,evaluate_calls_total=n,resolve_calls_total=n),'async-native-helper-exit.json':dict(normal_exit=True,wait_status=0,finalized_jobs=n),f'async-native-released-{session}-g0.json':release,f'async-retired-{session}-g0.json':dict(terminal,status='RETIRED',normal_shared_retirement=True,completed_GPU=True,shared_images_owned=0,export_raw_fd_owned=0),f'async-native-audited-{session}-g0.json':dict(release,status='FD_AUDITED',tracked_NR_FD_remaining=0)}
 for name,doc in docs.items():(root/name).write_text(json.dumps(doc))
 return session
class DailyRetirement(unittest.TestCase):
 def test_beyond_old_cap_with_real_terminal_contract_fixture(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);s=fixture(root);self.assertEqual(r.audit_daily(root,s)['terminal_counts']['finalized_jobs'],20000)
 def test_false_or_mismatched_proofs_are_rejected(self):
  for name,key,value in [('async-wine-result.json','normal_shutdown',False),('async-wine-result.json','feature_release_calls',1),('async-wine-result.json','evaluate_calls_total',19999),('async-native-helper-exit.json','wait_status',1),('async-native-released-daily-fixture-g0.json','native_last_use_observed_tick',499999),('async-retired-daily-fixture-g0.json','shared_images_owned',1),('async-native-audited-daily-fixture-g0.json','tracked_NR_FD_remaining',1),('async-config.json','transport_layout','frame-buckets-64-v1')]:
   with self.subTest(key=key),tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);s=fixture(root);doc=json.loads((root/name).read_text());doc[key]=value;(root/name).write_text(json.dumps(doc))
    with self.assertRaises(ValueError):r.audit_daily(root,s)
 def test_pending_files_refuse_normal_retirement(self):
  for name in ['runtime-model-request-3.json','runtime-job-model-2.json','frames/bucket-000000/async-input-x.json']:
   with self.subTest(name=name),tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);s=fixture(root);p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('{}')
    with self.assertRaises(ValueError):r.audit_daily(root,s)
if __name__=='__main__':unittest.main()
