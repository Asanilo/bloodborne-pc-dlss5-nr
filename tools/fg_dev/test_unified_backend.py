import argparse,json,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'nr_dev'))
import fg_config,runner,prefix_lease
class UnifiedTests(unittest.TestCase):
 def test_selected_backend_can_start_disabled_without_changing_nr(self):
  a=runner.parse_args(['start','cpu','--daily','--local-inputs','/inputs','--fg-backend','dlssg','--fg-start-off','--dlssg-model','/model'])
  self.assertTrue(a.fg_start_off);self.assertFalse(a.sr_only)
  self.assertEqual(fg_config.requested(a),'dlssg')
  with self.assertRaises(SystemExit):
   runner.parse_args(['start','cpu','--daily','--local-inputs','/inputs','--fg-backend','off','--fg-start-off'])
 def test_legacy_and_explicit_precedence(self):
  self.assertEqual(fg_config.selection({})['backend'],'off')
  self.assertEqual(fg_config.selection({'nrbridge_fsr_fg':True})['backend'],'fsr31')
  self.assertEqual(fg_config.selection({'nrbridge_fsr_fg':True,'fg_backend':'off'})['backend'],'off')
  with self.assertRaises(ValueError):fg_config.selection({'fg_backend':'future-algorithm'})
 def test_one_backend_and_nr_independence(self):
  for backend in fg_config.BACKENDS:
   for nr in (False,True):
    argv=['start','cpu','--daily','--local-inputs','/inputs','--fg-backend',backend]
    if not nr:argv+=['--sr-only']
    if backend=='dlssg':argv+=['--dlssg-model','/model']
    if backend=='smooth-motion':
     a=runner.parse_args(argv)
     self.assertTrue(a.smooth_motion);self.assertIsNone(a.experimental_fg)
     self.assertEqual(fg_config.requested(a),'smooth-motion')
     continue
    a=runner.parse_args(argv)
    self.assertEqual(a.sr_only,not nr)
    self.assertFalse(a.smooth_motion and a.experimental_fg)
    if backend=='dlssg':self.assertEqual((a.dlssg_transport,a.dlssg_pipeline),('gpu-dmabuf-v1','submit-ready-v1'))
  for backend in ('off','fsr31','dlssg'):
   with self.assertRaises(SystemExit):runner.parse_args(['start','cpu','--daily','--local-inputs','/inputs','--fg-backend',backend,'--smooth-motion'])
 def test_pipeline_is_not_backend_and_cpu_fallback(self):
  for pipe in fg_config.PIPELINES:
   c=fg_config.selection({'fg_backend':'dlssg','fg_dlss_pipeline':pipe})
   self.assertEqual(c['backend'],'dlssg');self.assertEqual(c['pipeline'],pipe)
  self.assertEqual(fg_config.selection({'fg_backend':'dlssg','fg_dlss_transport':'mapped-v1'})['pipeline'],'sync-v1')
 def test_sm_is_selectable_without_application_fg(self):
  # The M3 quarantine is lifted on this branch: the runner still requires a Native
  # with the SM_EXIT_PROOF marker and the boot receipt still gates every daily run.
  self.assertIsInstance(fg_config.availability({'backend':'smooth-motion'},'/missing'),str)
  a=argparse.Namespace(fg_backend='smooth-motion',smooth_motion=False)
  fg_config.normalize(a)
  self.assertTrue(a.smooth_motion);self.assertIsNone(a.experimental_fg);self.assertFalse(a.fg_native)
  self.assertEqual(fg_config.requested(a),'smooth-motion')
  with self.assertRaises(ValueError):
   fg_config.normalize(argparse.Namespace(fg_backend='smooth-motion',smooth_motion=False,fg_native=True))
 def test_mfg_requires_supported_pipeline(self):
  for m in range(2,7):
   c=fg_config.selection({'fg_backend':'dlssg','fg_dlss_pipeline':'consumer-queue-v1','fg_dlss_multiplier':m})
   self.assertEqual(c['multiplier'],m)
  config=fg_config.selection({'fg_backend':'dlssg','fg_dlss_multiplier':3,'fg_dlss_pipeline':'submit-ready-v1'})
  self.assertEqual(config['pipeline'],'consumer-queue-v1')
  config=fg_config.selection({'fg_backend':'dlssg','fg_dlss_multiplier':3,'fg_dlss_transport':'mapped-v1'})
  with self.assertRaisesRegex(ValueError,'requires GPU'):fg_config.availability(config,'/missing')
 def test_manifest_drift(self):
  env={'BB_FRAME_GEN':'0','BB_FRAME_GEN_BACKEND':'smooth-motion','NVPRESENT_ENABLE_SMOOTH_MOTION':'1'}
  fg_config.validate_environment('smooth-motion',env)
  with self.assertRaises(ValueError):fg_config.validate_environment('smooth-motion',env|{'BB_FRAME_GEN':'1'})
  with self.assertRaises(ValueError):fg_config.validate_environment('off',env)
 def test_unavailable_does_not_substitute_fsr(self):
  c=fg_config.selection({'fg_backend':'dlssg'})
  with self.assertRaisesRegex(ValueError,'model'):fg_config.availability(c,'/missing')
  self.assertEqual(c['backend'],'dlssg')
 def test_prefix_moves_without_copy_and_requires_retirement(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);source=p/'old';target=p/'new';source.mkdir();target.mkdir();(source/'system.reg').write_text('identity')
   inode=(source/'system.reg').stat().st_ino
   with prefix_lease.PrefixLease(source,target,p/'lease.json') as lease:
    self.assertEqual((target/'system.reg').stat().st_ino,inode)
    lease.started=True
    with self.assertRaises(ValueError):lease.complete({'status':'HOLD'})
    lease.complete({'status':'PASS_NORMAL_TEARDOWN','native_lastuse_proof':True,'residuals':[]})
   self.assertEqual((source/'system.reg').stat().st_ino,inode)
   with prefix_lease.PrefixLease(source,target,p/'lease.json') as lease:lease.started=True
   self.assertFalse(source.exists());self.assertTrue((target/'system.reg').is_file())
   self.assertEqual(json.loads((p/'lease.json').read_text())['state'],'RETAINED_UNPROVED')
if __name__=='__main__':unittest.main()
