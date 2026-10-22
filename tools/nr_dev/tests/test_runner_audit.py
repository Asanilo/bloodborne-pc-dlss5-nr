"""Offline parser/proof regressions; no process, GPU, Wine or game launch."""
import hashlib,importlib.util,json,os,tempfile,unittest
from unittest.mock import patch
from pathlib import Path
HERE=Path(__file__).resolve().parents[1]
def module(name):
 spec=importlib.util.spec_from_file_location(name,HERE/(name+'.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
runner=module('runner');retirement=module('peer_retirement')
PRE='NATIVE_ASYNC_PRE_SR_EXIT_PROOF actual_source_cache_SR_GPU_lastuse_complete=1 managed_owner_joined=1 peer_retirement=OWNER_PROOF\n'
NO='NATIVE_ASYNC_PRE_SR_EXIT_NO_PEER session=test actual_no_owner=1 actual_no_snapshot=1 actual_no_pending=1 maximum_native_use_tick=0 preparation_joined=1 no_peer_created=1 no_NR_claim=1 scope=native_async_only\n'
FG='FG_EXIT_PROOF draw_GPU_complete=1 present_GPU_complete=1 resources_released=1\n'
class AuditTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
 def tearDown(self):self.tmp.cleanup()
 def fixture(self,text=NO+PRE):
  (self.root/'manifest.json').write_text(json.dumps({'session':'test'}));(self.root/'game.log').write_text(text);(self.root/'result.json').write_text(json.dumps({'status':'HOLD','returncode':0,'residuals':[],'watchdog_expired':False}))
 def daily_fixture(self,previous=None,current=NO+PRE):
  self.fixture(current)
  (self.root/'manifest.json').write_text(json.dumps({'session':'test','daily':True}))
  if previous is not None:(self.root/'game.log.1').write_text(previous)
  state={'schema':'nr-daily-game-log-v1','complete':True,'failure_latched':False,
         'error':None,'bytes_seen':len((previous or '').encode())+len(current.encode()),
         'rotations':int(previous is not None),'limit_bytes':4096,'retained_files':2}
  (self.root/'game-log-state.json').write_text(json.dumps(state))
 def test_correct_presr_nopeer_readonly(self):
  self.fixture();original=(self.root/'result.json').read_bytes();out=retirement.audit_session(self.root);self.assertEqual(out['path'],'NO_PEER_NORMAL_EXIT');self.assertEqual((self.root/'result.json').read_bytes(),original)
 def test_old_postsr_tag_rejected(self):
  self.fixture(NO+'NATIVE_ASYNC_EXIT_PROOF actual_snapshot_GPU_lastuse_complete=1 managed_owner_joined=1\n')
  with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_nonzero_absent_use_rejected(self):
  self.fixture((NO+PRE).replace('maximum_native_use_tick=0','maximum_native_use_tick=1'))
  with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_peer_does_not_fallback_to_nopeer(self):
  self.fixture();(self.root/'async-native-launch.json').write_text('{}')
  with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_peer_missing_terminal_does_not_pass(self):
  self.fixture(PRE);(self.root/'async-native-launch.json').write_text('{}')
  with self.assertRaises(FileNotFoundError):retirement.audit_session(self.root)
 def test_failure_marker_rejected(self):
  self.fixture(NO+PRE+'NATIVE_ASYNC_PRE_SR_HOLD unknown_use=1\n')
  with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_daily_exit_proofs_span_rotation_and_original_inputs_stay_unchanged(self):
  self.daily_fixture(previous=FG+NO,current=PRE)
  names=('manifest.json','result.json','game.log.1','game.log','game-log-state.json')
  originals={name:(self.root/name).read_bytes() for name in names}
  out=retirement.audit_session(self.root)
  self.assertEqual(out['path'],'NO_PEER_NORMAL_EXIT')
  self.assertEqual(out['daily_game_log']['order'],['game.log.1','game.log'])
  for name in names:
   self.assertEqual((self.root/name).read_bytes(),originals[name])
   self.assertEqual(out[name+'_sha256'],hashlib.sha256(originals[name]).hexdigest())
 def test_daily_unrotated_log_and_unrecorded_previous_file(self):
  self.daily_fixture()
  self.assertEqual(retirement.audit_session(self.root)['daily_game_log']['order'],['game.log'])
  (self.root/'game.log.1').write_text('unexpected retained tail\n')
  with self.assertRaisesRegex(ValueError,'without a recorded rotation'):
   retirement.audit_session(self.root)
 def test_daily_duplicate_native_proofs_across_rotation_rejected(self):
  for previous in (PRE,NO):
   with self.subTest(previous=previous):
    self.daily_fixture(previous=previous)
    with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_daily_duplicate_fg_rows_are_not_deduplicated_by_reader(self):
  self.daily_fixture(previous=FG,current=FG+NO+PRE)
  text,_=retirement.daily_log.read_completed(self.root)
  self.assertEqual(len(retirement.rows(text,'FG_EXIT_PROOF')),2)
 def test_daily_rotated_failure_and_failure_latch_are_both_enforced(self):
  self.daily_fixture(previous='NATIVE_ASYNC_PRE_SR_HOLD unknown_use=1\n')
  with self.assertRaises(ValueError):retirement.audit_session(self.root)
  self.daily_fixture(previous='old ordinary text\n')
  state_path=self.root/'game-log-state.json';state=json.loads(state_path.read_text())
  for changes in ({'failure_latched':True},{'error':'capture failed'},{'complete':False}):
   with self.subTest(changes=changes):
    state_path.write_text(json.dumps(state|changes))
    with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_daily_missing_previous_log_cannot_pass(self):
  self.daily_fixture(previous=NO,current=PRE);(self.root/'game.log.1').unlink()
  with self.assertRaises(ValueError):retirement.audit_session(self.root)
 def test_named_deleted_memfds_and_real_inode(self):
  ngx=self.root/'libnvidia-ngx-dlss.so.310.9.1';ngx.write_bytes(b'pinned-source-not-unpacked-memfd')
  bridge=self.root/'libbbport_dlss.so';bridge.write_bytes(b'bridge');st=bridge.stat()
  mem=f'1000-2000 r-xp 00000000 00:01 1234 /memfd:{ngx} (deleted)\n'
  mapped=f'3000-4000 r-xp 00000000 {os.major(st.st_dev):02x}:{os.minor(st.st_dev):02x} {st.st_ino} {bridge}\n'
  artifacts={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (ngx,bridge)}
  out=runner.verify_native_mappings(mem+mem+mapped,artifacts);self.assertEqual(len(out),3);self.assertEqual(out[0]['source'],str(ngx));self.assertTrue(out[0]['memfd']);self.assertIn('not asserted',out[0]['identity_scope'])
  with self.assertRaises(ValueError):runner.verify_native_mappings(mem.replace(str(ngx),'/elsewhere/'+ngx.name),artifacts)
  with self.assertRaises(ValueError):runner.verify_native_mappings(mapped.replace(str(st.st_ino)+' ',str(st.st_ino+1)+' '),artifacts)
 def test_btrfs_device_requires_superblock_fsid_and_inode(self):
  source=self.root/'libbbport_dlss.so';source.write_bytes(b'library');st=source.stat()
  minor=os.minor(st.st_dev)+1
  fields=['1000-2000','r-xp','00000000',f'00:{minor:x}',str(st.st_ino),str(source)]
  mount=f'40 1 0:{minor} /@ / rw - btrfs /dev/test rw,subvolid=256\n'
  with patch.object(Path,'read_text',return_value=mount),patch.object(runner,'btrfs_fsid',return_value='same-fsid'):
   self.assertIn('Btrfs',runner.mapped_file_identity(str(source),fields)['device_proof'])
  with patch.object(Path,'read_text',return_value=mount),patch.object(runner,'btrfs_fsid',side_effect=['source-fsid','other-fsid']):
   with self.assertRaises(ValueError):runner.mapped_file_identity(str(source),fields)
  fields[3]=f'00:{minor+1:x}'
  with patch.object(Path,'read_text',return_value=mount):
   with self.assertRaises(ValueError):runner.mapped_file_identity(str(source),fields)
 def test_source_drift_rejected(self):
  ngx=self.root/'libnvidia-ngx-dlss.so.1';ngx.write_bytes(b'changed')
  with self.assertRaises(ValueError):runner.verify_native_mappings('',{str(ngx):'0'*64})
 def test_online_module_mapping_requires_exact_pinned_source(self):
  module=self.root/'libbbnet20.so';module.write_bytes(b'online module fixture');st=module.stat()
  mapped=f'1000-2000 r-xp 00000000 {os.major(st.st_dev):02x}:{os.minor(st.st_dev):02x} {st.st_ino} {module}\n'
  artifacts={str(module):runner.sha(module)}
  self.assertEqual(runner.verify_native_mappings(mapped,artifacts)[0]['source'],str(module))
  with self.assertRaises(ValueError):runner.verify_native_mappings(mapped,{})
  with self.assertRaises(ValueError):runner.verify_native_mappings(mapped.replace(str(module),'/wrong/libbbnet20.so'),artifacts)
if __name__=='__main__':unittest.main()
