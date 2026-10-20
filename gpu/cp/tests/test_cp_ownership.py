#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile actual CP/GNM/sampler function bodies with CPU-only collaborators, plus negative controls.

Does not start CP threads, Vulkan, guest code or a Helper. Full engine integration is separate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

def function(text, signature):
    start=text.index(signature)
    opening=text.index("{",start)
    depth=1
    end=opening+1
    while depth:
        depth+=(text[end]=="{")-(text[end]=="}")
        end+=1
    return text[start:end]

PREFIX=r'''
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>
#include <algorithm>
#include "cp/submission_watermark.h"
#include "cp/cp_diagnostics.h"
#include "bbport_pm4_cold_trace.h"
#include "bbport_resource_trace.h"
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
namespace BbGnm {
bool locked=true, decode=false; unsigned released=0; u64 retired=0;
bool SubmitLockOnDecode(){return decode;}
void ReleaseSubmissionLock(){locked=false;++released;}
void NoteFramesRetired(u64 frame){retired=frame;}
}
namespace BbIrq {
enum class Line {GpuIdle}; std::function<void()> hook;
void Raise(Line){if(hook)hook();}
}
namespace BbTimeline {enum {GuestSubmit,PipeTask}; void Note(int,u64,u64){} }
namespace BbFreeCheck {void NoteSubmit(u64,const u32*,size_t){}}
namespace BbStats {std::atomic<unsigned> asc_submits{};}
namespace BbToggle {constexpr unsigned PipelinedTasks=1;}
struct {bool IsCopyGpuBuffers(){return false;}} EmulatorSettings;
#define ASSERT_MSG(c,...) assert(c)
namespace Vulkan {
struct DrawPipe {enum {ReasonSubmissionEnd};};
struct DrawPreparation {
 std::shared_ptr<int> Build(std::span<const u32>){return {};}
 void Enqueue(u64,std::shared_ptr<int>){}
};
struct Rasterizer {
 static inline bool honest=false;
 bool pipe=false;
 std::function<void()> on_submit;
 std::vector<std::function<void()>> completions;
 bool PipeSignals(){return pipe;}
 static bool HonestLabels(){return honest;}
 void DrainDrawPipe(unsigned){} void WaitDeferredSignals(){} void Flush(){} void Finish(){}
 void SubmitForSignals(){} u64 CurrentTick(){return 1;}
 void OnSubmit(){if(on_submit)on_submit();}
 DrawPreparation& GetDrawPreparation(){static DrawPreparation p;return p;}
 template<class F>void SignalAfterGpu(F f){completions.emplace_back(f);}
 template<class F>void RunInOrder(F f,const void* p,size_t,unsigned=0,bool=false){
  f(*this,static_cast<const u8*>(p));
 }
};
}
using Vulkan::DrawPipe;
namespace BbCp {
struct SubmitMutex {
 std::mutex mutex;
 std::function<void()> before_lock;
 void lock(){if(before_lock)before_lock();mutex.lock();}
 void unlock(){mutex.unlock();}
};
namespace Diagnostics {
void InspectSubmittedCopy(SubmittedCopy&){} void RememberSubmittedCopy(std::shared_ptr<const SubmittedCopy>){}
}
struct CommandProcessor {
 static constexpr u32 GfxQueueId=0,NumTotalQueues=3;
 using SubmittedCopy=Diagnostics::SubmittedCopy;
 struct Work {u32 queue{};u64 seq{};std::span<const u32>dcb,ccb;std::shared_ptr<SubmittedCopy>copy;};
 struct Queue{std::mutex mutex;std::deque<std::unique_ptr<Work>>works;};
 std::array<Queue,3>queues;
 SubmitMutex submit_mutex;std::condition_variable submit_cv;
 std::atomic<u32> num_submits{},num_mapped_queues{1};
 std::atomic<bool> submit_done{},work_retired{true};
 u64 submissions_total{},decoded_total{},gfx_submit_seq{};
 std::array<u64,3>queue_submitted{},queue_decoded{};
 std::deque<SubmissionWatermark<3>>frame_ends;
 Vulkan::Rasterizer* rasterizer{};
 void SubmitGfx(std::span<const u32>,std::span<const u32>,bool);
 void SubmitAsc(u32,std::span<const u32>);
 void SubmitDone(u64) noexcept;
 void SignalDecodedFrames();void SignalGpuIdle(u64);void AfterBatch();
};
'''
SAMPLER=r'''
}
using VkSampler=void*;
struct Sampler {
 static inline unsigned destroyed=0;
 std::unique_ptr<int> handle;
 size_t lru_id{};
 int* Handle(){return handle.get();}
 explicit Sampler(size_t id):handle(new int(1)),lru_id(id){}
 Sampler(Sampler&&)=default;Sampler&operator=(Sampler&&)=default;
 ~Sampler(){if(handle)++destroyed;}
};
struct Scheduler {
 u64 tick=11, completed=0;
 u64 CurrentTick(){return tick;}
 Scheduler* GetWorkSemaphore(){return this;}
 u64 KnownGpuTick(){return completed;}
 std::vector<std::pair<u64,std::move_only_function<void()>>> pending;
 void DeferOperation(std::move_only_function<void()> f){pending.emplace_back(tick,std::move(f));}
 void Complete(u64 at){
  completed=at;
  for(auto it=pending.begin();it!=pending.end();)
   if(it->first<=completed){it->second();it=pending.erase(it);}else ++it;
 }
};
struct Lru {
 std::vector<u64> keys;
 void Free(size_t){}
 template<class F>void ForEachItemBelow(u64,F f){for(auto key:keys)if(f(key))break;keys.clear();}
};
struct TextureCache {
 std::map<u64,Sampler>samplers;std::mutex samplers_mutex;Lru sampler_lru;Scheduler scheduler;
 u64 trigger_gc_samplers=1,pressure_gc_samplers=10,critical_gc_samplers=20,gc_tick=200;
 void GarbageCollectSamplers();
};
'''
MAIN=r'''
int main(){
 using BbCp::CommandProcessor;
 // Hold the producer just before it acquires submit_mutex. Its queue must still be
 // locked: otherwise a consumer can see the work before its count is published.
 for(bool graphics : {true,false}) {
  CommandProcessor publish;
  std::atomic<bool> waiting{},release{};
  publish.submit_mutex.before_lock=[&]{
   waiting.store(true,std::memory_order_release);
   while(!release.load(std::memory_order_acquire))std::this_thread::yield();
  };
  std::array<u32,2> commands{1,2};
  std::thread producer([&]{
   if(graphics)publish.SubmitGfx(commands,{},true);else publish.SubmitAsc(1,commands);
  });
  while(!waiting.load(std::memory_order_acquire))std::this_thread::yield();
  const bool visible=publish.queues[graphics?0:1].mutex.try_lock();
  if(visible)publish.queues[graphics?0:1].mutex.unlock();
  release.store(true,std::memory_order_release);
  producer.join();
  assert(!visible && publish.num_submits==1 && publish.submissions_total==1);
 }
 CommandProcessor cp;
 std::array<u32,3> dcb{1,2,3};std::array<u32,2> ccb{4,5};
 cp.SubmitGfx(dcb,ccb,true);
 assert(cp.num_submits==1 && cp.submissions_total==1 && cp.queue_submitted[0]==1);
 auto& w=*cp.queues[0].works.front();
 if(BbPm4Cold::Enabled()) {
  assert(w.copy->cold_trace && w.copy->cold_digest==BbPm4Cold::Digest(w.dcb));
  assert(w.copy->cold_copy_tid>0 && w.copy->cold_copy_end_ns>=w.copy->cold_copy_begin_ns);
  assert(!w.copy->cold_framing_valid); // this fixture deliberately uses non-PM4 words
 } else assert(!w.copy->cold_trace);
 assert(w.copy && w.dcb.data()!=dcb.data() && w.ccb.data()!=ccb.data());
 assert(w.copy->guest_dcb==w.dcb.data() && w.copy->guest_ccb==w.ccb.data());
 dcb.fill(99);ccb.fill(98);
 assert(w.dcb[0]==1 && w.ccb[0]==4); // transient caller storage is not the decoded copy
 cp.SubmitAsc(1,dcb);
 assert(cp.num_submits==2 && cp.submissions_total==2 && cp.queue_submitted[1]==1);
 cp.SubmitDone(7);
 cp.queue_decoded={0,2,9};cp.decoded_total=11;
 cp.SignalDecodedFrames();assert(BbGnm::retired==0);
 cp.queue_decoded={1,1,0};cp.SignalDecodedFrames();assert(BbGnm::retired==7);

 // Actual AfterBatch: an OnSubmit callback stands in for an arriving SubmitDone.
 CommandProcessor notify;Vulkan::Rasterizer raster;notify.rasterizer=&raster;
 raster.on_submit=[&]{notify.SubmitDone(0);};
 notify.submit_done=true;notify.AfterBatch();assert(notify.submit_done);

 CommandProcessor idle;idle.submissions_total=2;BbGnm::released=0;BbGnm::locked=true;
 idle.SignalGpuIdle(1);assert(BbGnm::released==0 && BbGnm::locked);
 BbIrq::hook=[&]{
  // A new submission after the transaction must not be unlocked by the old IRQ.
  assert(BbGnm::released==1);
  std::scoped_lock lock{idle.submit_mutex};
  ++idle.submissions_total;idle.num_submits=1;BbGnm::locked=true;
 };
 idle.SignalGpuIdle(2);
 assert(BbGnm::locked && BbGnm::released==1);
 BbIrq::hook={};

 TextureCache cache;cache.samplers.emplace(1,Sampler{1});cache.sampler_lru.keys={1};
 const unsigned before=Sampler::destroyed;
 cache.GarbageCollectSamplers();
 assert(cache.samplers.empty() && Sampler::destroyed==before);
 assert(cache.scheduler.pending.size()==1 && cache.scheduler.pending[0].first==11);
 cache.scheduler.Complete(10);assert(Sampler::destroyed==before);
 cache.scheduler.Complete(11);assert(Sampler::destroyed==before+1);
 std::puts("PASS: actual CP force_copy/per-queue retirement/notification exchange/generation release and actual sampler GC move-owning tick retirement");
}
'''

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[3] # gpu/cp/tests -> repository
    cp=(root/"gpu/cp/command_processor.cpp").read_text()
    texture=(root/"gpu/textures/texture_cache_memory.cpp").read_text()
    signatures=[
        "void CommandProcessor::SubmitGfx(", "void CommandProcessor::SubmitAsc(",
        "void CommandProcessor::SubmitDone(", "void CommandProcessor::SignalDecodedFrames(",
        "void CommandProcessor::SignalGpuIdle(", "void CommandProcessor::AfterBatch("]
    bodies="\n".join(function(cp,s) for s in signatures)
    sampler=function(texture,"void TextureCache::GarbageCollectSamplers(")
    diagnostic=(root/"gpu/cp/cp_diagnostics.cpp").read_text()
    dump="namespace Diagnostics {\n"+function(diagnostic,"void DumpColdSubmittedCopy(")+"\n}\n"
    source=PREFIX+dump+bodies+SAMPLER+sampler+MAIN
    variants={"current":source,
              "early_queue_visibility":source.replace(
                  "        std::scoped_lock submit_lock{submit_mutex};",
                  "        queue.mutex.unlock();\n        std::scoped_lock submit_lock{submit_mutex};"
              ).replace("        work_retired = false;\n    }",
                        "        work_retired = false;\n        queue.mutex.lock();\n    }"),
              "lost_notification":source.replace("if (submit_done.exchange(false)) {","if (submit_done) {").replace("    SignalDecodedFrames();","    submit_done = false;\n    SignalDecodedFrames();"),
              "early_sampler_destroy":source.replace("scheduler.DeferOperation([retired = std::move(sampler)] { (void)retired; });",""),
              "late_idle_release":source.replace("            BbGnm::ReleaseSubmissionLock();","")}
    with tempfile.TemporaryDirectory(prefix="bb-cp-ownership-") as temp:
        out=args.output or Path(temp)
        out.mkdir(parents=True,exist_ok=True)
        results={}
        for name,text in variants.items():
            src=out/(name+".cpp"); exe=out/name;src.write_text(text)
            subprocess.run(["clang++","-std=c++23","-O1","-g","-UNDEBUG","-Wall","-Wextra",
                            "-Werror","-pthread","-I"+str(root/"gpu"),"-I"+str(root/"gpu/shim"),str(src),"-o",str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,env={**os.environ,"BB_NR_PM4_COLD_TRACE":"0","BB_NR_RESOURCE_TRACE":"0"})
            (out/(name+".log")).write_text(result.stdout+result.stderr)
            assert (result.returncode==0)==(name=="current"), (name,result.returncode,result.stderr)
            results[name]={"exit":result.returncode,"expected_rejection":name!="current"}
        cold=subprocess.run([str(out/"current")],capture_output=True,text=True,
            env={**os.environ,"BB_NR_PM4_COLD_TRACE":"1","BB_NR_RESOURCE_TRACE":"1"})
        (out/"current-cold.log").write_text(cold.stdout+cold.stderr)
        assert cold.returncode==0,(cold.returncode,cold.stderr)
        results["current_cold_enabled"]={"exit":cold.returncode}
        results["source_sha256"]={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in
            ["gpu/cp/command_processor.cpp","gpu/textures/texture_cache_memory.cpp",
             "gpu/cp/submission_watermark.h","gpu/cp/cp_diagnostics.cpp",
             "gpu/cp/cp_diagnostics.h","gpu/shim/bbport_pm4_cold_trace.h"]}
        (out/"results.json").write_text(json.dumps(results,indent=2)+"\n")
        print(json.dumps(results,indent=2))
if __name__=="__main__":main()
