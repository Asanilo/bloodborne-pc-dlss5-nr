"""Pause actual Gfx/ASC producer blocks after enqueue, before counter publication."""
from pathlib import Path
import tempfile,subprocess
ROOT=Path(__file__).resolve().parents[2]
text=(ROOT/'gpu/shadps4/video_core/amdgpu/liverpool.cpp').read_text()
def scope_after(start):
 a=text.index('\n    {',start)+5;i=a+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 block=text[a:i]
 return block.replace('queue.submits.emplace(task.handle);','queue.submits.emplace(task.handle);paused.count_down();resume.wait();')
gfx=scope_after(text.index('auto prep_submission =',text.index('void Liverpool::SubmitGfx')))
asc=scope_after(text.index('const auto& task =',text.index('void Liverpool::SubmitAsc')))
code=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <latch>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
using u64=uint64_t;constexpr u64 NoSeq=~u64(0);
namespace BbFreeCheck{template<class...T>void NoteSubmit(T...) {}}
namespace BbTimeline{constexpr int GuestSubmit=0;template<class...T>void Note(T...) {}}
struct Task{int handle=1;};struct Span{const unsigned* data(){return nullptr;}size_t size_bytes(){return 0;}};
struct Copy{const unsigned* guest_dcb=nullptr;};struct Prep{void Enqueue(u64,int){}};
struct State{
 struct Queue{std::mutex m_access;std::queue<int> submits;}queue;
 std::mutex submit_mutex;std::atomic<unsigned> num_submits{0};u64 submissions_total=0,gfx_submitted=0,gfx_submit_seq=0;std::atomic<unsigned> num_mapped_queues{1};unsigned gnm_vqid=1;bool work_retired=true;
 std::latch paused{1},resume{1};Prep* draw_prep=nullptr;int prep_submission=0;Span dcb,ccb;std::shared_ptr<Copy> copy;
 template<class...T>Task ProcessGraphics(T...){return {};}
 void Gfx()
'''+gfx+r'''
 void Asc(){Task task;
'''+asc+r'''
 }
};
void test(bool gfx){
 State s;std::thread producer([&]{if(gfx)s.Gfx();else s.Asc();});s.paused.wait();assert(s.num_submits==0&&s.submissions_total==0);
 {std::unique_lock locked(s.queue.m_access,std::try_to_lock);assert(!locked.owns_lock());} // unpublished task cannot be read
 std::latch consumer_attempt{1};std::thread consumer([&]{consumer_attempt.count_down();std::lock_guard lock(s.queue.m_access);assert(!s.queue.submits.empty());std::lock_guard global(s.submit_mutex);assert(s.num_submits==1&&s.submissions_total==1&&!s.work_retired);s.queue.submits.pop();--s.num_submits;});
 consumer_attempt.wait();s.resume.count_down();producer.join();consumer.join();assert(s.num_submits==0&&s.submissions_total==1);if(gfx)assert(s.gfx_submitted==1);else assert(s.num_mapped_queues==2);
}
int main(){test(true);test(false);}
'''
with tempfile.TemporaryDirectory(prefix='nr-submit-publish-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code);subprocess.run(['c++','-std=c++20','-O2','-pthread',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('Actual Gfx/ASC paused-before-count publication CPU tests PASS')
