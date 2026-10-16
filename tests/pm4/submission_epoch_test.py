"""Compile actual idle-publication/release functions against CPU synchronization stubs."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
gnm=(ROOT/'gpu/shadps4/core/libraries/gnmdriver/gnmdriver.cpp').read_text()
cp=(ROOT/'gpu/shadps4/video_core/amdgpu/liverpool.cpp').read_text()
def function(text,signature):
 a=text.index(signature);i=text.index('{',a)+1;depth=1
 while depth:
  depth += (text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
release=function(gnm,'void ReleaseSubmissionLock()')
idle=function(cp,'void Liverpool::SignalGpuIdle(u64 generation)')
assert 'ResetSubmissionLock' not in gnm
# Take the actual decode-mode transaction, not an independently copied implementation.
a=cp.index('if (Libraries::GnmDriver::SubmitLockOnDecode())');decode=function(cp[a:],'if (Libraries::GnmDriver::SubmitLockOnDecode())')
code=r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <latch>
#include <mutex>
#include <thread>
using u64=uint64_t;
namespace Libraries::GnmDriver {std::mutex m_wait_idle;std::condition_variable cv_lock;unsigned submission_lock=1;bool SubmitLockOnDecode(){return true;}
'''+release+r'''
}
namespace Platform {enum class InterruptId {GpuIdle};struct IrqC {
 static inline bool block=false;static inline unsigned signals=0;static inline std::latch entered{1},resume{1};static IrqC* Instance(){static IrqC q;return &q;}
 void Signal(InterruptId){++signals;if(block){entered.count_down();resume.wait();}}
};}
struct Liverpool {std::mutex submit_mutex;std::atomic<unsigned> num_submits{0};std::atomic<bool> work_retired{false};u64 submissions_total=1;void SignalGpuIdle(u64 generation);void ReleaseDecoded(u64 generation){
'''+decode+r'''
}};
'''+idle+r'''
unsigned lock_value(){std::lock_guard lk(Libraries::GnmDriver::m_wait_idle);return Libraries::GnmDriver::submission_lock;}
void lock_new(){std::lock_guard lk(Libraries::GnmDriver::m_wait_idle);Libraries::GnmDriver::submission_lock=1;}
int main(){
 Liverpool q;
 // IRQ notification is delayed after actual release. A new submission/lock may
 // complete before the notification; the old IRQ must not reset that new lock.
 Platform::IrqC::block=true;std::thread old([&]{q.SignalGpuIdle(1);});Platform::IrqC::entered.wait();assert(lock_value()==0);
 {std::lock_guard lk(q.submit_mutex);q.submissions_total=2;q.num_submits=1;q.work_retired=false;}lock_new();
 Platform::IrqC::resume.count_down();old.join();assert(lock_value()==1&&!q.work_retired);Platform::IrqC::block=false;
 q.num_submits=0;q.SignalGpuIdle(1);assert(lock_value()==1&&!q.work_retired); // stale generation rejected
 q.ReleaseDecoded(1);assert(lock_value()==1);q.ReleaseDecoded(2);assert(lock_value()==0&&!q.work_retired); // decode mode unchanged
 lock_new();q.num_submits=1;q.SignalGpuIdle(2);assert(lock_value()==1);q.num_submits=0;q.SignalGpuIdle(2);assert(lock_value()==0&&q.work_retired);
 // The release may block on the Gnm mutex, but must retain submit_mutex
 // until the check+release transaction is complete (no new epoch can enter).
 lock_new();Libraries::GnmDriver::m_wait_idle.lock();
 std::thread transaction([&]{q.SignalGpuIdle(2);});bool holding=false;
 for(unsigned i=0;i<100000&&!holding;i++){if(q.submit_mutex.try_lock())q.submit_mutex.unlock();else holding=true;std::this_thread::yield();}assert(holding);
 std::atomic<bool> inserted{false};std::thread newer([&]{std::lock_guard lk(q.submit_mutex);q.submissions_total=3;q.num_submits=1;q.work_retired=false;lock_new();inserted=true;});
 assert(!inserted);Libraries::GnmDriver::m_wait_idle.unlock();transaction.join();newer.join();assert(inserted&&lock_value()==1);
 q.num_submits=0;q.SignalGpuIdle(3);assert(lock_value()==0);
 // Readers and repeated true generation releases use the same real Gnm mutex.
 std::thread reader([&]{for(unsigned i=0;i<10000;i++)assert(lock_value()<=1);});
 for(unsigned i=0;i<10000;i++){lock_new();q.SignalGpuIdle(3);}reader.join();assert(lock_value()==0);
}
'''
with tempfile.TemporaryDirectory(prefix='nr-submit-epoch-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code);subprocess.run(['c++','-std=c++20','-O2','-pthread',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('Actual idle/release/decode transaction CPU interleavings PASS')
