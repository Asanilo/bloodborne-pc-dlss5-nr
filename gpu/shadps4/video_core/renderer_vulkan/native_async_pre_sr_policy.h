// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include "native_origin_lease.h"
#include <cstdlib>
#include <cstring>
namespace BbNativeAsyncPreSr {
// Private Native opt-in only. No new Helper wire/profile is defined here.
inline bool Enabled()noexcept {
 const char* on=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR");
 const char* mode=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 const char* session=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
 if(!on||std::strcmp(on,"1")||!mode||(!(!std::strcmp(mode,"resident")||!std::strcmp(mode,"skipEvaluate")))||!session||!*session||std::strlen(session)>63)return false;
 for(const char* k:{"BB_NR_NATIVE_ASYNC","BB_NR_NATIVE_ASYNC_PLAY","BB_NR_NATIVE_ASYNC_ROLLING","BB_NR_NATIVE_PRE_SR","BB_NR_P0_MODE","BB_NR_Q1_MODE","BB_NR_C4_MODE","BB_NR_C3_MODE","BB_NR_C2_MODE","BB_NR_C1_MODE"}) {
  const char* v=std::getenv(k);if(v&&*v&&std::strcmp(v,"0"))return false;
 }
 for(const char* q=session;*q;++q)if(!((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')||(*q>='0'&&*q<='9')||*q=='-'))return false;
 return true;
}
inline constexpr unsigned Width=1280,Height=720,MaxJobs=4,SourceLimit=120;
static_assert(BbNativeAsync::NodeCount==5&&BbNativeAsync::MaxLinks==4);
// Pixel texels only. VMA allocation padding and model footprint are not inferred.
inline constexpr uint64_t RawPacketBytes=uint64_t(Width)*Height*(8+8+4);
inline constexpr uint64_t RawPacketsBytes=RawPacketBytes*BbNativeAsync::NodeCount;
struct CacheIdentity {uint64_t job=0,origin=0,epoch=0,history=0,params=1,current=0;};
inline bool CacheLineageMatches(const BbNativeAsync::Chain& c,const CacheIdentity& x)noexcept {
 return x.job<MaxJobs&&x.params==1&&c.Size()&&c.Origin().params==x.params&&c.CompleteTo(x.origin,x.current,x.epoch,x.history);
}
// SR read is a distinct real-use field, never a renamed copy/catchup completion.
struct SourceLease {
 BbNativeAsync::OriginLease native;uint64_t sr_read=0;
 uint64_t LastUse()const noexcept{return std::max(native.LastUse(),sr_read);}
 bool CanReuse()const noexcept{return !native.peer_read&&!native.unknown&&LastUse()!=0&&native.observed.load(std::memory_order_acquire)>=LastUse();}
 void CompleteNative(uint64_t tick)noexcept{native.CompleteNative(tick);}
};
class InputDomain {
 bool edited=false;
public:
 bool Change(bool next)noexcept{const bool changed=next!=edited;edited=next;return changed;}
 static bool AppliedReset(bool original,bool changed)noexcept{return original||changed;}
};
} // namespace BbNativeAsyncPreSr
