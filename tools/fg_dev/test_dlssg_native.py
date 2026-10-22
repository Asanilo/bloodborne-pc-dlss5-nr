"""Native camera/admission CPU checks; no Vulkan device or Helper launch."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'nr_dev'))
import runner


class NativeDlssgTests(unittest.TestCase):
    def test_explicit_model_and_source_binding_required(self):
        with self.assertRaises(SystemExit):
            runner.parse_args(['start','cpu-dlssg','--daily','--fg-native','--local-inputs','/inputs','--experimental-fg','dlssg'])
        with self.assertRaises(SystemExit):
            runner.parse_args(['start','cpu-dlssg','--daily','--sr-only','--local-inputs','/inputs','--experimental-fg','dlssg','--dlssg-model','/model'])
        args=runner.parse_args(['start','cpu-dlssg','--daily','--fg-native','--local-inputs','/inputs','--experimental-fg','dlssg','--dlssg-model','/model'])
        self.assertTrue(args.fg_native)

    def test_native_camera_depth_and_inverse(self):
        root=Path(__file__).resolve().parents[2]
        code=r'''
#include "gpu/upscale/dlssg_camera.h"
#include <cassert>
using namespace Vulkan::DlssgCamera;
struct Scene {float camera_near=.1f,camera_far=1000,vertical_fov=1.5707963268f;unsigned width=1920,height=1080;float camera_position[3]{3,4,5},camera_right[3]{1,0,0},camera_up[3]{0,1,0},camera_forward[3]{0,0,1};float jitter_x=0,jitter_y=0;};
int main(){Scene s;auto a=Prepare(s,nullptr,true);auto product=Multiply(a.vp,a.inverse_vp);for(unsigned i=0;i<16;++i)assert(std::abs(product[i]-(i%5==0?1:0))<1e-10);
for(double z:{double(s.camera_near),double(s.camera_far)}){double world[]={3,4,5+z,1},clip[4]{};for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)clip[j]+=world[i]*a.vp[i*4+j];assert(std::abs(clip[0])<1e-10&&std::abs(clip[1])<1e-10);assert(std::abs(clip[2]/clip[3]-(z==s.camera_far?1:0))<1e-10);}
s.camera_right[0]=0;s.camera_right[2]=-1;s.camera_forward[0]=1;s.camera_forward[2]=0;auto b=Prepare(s,&a,false);auto to_prev=Multiply(b.inverse_vp,a.vp),from_prev=Multiply(a.inverse_vp,b.vp);product=Multiply(to_prev,from_prev);for(unsigned i=0;i<16;++i)assert(std::abs(product[i]-(i%5==0?1:0))<1e-9);
s.camera_up[1]=0;bool rejected=false;try{Prepare(s,&b,false);}catch(...){rejected=true;}assert(rejected);}
'''
        with tempfile.TemporaryDirectory() as temp:
            exe=str(Path(temp)/'camera')
            subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I',str(root),'-I',str(root/'gpu/third_party/nlohmann/include'),'-x','c++','-','-o',exe],input=code,text=True,check=True)
            subprocess.run([exe],check=True)


if __name__=='__main__':
    unittest.main()
