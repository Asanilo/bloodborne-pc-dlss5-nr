"""Actual Process boundary block under a deterministic new-SubmitDone interleaving."""
from pathlib import Path
import subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'gpu/shadps4/video_core/amdgpu/liverpool.cpp').read_text()
start=source.index('        if (submit_done')
a=source.index('{',start);i=a+1;depth=1
while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
actual=source[start:i]
# Keep the previous consumption protocol as an explicit negative control; work
# body remains the actual current source, so both exercise the same callbacks.
legacy=actual.replace('if (submit_done.exchange(false))','if (submit_done)')
if 'submit_done = false;' not in legacy:legacy=legacy[:-1]+'submit_done = false;\n}'
header=(ROOT/'gpu/shadps4/video_core/amdgpu/liverpool.h').read_text()
assert 'submit_done = true;' in header
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <latch>
#include <thread>
using u8=uint8_t;
namespace VideoCore{void EndCapture(){}}
namespace Vulkan{struct Rasterizer {
 bool piped=false;unsigned flushes=0,calls=0;std::latch entered{1},resume{1};
 bool PipeSignals(){return piped;}
 void OnSubmit(){if(calls++==0){entered.count_down();resume.wait();}}
 void Flush(){++flushes;}
 template<class Fn>void RunInOrder(Fn f,const void*,size_t){f(*this,nullptr);}
};}
struct Consumer{std::atomic<bool> submit_done{true};Vulkan::Rasterizer* rasterizer;void Boundary(){
'''
suffix=r'''
}};
int main(){for(bool piped:{false,true}){
 Vulkan::Rasterizer renderer;renderer.piped=piped;Consumer c;c.rasterizer=&renderer;
 std::thread owner([&]{c.Boundary();});renderer.entered.wait();
 c.submit_done=true; // actual SubmitDone publication after consumer entered work
 renderer.resume.count_down();owner.join();
 assert(c.submit_done.load()==EXPECT_SURVIVES);
 if(EXPECT_SURVIVES){c.Boundary();assert(!c.submit_done&&renderer.flushes==2);c.Boundary();assert(renderer.flushes==2);}
 else assert(renderer.flushes==1); // lost second boundary, reproduces old bug
}}
'''
with tempfile.TemporaryDirectory(prefix='nr-submit-done-') as directory:
 p=Path(directory)
 for name,body,expected in [('legacy',legacy,False),('actual',actual,'--expect-legacy' not in sys.argv)]:
  cpp=p/(name+'.cpp');exe=p/name;cpp.write_text(prefix+body+suffix.replace('EXPECT_SURVIVES','true' if expected else 'false'))
  subprocess.run(['c++','-std=c++20','-O2','-pthread',str(cpp),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Actual boundary interleaving PASS; legacy loses notification, current expected protocol verified')
