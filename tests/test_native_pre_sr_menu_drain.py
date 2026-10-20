"""Compile the actual frame-boundary/drain bodies; CPU only, no GPU proof."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from paths import ROOT


def body(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2]


class MenuDrain(unittest.TestCase):
    def test_complete_without_further_sr_requires_real_final_use(self):
        boundary = body('gpu/upscale/vk_temporal_upscaler.cpp',
                        'void TemporalUpscaler::PollNativeFrameBoundary()')
        drain = body('gpu/render/native_async_pre_sr.cpp',
                     'void NativeAsyncPreSr::DrainUnavailable(')
        source = r'''
#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>
struct Scheduler { int render_ends=0;void EndRendering(){++render_ends;} };
struct Instance {};
namespace BbSettings {
struct Settings { int preset=1; bool jitter=true, frame_generation=false; };
Settings& Get(){ static Settings settings; return settings; }
}
struct NativeAsyncPreSrOriginOwner {
 enum class Phase { WaitingComplete, Complete, FinalRecorded, Finalized };
 struct Job { int resources=0, complete=0, final=0; };
 Phase phase=Phase::WaitingComplete;
 std::shared_ptr<Job> job=std::make_shared<Job>();
 bool actual_final_gpu_complete=false, consumed=false;
 Phase State(){return phase;}
 auto TryJob(){return job;}
 void FinalRecorded(const std::shared_ptr<Job>&,int) {phase=Phase::FinalRecorded;}
 void PollActualCompletion(){
  if(phase==Phase::FinalRecorded&&actual_final_gpu_complete){
   consumed=true;phase=Phase::Finalized;
  }
 }
};
struct NativeAsyncPreSr {
 struct SparseDisposition { static constexpr int Unavailable=0; };
 struct Result {int disposition;};
 std::shared_ptr<NativeAsyncPreSrOriginOwner> peer=std::make_shared<NativeAsyncPreSrOriginOwner>();
 bool rejected=false, fail_record=false, supported=false;
 int actual_record_calls=0, tracked=0, polls=0;
 void PollReady(Instance&,Scheduler&,bool support){supported=support;++polls;peer->PollActualCompletion();}
 Result TryRecordSparseComplete(Scheduler&,int,int,int source,int){
  assert(source==0);++actual_record_calls;
  if(fail_record)throw std::runtime_error("partial record failure");
  return {1}; // Commands recorded, never proof of their GPU completion.
 }
 void TrackUse(Scheduler&){++tracked;}
 void QuarantineRecorded(Scheduler&){rejected=true;}
 void DrainUnavailable(Scheduler&);
};
struct TemporalUpscaler {
 NativeAsyncPreSr native_async_pre_sr;
 bool native_pre_sr_source_visited=false;
 Instance instance;Scheduler scheduler;
 bool Scaled(){return false;}bool UseDlss(){return true;}
 void PollNativeFrameBoundary() noexcept;
};
''' + drain + '\n' + boundary + r'''
int main(){
 using Phase=NativeAsyncPreSrOriginOwner::Phase;
 TemporalUpscaler u;auto& n=u.native_async_pre_sr;
 // Completion arrives after the final scene RecordDlss, before the menu.
 u.native_pre_sr_source_visited=true;n.peer->phase=Phase::Complete;
 u.PollNativeFrameBoundary();
 assert(n.supported&&n.actual_record_calls==0&&!n.peer->consumed);
 // No more SR calls. Configuration still reports supported.
 u.PollNativeFrameBoundary();
 assert(n.supported&&n.actual_record_calls==1&&n.tracked==1);
 assert(u.scheduler.render_ends==1);
 assert(n.peer->phase==Phase::FinalRecorded&&!n.peer->consumed);
 u.PollNativeFrameBoundary();
 assert(n.actual_record_calls==1&&!n.peer->consumed);
 n.peer->actual_final_gpu_complete=true;
 u.PollNativeFrameBoundary();assert(n.peer->consumed);
 // Completion can also arrive several menu frames later.
 TemporalUpscaler late;
 late.PollNativeFrameBoundary();late.PollNativeFrameBoundary();
 assert(late.native_async_pre_sr.actual_record_calls==0);
 late.native_async_pre_sr.peer->phase=Phase::Complete;
 late.PollNativeFrameBoundary();
 assert(late.native_async_pre_sr.actual_record_calls==1);
 // Quarantine never permits new shared acquire/release commands.
 TemporalUpscaler held;held.native_async_pre_sr.rejected=true;
 held.native_async_pre_sr.peer->phase=Phase::Complete;
 held.PollNativeFrameBoundary();assert(held.native_async_pre_sr.actual_record_calls==0);
 assert(held.scheduler.render_ends==0);
 TemporalUpscaler partial;partial.native_async_pre_sr.peer->phase=Phase::Complete;
 partial.native_async_pre_sr.fail_record=true;partial.PollNativeFrameBoundary();
 assert(partial.native_async_pre_sr.rejected&&!partial.native_async_pre_sr.peer->consumed);
 partial.PollNativeFrameBoundary();assert(partial.native_async_pre_sr.actual_record_calls==1);
}
'''
        with tempfile.TemporaryDirectory(prefix='bb-pre-sr-menu-') as temporary:
            folder = Path(temporary)
            (folder / 'test.cpp').write_text(source)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17',
                            '-Wall', '-Wextra', '-Werror', str(folder / 'test.cpp'),
                            '-o', str(folder / 'test')], check=True)
            subprocess.run([str(folder / 'test')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
