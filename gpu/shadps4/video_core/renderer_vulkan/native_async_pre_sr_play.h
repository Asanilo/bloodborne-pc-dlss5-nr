// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>
namespace BbNativeAsyncPreSrPlay {
// Root902026b1 private continuous backup limits. The old4 route remains exact.
inline bool Enabled()noexcept{
 const char* on=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_PLAY");
 const char* pre=std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR");
 const char* mode=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 return on&&!std::strcmp(on,"1")&&pre&&!std::strcmp(pre,"1")&&mode&&
  (!std::strcmp(mode,"resident")||!std::strcmp(mode,"skipEvaluate"));
}
inline uint64_t MaxJobs()noexcept{return Enabled()?16384u:4u;}
inline uint64_t SourceLimit()noexcept{return Enabled()?36000u:120u;}
inline unsigned ServiceSeconds()noexcept{return Enabled()?600u:300u;}
inline unsigned BucketCount()noexcept{return Enabled()?256u:1u;}
inline unsigned Bucket(uint64_t job)noexcept{return unsigned(job/64);}
}
