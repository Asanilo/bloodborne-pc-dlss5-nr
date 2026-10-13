// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
namespace BbNativeAsync {
// Record-time admission only: source copy and conversion were recorded on this
// same Native Scheduler before input copy/release. NOT a GPU completion proof.
struct RecordedOrigin {
 uint64_t source=0,epoch=0,history=0,params=0,copy_tick=0,conversion_tick=0;
 uintptr_t scheduler_identity=0;
 bool Matches(const Frame&f,uintptr_t scheduler,uint64_t current_tick)const noexcept {
  return Finite(f)&&source==f.source&&epoch&&epoch==f.epoch&&history&&history==f.history&&
   params==f.params&&copy_tick&&copy_tick==f.copy_tick&&conversion_tick>=copy_tick&&
   conversion_tick<=current_tick&&scheduler_identity&&scheduler_identity==scheduler;
 }
};
// Renderer-owned requirements; callbacks publish ONLY this native timeline's completion.
// Peer completion is a separate lease bit, NEVER a cross-domain tick comparison.
struct OriginLease {
 uint64_t copy=0,capture=0,catchup=0,merge=0;
 bool peer_read=false,unknown=false;
 std::atomic<uint64_t> observed{0};
 uint64_t LastUse()const noexcept{return std::max({copy,capture,catchup,merge});}
 bool CanReuse()const noexcept{return !peer_read&&!unknown&&LastUse()!=0&&observed.load(std::memory_order_acquire)>=LastUse();}
 void CompleteNative(uint64_t tick)noexcept{auto old=observed.load(std::memory_order_relaxed);while(old<tick&&!observed.compare_exchange_weak(old,tick,std::memory_order_release,std::memory_order_relaxed)){} }
};
}
