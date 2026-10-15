"""Signal callbacks exercised offline; never start a renderer, Wine or a game."""
import importlib.util,json,signal,tempfile,unittest
from unittest.mock import patch
from pathlib import Path
spec=importlib.util.spec_from_file_location('nr_runner',Path(__file__).resolve().parents[1]/'runner.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class SignalTests(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.run=Path(self.tmp.name)
 def tearDown(self):self.tmp.cleanup()
 def test_pending_prevents_spawn_and_records_hold(self):
  s=m.SupervisorSignals();s.run=self.run;s.receive(signal.SIGTERM,None);self.assertFalse(s.before_spawn());r=json.loads((self.run/'result.json').read_text());self.assertIsNone(r['returncode']);self.assertEqual(r['actual_game_starts'],0);self.assertEqual(r['status'],'HOLD')
 def test_events_append_and_close_requested_once(self):
  s=m.SupervisorSignals();s.run=self.run;s.receive(signal.SIGTERM,None);s.flush();first=(self.run/'signal-events.jsonl').read_bytes();requests=[];s.request_close_once('test',requests.append);s.receive(signal.SIGINT,None);s.flush();s.request_close_once('test',requests.append);self.assertEqual(len(requests),1);self.assertTrue((self.run/'signal-events.jsonl').read_bytes().startswith(first));events=[json.loads(x) for x in (self.run/'signal-events.jsonl').read_text().splitlines()];self.assertEqual([x['signal'] for x in events if 'signal' in x],[signal.SIGTERM,signal.SIGINT]);self.assertTrue(s.interrupted)
 def test_close_failure_not_retried_or_called_safe(self):
  s=m.SupervisorSignals();s.run=self.run;s.receive(signal.SIGTERM,None);s.flush();calls=[]
  def fail(args):calls.append(args);raise ValueError('identity unknown')
  s.request_close_once('test',fail);s.request_close_once('test',fail);self.assertEqual(len(calls),1);self.assertIn('REQUEST_FAILED',(self.run/'signal-events.jsonl').read_text());self.assertTrue(s.interrupted)
 def test_final_signal_keeps_real_exit_code_but_never_pass(self):
  previous=signal.getsignal(signal.SIGTERM)
  with m.SupervisorSignals() as s:
   s.run=self.run;self.assertEqual(signal.getsignal(signal.SIGTERM),s.receive);m.atomic(self.run/'result.json',{'status':'PASS_NORMAL_TEARDOWN','returncode':0});s.receive(signal.SIGTERM,None)
  result=json.loads((self.run/'result.json').read_text());self.assertEqual(result['status'],'HOLD');self.assertEqual(result['returncode'],0);self.assertTrue(result['supervisor_interrupted']);self.assertEqual(signal.getsignal(signal.SIGTERM),previous)
 def test_existing_normal_close_not_duplicated(self):
  s=m.SupervisorSignals();s.run=self.run;(self.run/'stop-request.json').write_text('{}');s.receive(signal.SIGTERM,None);s.flush();calls=[];s.request_close_once('test',calls.append);self.assertEqual(calls,[])
 def test_device_sample_survives_missing_pid_query(self):
  with patch.object(m.subprocess,'check_output',side_effect=[OSError('compute unavailable'),'GPU-test, 4000, 8192\n']) as query:
   result=m.sample_gpu_memory()
  self.assertEqual(result['pid_used_MiB_csv'],'');self.assertEqual(result['device_uuid_used_total_MiB_csv'],'GPU-test, 4000, 8192\n');self.assertEqual(query.call_count,2);self.assertIn('--query-gpu=uuid,memory.used,memory.total',query.call_args_list[1].args[0])
if __name__=='__main__':unittest.main()
