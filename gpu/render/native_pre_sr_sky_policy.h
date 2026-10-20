// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "native_async_state.h"
namespace BbNativePreSrSkyPolicy {
enum class Mode {Off,LowTemporal,RegionalAntiPop,Invalid};
inline Mode Parse(const char*v)noexcept{
 if(!v||!std::strcmp(v,"off"))return Mode::Off;
 if(!std::strcmp(v,"low_temporal"))return Mode::LowTemporal;
 if(!std::strcmp(v,"regional_antipop"))return Mode::RegionalAntiPop;
 return Mode::Invalid;
}
inline Mode Current()noexcept{return Parse(std::getenv("BB_NR_NATIVE_ASYNC_PRE_SR_SKY"));}
inline bool Enabled()noexcept{return Current()==Mode::LowTemporal||Current()==Mode::RegionalAntiPop;}
inline const char* Name()noexcept{return Current()==Mode::Off?"off":Current()==Mode::LowTemporal?"low_temporal":Current()==Mode::RegionalAntiPop?"regional_antipop":"INVALID";}
// Exact local Root control 4667d5f8. No Helper parameter or protocol key.
inline bool PreviousLegal(bool eligible,bool SR_success,const BbNativeAsync::Frame& previous,const BbNativeAsync::Frame& current,uint64_t oldest_origin)noexcept{
 return eligible&&SR_success&&BbNativeAsync::Finite(previous)&&BbNativeAsync::Finite(current)&&previous.source<UINT64_MAX&&previous.source+1==current.source&&previous.epoch==current.epoch&&previous.history==current.history&&previous.params==current.params&&oldest_origin&&current.source>=oldest_origin&&current.source-oldest_origin<=BbNativeAsync::MaxLinks;
}
struct FrameClock {double smoothed=1.0/60.0;
 float Update(double actual_seconds)noexcept{
  if(!std::isfinite(actual_seconds)||actual_seconds<=0)return 0;
  smoothed=.9*smoothed+.1*std::clamp(actual_seconds,1.0/500.0,1.0/10.0);return float(.75*smoothed);
 }
};
inline constexpr unsigned MaskCount=2,MaskBytes=4,NumericBit=1,ContextBit=2;
inline unsigned MaskValue(bool finite,bool wholeEligible,bool contextWritten)noexcept{return finite&&wholeEligible?(NumericBit|(contextWritten?ContextBit:0)):0;}
}
