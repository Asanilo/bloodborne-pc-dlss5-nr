// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_async_owner.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace BbNativeAsync {
namespace {std::atomic<bool> exit_held{false},exit_requested{false},exit_complete{false},exit_abnormal_empty{false};}
bool OptIn() noexcept {
 static const bool enabled=[] {
  const char* on=std::getenv("BB_NR_NATIVE_ASYNC");
  const char* session=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
  if(!on||std::strcmp(on,"1")||!session||!session[0]||std::strlen(session)>64)return false;
  for(const char* p=session;*p;++p)
   if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'))return false;
  for(const char* key:{"BB_NR_P0_MODE","BB_NR_Q1_MODE","BB_NR_C4_MODE","BB_NR_C3_MODE","BB_NR_C2_MODE","BB_NR_C1_MODE"}) {
   const char* v=std::getenv(key);if(v&&v[0])return false; // old renderer paths remain exclusive
  }
  return true;
 }();return enabled;
}
void RequestExit()noexcept{exit_requested.store(true);}
bool ExitRequested()noexcept{return exit_requested.load();}
void PublishExitHeld()noexcept{exit_held.store(true,std::memory_order_release);}
bool ExitHeld()noexcept{return exit_held.load(std::memory_order_acquire);}
void PublishExitComplete()noexcept{exit_complete.store(true);}
void PublishAbnormalEmptyExitComplete()noexcept{exit_abnormal_empty.store(true,std::memory_order_release);exit_complete.store(true,std::memory_order_release);}
bool ExitAbnormalEmpty()noexcept{return exit_abnormal_empty.load(std::memory_order_acquire);}
bool ExitComplete()noexcept{return exit_complete.load();}
Owner::Owner():thread([this](std::stop_token token){Run(token);}){}
Owner::~Owner(){Stop();}
bool Owner::TryPost(const std::shared_ptr<Node>& node) {
 std::unique_lock lock(mutex,std::try_to_lock);
 if(!lock||stopped||!node||!Finite(node->frame)||!node->frame.copy_tick)return false;
 for(const auto& p:pending) {if(p==node)return false;}
 for(auto& p:pending) {if(!p){p=node;return true;}}
 return false;
}
bool Owner::TryObserve(const Frame& origin,const CompletionNotice& notice) {
 std::unique_lock lock(mutex,std::try_to_lock);
 if(!lock||stopped||!NoticeMatches(origin,notice))return false;
 ++observed;return true; // never grants an edit/returned view; adapter pending canonical
}
void Owner::Stop(){
 {std::scoped_lock lock(mutex);stopped=true;}thread.request_stop();cv.notify_all();
 if(thread.joinable())thread.join();
}
void Owner::Run(std::stop_token token) {
 std::unique_lock lock(mutex);
 while(!token.stop_requested()) {
  cv.wait(lock,token,[&]{if(stopped)return true;for(const auto& p:pending)if(p&&p->gpu_ready.load(std::memory_order_acquire))return true;return false;});
  if(stopped||token.stop_requested())break;
  for(auto& p:pending)if(p&&p->gpu_ready.load(std::memory_order_acquire)) {
   latest_source.store(p->frame.source);++ready_count;p.reset();
  }
 }
}
}
