// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>
#include <cstdint>
namespace BbNativeAsyncPlay {
// Root921101a4 exact values; backup limits, never required completion counts.
inline bool Enabled()noexcept {
 const char*p=std::getenv("BB_NR_NATIVE_ASYNC_PLAY");
 const char*m=std::getenv("BB_NR_NATIVE_ASYNC_MODE");
 return p&&std::strcmp(p,"1")==0&&m&&
  (std::strcmp(m,"resident")==0||std::strcmp(m,"skipEvaluate")==0);
}
inline uint64_t MaxJobs()noexcept{return Enabled()?16384u:4u;}
inline uint64_t SourceLimit()noexcept{return Enabled()?36000u:120u;}
inline unsigned ServiceSeconds()noexcept{return Enabled()?600u:300u;}
inline unsigned BucketCount()noexcept{return Enabled()?256u:1u;}
}
