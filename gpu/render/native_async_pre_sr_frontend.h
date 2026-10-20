// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_scene_first.h"
#include "native_dev_paths.h"
#include "native_async_pre_sr_play.h"
#include "native_async_play.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
namespace BbNativeAsyncPreSr {
namespace SceneFirst=BbNativeAsync::SceneFirst;
// Local scene-arm only, not Helper wire. No Scheduler, Vulkan or renderer polling.
// One managed thread, joined before member teardown.
class Frontend {
public:
 Frontend() {
  const bool scene_first=SceneFirst::Active();
  helper_start_requested.store(!scene_first,std::memory_order_release);
  const char* r=std::getenv("BB_NR_NATIVE_ASYNC_RUN_DIR");
  const char* p=std::getenv("BB_NR_NATIVE_ASYNC_SCENE_ARM");
  const char* s=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
  if(!r||!p||!s)return;
  const std::string run(r),path(p),session(s);
  if(path!=run+"/native-scene-arm"||!ValidDevRun(run,session))throw std::runtime_error("preSR frontend isolated run/session path rejected");
  finished.store(false,std::memory_order_release);
  thread=std::jthread([this,path,run,session,scene_first](std::stop_token token){
   struct Finish{std::atomic<bool>& flag;~Finish(){flag.store(true,std::memory_order_release);}} finish{finished};
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(BbNativeAsyncPreSrPlay::ServiceSeconds());
   while(!token.stop_requested()&&std::chrono::steady_clock::now()<deadline) {
    const auto exact_token=[&](const std::string& target){
     std::ifstream f(target,std::ios::binary);char b[65]{};
     if(!f){return false;}
     f.read(b,sizeof(b));
     return SceneFirst::ExactToken(std::string_view(b,size_t(f.gcount())),session);
    };
    if(scene_first&&!helper_start_requested.load(std::memory_order_acquire)&&exact_token(run+"/native-helper-start")){
     helper_start_requested.store(true,std::memory_order_release);
     std::printf("NATIVE_ASYNC_HELPER_START_REQUEST session=%s scene_first=1 scene_proof=REQUIRES_ROOT GPU_proof=NONE\n",session.c_str());
    }
    if(!armed.load(std::memory_order_acquire)&&exact_token(path)){
     armed.store(true,std::memory_order_release);
     std::printf("NATIVE_ASYNC_SCENE_ARM session=%s actual_scene_evidence=REQUIRES_ROOT no_GPU_proof=1\n",session.c_str());
    }
    if(armed.load(std::memory_order_acquire)&&helper_start_requested.load(std::memory_order_acquire))return;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
   }
  });
 }
 ~Frontend(){if(thread.joinable()){thread.request_stop();thread.join();}}
 void RequestStop()noexcept{thread.request_stop();}
 bool Finished()const noexcept{return finished.load(std::memory_order_acquire);}
 void JoinFinished(){if(Finished()&&thread.joinable())thread.join();}
 bool HelperStartRequested()const noexcept{return helper_start_requested.load(std::memory_order_acquire);}
 bool Joined()const noexcept{return Finished()&&!thread.joinable();}
 bool Armed()const noexcept{return armed.load(std::memory_order_acquire);}
private:
 std::atomic<bool> armed{false},finished{true},helper_start_requested{true};
 std::jthread thread;
};
}
