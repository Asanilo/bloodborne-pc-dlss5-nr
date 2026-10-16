import copy,importlib.util,json,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('reboot_runner',Path(__file__).parents[1]/'runner.py');r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
class RebootScope(unittest.TestCase):
 def setUp(self):
  self.args=SimpleNamespace(session='diagnostic',mode='skipEvaluate',sky='off',seconds=120,watchdog_seconds=240,runtime_model_helper=None,history_diagnostic=False,pm4_cold_trace=True,continue_session=None)
  self.snapshot={'old-failure':{'boot_id':'old','files':{'result.json':'hash'}}};self.provenance={'source_commit':'commit'};self.artifacts={'native':'digest'}
  self.record={'schema':'nr-reboot-sr-diagnostic-v1','normal_retirement_proved':False,'historical_faults_resolved':False,'boot_id':'new','previous_boot_id':'old','session':'diagnostic','held_runs':copy.deepcopy(self.snapshot),'source_commit':'commit','native_artifacts':self.artifacts,'project_holders':[],'driver_errors':[],'authorization_basis':'user resumed limited diagnostic after reboot'}
 def verify(self):r.validate_reboot_scope(self.record,self.args,'new',self.snapshot,self.provenance,self.artifacts)
 def test_new_boot_scope_does_not_change_old_evidence(self):
  original=copy.deepcopy(self.snapshot);self.verify();self.assertEqual(original,self.snapshot);self.assertFalse(self.record['normal_retirement_proved'])
 def test_same_boot_or_other_session_rejected(self):
  self.record['previous_boot_id']='new';self.assertRaises(ValueError,self.verify);self.record['previous_boot_id']='old';self.args.session='other';self.assertRaises(ValueError,self.verify)
 def test_nr_unbounded_or_untraced_options_rejected(self):
  for key,value in [('mode','resident'),('sky','persistent'),('seconds',181),('watchdog_seconds',600),('runtime_model_helper','helper'),('history_diagnostic',True),('continue_session','old-profile'),('pm4_cold_trace',False)]:
   old=getattr(self.args,key);setattr(self.args,key,value);self.assertRaises(ValueError,self.verify);setattr(self.args,key,old)
 def test_new_or_changed_hold_and_build_drift_rejected(self):
  self.snapshot['new-failure']={'boot_id':'new','files':{}};self.assertRaises(ValueError,self.verify);del self.snapshot['new-failure'];self.artifacts={'native':'changed'};self.assertRaises(ValueError,self.verify)
 def test_resolving_old_fault_is_rejected(self):
  self.record['historical_faults_resolved']=True;self.assertRaises(ValueError,self.verify)
 def test_controls_reject_before_observing_or_starting_game(self):
  with tempfile.TemporaryDirectory() as d:
   run=Path(d);(run/'manifest.json').write_text(json.dumps({'host_safety':{'reboot_diagnostic':True}}))
   with patch.object(r,'checked_run',return_value=run),patch.object(r,'current_game') as current:
    for command in ('start-helper','arm'):
     with self.assertRaisesRegex(ValueError,'forbids Helper and NR'):r.control(SimpleNamespace(session='diagnostic',command=command))
    current.assert_not_called();self.assertFalse((run/'native-helper-start').exists());self.assertFalse((run/'native-scene-arm').exists())
if __name__=='__main__':unittest.main()
