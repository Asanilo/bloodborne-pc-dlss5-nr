"""CPU rejection tests for immutable FG resource generations; no GPU completion claims."""
from pathlib import Path
import subprocess,tempfile,unittest
class LayoutTests(unittest.TestCase):
 def test_extents_formats_and_ack_generation(self):
  root=Path(__file__).resolve().parents[2]
  code=r'''
#include "gpu/upscale/dlssg_gpu_contract.h"
#include "gpu/upscale/dlssg_multiplier_policy.h"
#include <cassert>
using namespace DlssgGpu;
template<class F>void rejects(F f){bool fail=false;try{f();}catch(...){fail=true;}assert(fail);}
int main(){
 J config{{"multiplier",2},{"allowed_multipliers",{2,3,4,5,6}}};
 for(unsigned m=2;m<=6;++m)assert(RequestedMultiplier(config,m)==m);
 auto fixed=config;fixed["allowed_multipliers"]={2};assert(RequestedMultiplier(fixed,6)==2);
 fixed.erase("allowed_multipliers");assert(RequestedMultiplier(fixed,6)==2);
 rejects([&]{RequestedMultiplier(config,7);});
 for(const auto& policy:{J{2,3},J{2,3,4,5,7},J{true},J{2.0,3,4,5,6}}){
   auto bad=config;bad["allowed_multipliers"]=policy;rejects([&]{RequestedMultiplier(bad,3);});
 }
 Layout old;assert(old==Layout::From(J::object()));
 for(const auto& layout:{Layout{1706,960,2560,1440},Layout{1130,636,1920,1080},Layout{2560,1440,2560,1440},Layout{1706,720,2560,1080},Layout{1280,800,1920,1200}}){
  J wire{{"layout",layout.Json()},{"resource_epoch",7}};assert(Layout::From(wire)==layout);
  auto bad=wire;bad["layout"]["render_width"]=-1;rejects([&]{Layout::From(bad);});
  bad=wire;bad["layout"]["render_height"]=1.5;rejects([&]{Layout::From(bad);});
  bad=wire;bad["layout"]["color_format"]=97;rejects([&]{Layout::From(bad);});
  bad=wire;bad["layout"]["source_rect_origin"]={1,0};rejects([&]{Layout::From(bad);});
  bad=wire;bad["layout"]["output_width"]=8193;rejects([&]{Layout::From(bad);});
  bad=wire;bad["layout"]["output_width"]=7680;rejects([&]{Layout::From(bad);});
  J request{{"schema",Schema},{"session","cpu"},{"sequence",1},{"epoch",1},{"source_game_frame",9},{"previous_source_game_frame",0},{"display",10},{"native_input_tick",100},{"native_input_GPU_complete",true},{"reset",true},{"wire_order","game-scene-bytes"},{"layout",layout.Json()},{"resource_epoch",7}};
  Request(request,"cpu",0,0,0,0);auto ack=Pair(request);ack.update(J{{"helper_serial_echo",2},{"native_last_use_tick",101},{"native_GPU_complete",true},{"output_released",true}});Consumed(ack,request,2);
  ack["resource_epoch"]=6;rejects([&]{Consumed(ack,request,2);});ack["resource_epoch"]=7;ack["layout"]=old.Json();rejects([&]{Consumed(ack,request,2);});
 }
}
'''
  with tempfile.TemporaryDirectory() as d:
   exe=Path(d)/'layout'
   subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),'-I',str(root/'gpu/third_party/nlohmann/include'),'-x','c++','-','-o',str(exe)],input=code,text=True,check=True)
   subprocess.run([exe],check=True)
 def test_profiles_admit_gpu_and_keep_fallback_limits(self):
  import sys
  sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'nr_dev'))
  import runner
  base=['start','cpu','--daily','--local-inputs','/inputs','--nr-sr-preset','quality','--nr-output','2560x1440']
  args=runner.parse_args(base+['--fg-backend','dlssg','--dlssg-model','/model','--sr-only'])
  self.assertEqual(args.dlssg_transport,'gpu-dmabuf-v1')
  for backend in ('fsr31','fsr316'):
   for nr_args in (['--sr-only'],['--fg-native']):
    args=runner.parse_args(base+['--fg-backend',backend,*nr_args])
    self.assertEqual(args.fg_backend,backend)
 def test_fsr_dimensions_reject_old_native_before_gpu_launch(self):
  import sys
  sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'nr_dev'))
  import runner
  args=runner.parse_args(['start','cpu','--daily','--local-inputs','/inputs','--sr-only',
      '--nr-sr-preset','balanced','--nr-output','2560x1440','--fg-backend','fsr31'])
  with tempfile.TemporaryDirectory() as directory:
   build=Path(directory);(build/'gpu').mkdir()
   lib=build/'gpu/libbbgpu.so'
   lib.write_bytes(b'FG_BACKEND selected= FG_EXIT_PROOF BB_FRAME_GEN_NATIVE')
   with self.assertRaisesRegex(ValueError,'dynamic-source Native'):
    runner.check_fg_capability(args,build)
   lib.write_bytes(lib.read_bytes()+b' FG_FSR_DYNAMIC_V1')
   runner.check_fg_capability(args,build)
if __name__=='__main__':unittest.main()
