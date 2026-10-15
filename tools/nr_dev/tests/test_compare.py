"""Offline schema/source/bypass/format regressions; no GPU launch or historical writes."""
import importlib.util,json,sys,tempfile,unittest
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(HERE));spec=importlib.util.spec_from_file_location('nr_compare',HERE/'compare.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
import readback
class TransitionTests(unittest.TestCase):
 def setUp(self):
  self.m={'active_bank':0,'candidate_bank':2,'transition_active':True,'transition_weight':.25,'ttl_frames':12,'ttl_ms':200,'min_confidence':.5}
  self.old=np.array([[[1,2,3,1]]],dtype=np.float32);self.target=np.array([[[5,6,7,1]]],dtype=np.float32);self.meta=np.array([[[1,0,8,100]]],dtype=np.float32);self.tm=np.array([[[1,0,2,20]]],dtype=np.float32);self.state=np.zeros(8,dtype=np.uint32);self.guard=np.zeros(1,dtype=np.uint32)
 def sample(self):return readback.effective_history(self.m,self.old,self.meta,self.state,self.guard,self.target,self.tm)
 def test_both_log_mix_and_oldest_age(self):
  valid,age,ms,edit,_=self.sample();self.assertTrue(valid.all());np.testing.assert_allclose(edit,[[[2,3,4]]]);self.assertEqual(age.item(),8);self.assertEqual(ms.item(),100)
 def test_endpoints_only_participating_age(self):
  self.m['transition_weight']=1;self.assertEqual(self.sample()[1].item(),2);self.m['transition_weight']=0;self.assertEqual(self.sample()[1].item(),8)
 def test_single_valid_full_strength(self):
  self.old[...,3]=0;self.m['transition_weight']=0;valid,age,_,edit,_=self.sample();self.assertTrue(valid.all());self.assertEqual(age.item(),2);np.testing.assert_equal(edit,self.target[...,:3])
 def test_target_fatal_preserves_old(self):
  self.state[2]=1;self.m['transition_weight']=1;valid,age,_,edit,_=self.sample();self.assertTrue(valid.all());self.assertEqual(age.item(),8);np.testing.assert_equal(edit,self.old[...,:3])
 def test_guard_raw_and_local16_not_fatal(self):
  self.state[2]=16;self.assertTrue(self.sample()[0].all());self.guard[0]=1;self.assertFalse(self.sample()[0].any())
 def test_legacy_inactive_ignores_target(self):
  del self.m['transition_active'];self.assertEqual(self.sample()[1].item(),8)
class CompareTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.manifest=self.root/'readback.json';self.log=self.root/'game.log';assets=[]
  for name,array in ((m.RAW,np.ones((720,1280,4),dtype='<f2')),(m.NR,np.ones((720,1280,4),dtype='<f2')),('active-edit-confidence720.rgba16f',np.ones((720,1280,4),dtype='<f2')),('active-depth-guide-age720.rgba32f',np.zeros((720,1280,4),dtype='<f4')),('bank-state.u32',np.zeros(8,dtype='<u4')),('return-guard.u32',np.zeros(1,dtype='<u4')),('current-depth720.r32f',np.ones((720,1280),dtype='<f4')),('current-motion720.rg32f',np.zeros((720,1280,2),dtype='<f4'))):
   p=self.root/name;p.write_bytes(array.tobytes());assets.append({'file':name,'bytes':p.stat().st_size,'sha256':m.sha(p)})
  self.meta={'schema':'persistent-history-cold-v2','session':'cpu','capture_seq':1,'control_revision':8,'current_source':5,'image_extent':[1280,720],'image_formats_vk':[97,97,97,109,100,103],'source_image_handles':[11,22,33,44,55,66],'copy_target_tick':10,'copy_observed_tick':10,'assets':assets};self.manifest.write_text(json.dumps(self.meta));self.set_log(False)
 def test_readback_old_v2_without_transition(self):
  self.meta.update(performance_excluded=True,bank_state_actual=[0]*8,return_guard_actual=0,active_bank=0,candidate_bank=2,sample_index=0,capture_source_gap=0,host_history_potential_only=True)
  self.manifest.write_text(json.dumps(self.meta));audit,_=readback.analyze(self.manifest,self.text);self.assertTrue(audit['integrity_pass']);self.assertFalse(audit['transition_active'])
 def test_extended_v2_assets_and_sha(self):
  self.meta.update(transition_active=False,transition_weight=1,transition_start_source=0,transition_start_ms=0);self.meta['image_formats_vk'] += [97,109];self.meta['source_image_handles'] += [77,88]
  for name,dtype in [('target-edit-confidence720.rgba16f','<f2'),('target-depth-guide-age720.rgba32f','<f4')]:
   p=self.root/name;p.write_bytes(np.zeros((720,1280,4),dtype=dtype).tobytes());self.meta['assets'].append({'file':name,'bytes':p.stat().st_size,'sha256':m.sha(p)})
  self.manifest.write_text(json.dumps(self.meta));m.decode(self.manifest,self.log)
  (self.root/'target-edit-confidence720.rgba16f').write_bytes(b'bad')
  with self.assertRaises(ValueError):m.decode(self.manifest,self.log)
 def tearDown(self):self.tmp.cleanup()
 def set_log(self,bypass):
  self.text=f'NATIVE_ASYNC_PRE_SR_SR_READ session=cpu source=5 actual_input_image={hex(11 if bypass else 22)} native_sr_read_tick=10 SR_success=1 cache_selected={0 if bypass else 1}\nNATIVE_ASYNC_PRE_SR_CURRENT_PRESENT session=cpu source_game_frame=5 present_frame_number=5 merge_tick=11 prepare_tick=11 ready_tick=11 swapchain_present=1 cache_selected={0 if bypass else 1}\n';self.log.write_text(self.text)
 def test_v2_selected_and_bypassed(self):
  _,_,proof=m.decode(self.manifest,self.log,m.sha(self.manifest));self.assertTrue(proof['actual_source_join']['NR_candidate_displayed']);self.set_log(True);_,_,proof=m.decode(self.manifest,self.log);self.assertFalse(proof['actual_source_join']['NR_candidate_displayed']);self.assertTrue(proof['actual_source_join']['actual_raw_image_SR_Present_joined'])
 def test_guide_sha_and_revision_rejected(self):
  (self.root/'current-depth720.r32f').write_bytes(b'bad')
  with self.assertRaises(ValueError):m.decode(self.manifest,self.log)
 def test_actual_present_bytes_and_identity(self):
  folder=self.root/'runtime-output-capture-1'/'frame-0-source-5-Present';folder.mkdir(parents=True);pixels=folder/'pixels.raw';a=np.zeros((1080,1920,4),dtype='u1');a[0,0]=[1,2,3,255];pixels.write_bytes(a.tobytes());j={'schema':'nr-runtime-output-v1','stage':'Present','capture_seq':1,'source':5,'control_revision':8,'width':1920,'height':1080,'vk_format':44,'bytes':pixels.stat().st_size,'file':'pixels.raw','sha256':m.sha(pixels),'recorded_tick':2,'observed_tick':3,'actual_GPU_complete':True,'capture_position':'postprocess game frame before swapchain letterbox and NR overlay','linear_HDR':False,'raw_NR_manifest':str(self.manifest)};out=folder/'output.json';out.write_text(json.dumps(j));log=self.text+f'NR_RUNTIME_OUTPUT_COMPLETE stage=Present source=5 seq=1 revision=8 target_tick=2 observed_tick=3 bytes={j["bytes"]} path={folder}\n';image,proof=m.output_capture(out,self.manifest,self.meta,log,0);self.assertEqual(image[0,0].tolist(),[3,2,1]);self.assertEqual(proof['stage'],'Present')
  j['control_revision']=9;out.write_text(json.dumps(j))
  with self.assertRaises(ValueError):m.output_capture(out,self.manifest,self.meta,log,0)
 def test_same_transform_reference(self):self.assertEqual(m.srgb_reinhard(np.array([0.,1.]),0).tolist(),[0,188])
if __name__=='__main__':unittest.main()
