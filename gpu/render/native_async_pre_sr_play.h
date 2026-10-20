// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "native_runtime_model.h"
namespace BbNativeAsyncPreSrPlay {
// Current development uses continuous mode. Job/bucket/service limits match
// the unchanged Helper transport capacity, not a historical launch allowance.
inline bool Enabled()noexcept{
 const char* on=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY");
 const char* pre=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR");
 const char* mode=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 return on&&!std::strcmp(on,"1")&&pre&&!std::strcmp(pre,"1")&&mode&&
  (!std::strcmp(mode,"resident")||!std::strcmp(mode,"skipEvaluate"));
}
inline uint64_t MaxJobs()noexcept{return Enabled()?NativeRuntimeModel::JobLimit():4u;}
inline uint64_t SourceLimit()noexcept{
 if(!Enabled())return 120u;
 if(NativeRuntimeModel::Daily())return NativeRuntimeModel::CounterLimit-2;
 static const uint64_t limit=[]()->uint64_t{
  const char* value=std::getenv("BB_NR_NATIVE_ASYNC_SOURCE_LIMIT");if(!value)return 36000u;
  char* end=nullptr;const auto parsed=std::strtoull(value,&end,10);
  return *value&&*value!='-'&&end&&!*end&&parsed>=1&&parsed<=216000?parsed:0;
 }();return limit;
}
inline unsigned ServiceSeconds()noexcept{return NativeRuntimeModel::Daily()?0u:(Enabled()?600u:300u);}
inline unsigned BucketCount()noexcept{return NativeRuntimeModel::Daily()?1u:(Enabled()?256u:1u);}
inline unsigned Bucket(uint64_t job)noexcept{return unsigned(NativeRuntimeModel::FileOrdinal(job)/64);}
}
