"""Offline schema/source/bypass/format regressions; no GPU launch or historical writes."""
import importlib.util,json,sys,tempfile,unittest
from pathlib import Path
import numpy as np
HERE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(HERE));spec=importlib.util.spec_from_file_location('nr_compare',HERE/'compare.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CompareTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.manifest=self.root/'readback.json';self.log=self.root/'game.log';assets=[]
  for name,array in ((m.RAW,np.ones((720,1280,4),dtype='<f2')),(m.NR,np.ones((720,1280,4),dtype='<f2')),('active-edit-confidence720.rgba16f',np.ones((720,1280,4),dtype='<f2')),('active-depth-guide-age720.rgba32f',np.zeros((720,1280,4),dtype='<f4')),('bank-state.u32',np.zeros(8,dtype='<u4')),('return-guard.u32',np.zeros(1,dtype='<u4')),('current-depth720.r32f',np.ones((720,1280),dtype='<f4')),('current-motion720.rg32f',np.zeros((720,1280,2),dtype='<f4'))):
   p=self.root/name;p.write_bytes(array.tobytes());assets.append({'file':name,'bytes':p.stat().st_size,'sha256':m.sha(p)})
  self.meta={'schema':'persistent-history-cold-v2','session':'cpu','capture_seq':1,'control_revision':8,'current_source':5,'image_extent':[1280,720],'image_formats_vk':[97,97,97,109,100,103],'source_image_handles':[11,22,33,44,55,66],'copy_target_tick':10,'copy_observed_tick':10,'assets':assets};self.manifest.write_text(json.dumps(self.meta));self.set_log(False)
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
