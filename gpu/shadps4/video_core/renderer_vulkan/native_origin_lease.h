// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
namespace BbNativeAsync {
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
