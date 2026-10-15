import importlib.util,json,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('metrics',Path(__file__).resolve().parents[1]/'metrics.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class WindowTests(unittest.TestCase):
 def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.run=Path(self.tmp.name)
 def tearDown(self):self.tmp.cleanup()
 def write(self,sources=(10,11,12,13,14),times=None,extra=''):
  times=times or [1_000_000_000+i*10_000_000 for i in range(len(sources))]
  (self.run/'game.log').write_text(''.join(f'NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT session=test source_game_frame={s} present_frame_number={i+1} swapchain_present=1 cpu_monotonic_ns={t}\n' for i,(s,t) in enumerate(zip(sources,times)))+extra)
 def test_exclusive_source_and_time_bounds(self):
  self.write();out=m.measure(self.run,after_source=10,before_source=14,after_ns=1_005_000_000,before_ns=1_035_000_000);self.assertEqual(out['frames'],3);self.assertEqual(out['source_first'],11);self.assertEqual(out['FPS'],100);self.assertEqual(out['P99_ms'],10);self.assertEqual(out['window_seconds'],.02)
 def test_empty_and_one_sample(self):
  self.write();self.assertEqual(m.measure(self.run,after_source=14)['frames'],0);self.assertEqual(m.measure(self.run,after_source=13)['status'],'NO_VALID_ACTUAL_PRESENT_WINDOW')
 def test_gaps_not_removed_from_elapsed_window(self):
  self.write((10,11,14),[100,10_000_100,50_000_100]);out=m.measure(self.run);self.assertEqual(out['missing_source_count'],2);self.assertEqual(out['source_gap_events'],1);self.assertAlmostEqual(out['FPS'],40)
 def test_duplicate_and_nonmonotonic_reject(self):
  self.write((10,11,11));self.assertEqual(m.measure(self.run)['status'],'DUPLICATE_PRESENT_REJECTED');self.write((10,11,12),[100,300,200]);self.assertEqual(m.measure(self.run)['status'],'NONMONOTONIC_PRESENT_REJECTED')
 def test_cold_never_hot(self):
  self.write(extra='PERSISTENT_HISTORY_DIAGNOSTIC_RECORDED source=12 index=0\n');self.assertEqual(m.measure(self.run)['status'],'COLD_DIAGNOSTIC_EXCLUDED');out=m.measure(self.run,after_source=12,include_cold=True);self.assertFalse(out['hot_acceptance_eligible']);self.assertTrue(out['performance_excluded'])
 def test_memory_missing_not_zero_and_pid_match(self):
  self.write();(self.run/'game-identity.json').write_text(json.dumps({'pid':123,'start_ticks':456}));(self.run/'gpu-memory.jsonl').write_text(json.dumps({'monotonic_ns':1_020_000_000,'pid_used_MiB_csv':'456, 5000\n'})+'\n');out=m.measure(self.run)['gpu_memory'];self.assertEqual(out['status'],'NOT_MEASURED');self.assertNotIn('peak_MiB',out)
  (self.run/'gpu-memory.jsonl').write_text(json.dumps({'monotonic_ns':1_020_000_000,'pid_used_MiB_csv':'123, 2048\n'})+'\n');self.assertEqual(m.measure(self.run)['gpu_memory']['peak_MiB'],2048)
if __name__=='__main__':unittest.main()
