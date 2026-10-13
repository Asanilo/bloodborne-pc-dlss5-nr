// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
namespace BbNativeAsync {
// Local scene-arm only, not Helper wire. No Scheduler, Vulkan or renderer polling.
// One managed thread, joined before member teardown.
class Frontend {
public:
 Frontend() {
  const char* r=std::getenv("BB_NR_NATIVE_ASYNC_RUN_DIR");
  const char* p=std::getenv("BB_NR_NATIVE_ASYNC_SCENE_ARM");
  const char* s=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
  if(!r||!p||!s)return;
  const std::string run(r),path(p),session(s);
  if(path!=run+"/native-scene-arm"||run!=
    "/home/arinp/Code/nrbridge-design/game-poc-20261011/bbport/runs/native-async-game-"+session)return;
  thread=std::jthread([this,path,session](std::stop_token token){
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(300);
   while(!token.stop_requested()&&std::chrono::steady_clock::now()<deadline) {
    std::ifstream f(path,std::ios::binary);std::string value;
    if(f){char b[65]{};f.read(b,sizeof(b));value.assign(b,size_t(f.gcount()));
      if(value==session+"\n"){
       armed.store(true,std::memory_order_release);
       std::printf("NATIVE_ASYNC_SCENE_ARM session=%s actual_scene_evidence=REQUIRES_ROOT no_GPU_proof=1\n",session.c_str());
       return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
   }
  });
 }
 ~Frontend(){if(thread.joinable()){thread.request_stop();thread.join();}}
 bool Armed()const noexcept{return armed.load(std::memory_order_acquire);}
private:
 std::atomic<bool> armed{false};
 std::jthread thread;
};
}
