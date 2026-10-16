import copy,importlib.util,json,tempfile,unittest,sys,io
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
OLD='11111111-1111-4111-8111-111111111111'
NEW='22222222-2222-4222-8222-222222222222'
OTHER='33333333-3333-4333-8333-333333333333'
spec=importlib.util.spec_from_file_location('reboot_runner',Path(__file__).parents[1]/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
class RebootScope(unittest.TestCase):
 def setUp(self):
  self.args=SimpleNamespace(session='diagnostic',mode='skipEvaluate',sky='off',seconds=120,watchdog_seconds=240,runtime_model_helper=None,history_diagnostic=False,pm4_cold_trace=True,continue_session=None,pm4_producer_context=False,reboot_diagnostic='/record')
  self.snapshot={'old-failure':{'boot_id':OLD,'files':{'result.json':'hash'}}};self.provenance={'source_commit':'commit'};self.artifacts={'native':'digest'}
  self.record={'schema':'nr-reboot-sr-diagnostic-v1','normal_retirement_proved':False,'historical_faults_resolved':False,'boot_id':NEW,'previous_boot_id':OLD,'session':'diagnostic','held_runs':copy.deepcopy(self.snapshot),'source_commit':'commit','native_artifacts':self.artifacts,'project_holders':[],'driver_errors':[],'authorization_basis':'user resumed limited diagnostic after reboot'}
 def verify(self):r.validate_reboot_scope(self.record,self.args,NEW,self.snapshot,self.provenance,self.artifacts)
 def test_new_boot_scope_does_not_change_old_evidence(self):
  original=copy.deepcopy(self.snapshot);self.verify();self.assertEqual(original,self.snapshot);self.assertFalse(self.record['normal_retirement_proved'])
 def test_same_boot_or_other_session_rejected(self):
  self.record['previous_boot_id']=NEW;self.assertRaises(ValueError,self.verify);self.record['previous_boot_id']=OLD;self.args.session='other';self.assertRaises(ValueError,self.verify)
 def test_nr_unbounded_or_untraced_options_rejected(self):
  for key,value in [('mode','resident'),('sky','persistent'),('seconds',181),('watchdog_seconds',600),('runtime_model_helper','helper'),('history_diagnostic',True),('continue_session','old-profile'),('pm4_cold_trace',False)]:
   old=getattr(self.args,key);setattr(self.args,key,value);self.assertRaises(ValueError,self.verify);setattr(self.args,key,old)
 def test_new_or_changed_hold_and_build_drift_rejected(self):
  self.snapshot['new-failure']={'boot_id':NEW,'files':{}};self.assertRaises(ValueError,self.verify);del self.snapshot['new-failure'];self.artifacts={'native':'changed'};self.assertRaises(ValueError,self.verify)
 def test_resolving_old_fault_is_rejected(self):
  self.record['historical_faults_resolved']=True;self.assertRaises(ValueError,self.verify)
 def v2(self):
  self.snapshot['older-failure']={'boot_id':OTHER,'files':{'result.json':'other-hash'}}
  self.record.update(schema='nr-reboot-sr-diagnostic-v2',held_runs=copy.deepcopy(self.snapshot),historical_boot_ids=sorted([OLD,OTHER]),producer_context=False)
 def test_v2_multiple_real_historical_boots(self):
  self.v2();original=copy.deepcopy(self.snapshot);self.verify();self.assertEqual(original,self.snapshot)
  del self.record['previous_boot_id'];self.verify()
 def test_v1_preserves_single_boot_only(self):
  self.v2();self.record['schema']='nr-reboot-sr-diagnostic-v1';self.assertRaises(ValueError,self.verify)
 def test_v2_history_exact_sorted_unique(self):
  self.v2()
  for history in ([OLD],[OLD,OTHER,OTHER],[OTHER,OLD],[OLD,OTHER,NEW],['bad',OTHER],[],None):
   self.record['historical_boot_ids']=history;self.assertRaises(ValueError,self.verify)
 def test_v2_uuid_and_current_boot_rejected(self):
  self.v2()
  for key in ('boot_id','previous_boot_id'):
   old=self.record[key]
   for invalid in ('bad','',123):
    self.record[key]=invalid;self.assertRaises(ValueError,self.verify)
   self.record[key]=old
  self.snapshot['old-failure']['boot_id']='bad';self.record['held_runs']=copy.deepcopy(self.snapshot);self.assertRaises(ValueError,self.verify)
  self.snapshot['old-failure']['boot_id']=NEW;self.record['held_runs']=copy.deepcopy(self.snapshot);self.record['historical_boot_ids']=sorted([NEW,OTHER]);self.assertRaises(ValueError,self.verify)
 def test_v2_snapshot_hash_drift_and_new_boot_rejected(self):
  self.v2();self.snapshot['old-failure']['files']['result.json']='changed';self.assertRaises(ValueError,self.verify)
  self.record['held_runs']=copy.deepcopy(self.snapshot);self.verify()
  self.snapshot['new-failure']={'boot_id':OTHER,'files':{}};self.assertRaises(ValueError,self.verify)
  self.assertRaises(ValueError,r.validate_reboot_scope,self.record,self.args,OTHER,self.snapshot,self.provenance,self.artifacts)
 def test_producer_scope_and_record_binding(self):
  self.args.pm4_producer_context=True;self.assertRaises(ValueError,self.verify)
  self.v2();self.assertRaises(ValueError,self.verify);self.record['producer_context']=True;self.verify()
  for key,value in (('pm4_cold_trace',False),('reboot_diagnostic',None),('pm4_producer_context',1)):
   old=getattr(self.args,key);setattr(self.args,key,value);self.assertRaises(ValueError,self.verify);setattr(self.args,key,old)
  for value in (None,1,'true',False):
   self.record['producer_context']=value;self.assertRaises(ValueError,self.verify)
 def test_producer_exactly_one_scope_and_strict_cold(self):
  args=SimpleNamespace(pm4_producer_context=True,pm4_cold_trace=True,reboot_diagnostic=None,context_recovery='/record')
  self.assertTrue(r.validate_pm4_producer_context(args))
  args.reboot_diagnostic='/other';self.assertRaises(ValueError,r.validate_pm4_producer_context,args)
  args.reboot_diagnostic=None
  for value in (False,1,'1',None):
   args.pm4_cold_trace=value;self.assertRaises(ValueError,r.validate_pm4_producer_context,args)
 def test_producer_option_rejected_before_launch_setup(self):
  args=SimpleNamespace(pm4_producer_context=True,pm4_cold_trace=True,reboot_diagnostic=None)
  with patch.object(r,'checked_run') as checked:
   self.assertRaises(ValueError,r.launch_supervised,args,None);checked.assert_not_called()
 def test_cli_producer_combinations_before_launch(self):
  base=['runner.py','start','diagnostic','--seconds','120','--watchdog-seconds','240','--pm4-producer-context']
  for options in ([],['--pm4-cold-trace'],['--reboot-diagnostic','/record']):
   with patch.object(sys,'argv',base+options),patch('sys.stderr',new=io.StringIO()),patch.object(r,'launch') as launch:
    with self.assertRaises(SystemExit) as error:r.main()
    self.assertEqual(error.exception.code,2);launch.assert_not_called()
  with patch.object(sys,'argv',base+['--pm4-cold-trace','--reboot-diagnostic','/record']),patch.object(r,'launch',return_value=0) as launch:
   self.assertEqual(r.main(),0);self.assertTrue(launch.call_args.args[0].pm4_producer_context)
 def test_controls_reject_before_observing_or_starting_game(self):
  with tempfile.TemporaryDirectory() as d:
   run=Path(d);(run/'manifest.json').write_text(json.dumps({'host_safety':{'reboot_diagnostic':True}}))
   with patch.object(r,'checked_run',return_value=run),patch.object(r,'current_game') as current:
    for command in ('start-helper','arm'):
     with self.assertRaisesRegex(ValueError,'forbids Helper and NR'):r.control(SimpleNamespace(session='diagnostic',command=command))
    current.assert_not_called();self.assertFalse((run/'native-helper-start').exists());self.assertFalse((run/'native-scene-arm').exists())
if __name__=='__main__':unittest.main()
