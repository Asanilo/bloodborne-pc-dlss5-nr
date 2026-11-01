// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace Vulkan {
struct PersistentHistoryAdmissionClock {
 uint64_t source;
 double ms;
 uint64_t history,params;
};
struct PersistentHistoryAdmission {
 bool admitted=false;
 // The exact observed times used by Capture and each catch-up Advance.
 std::array<double,BbNativeAsync::NodeCount> times{};
};

// CPU metadata admission only: no image, descriptor, transition, or GPU lease
// mutation. Links expose their immutable request Frame as .frame; clocks belong
// to the independent visual timeline. Their history/epoch counters must NOT be
// compared directly, since a request rollover is not a visual reset.
// The <=4-link / <=200ms request window is independent of per-pixel visual TTL.
template<class Link,class Clocks>
PersistentHistoryAdmission AdmitPersistentHistoryCandidate(std::span<const Link> links,
 const Clocks& clocks,const BbNativeAsync::Frame& current,uint64_t minimum_origin) {
 PersistentHistoryAdmission result;
 if(links.empty()||links.size()>BbNativeAsync::NodeCount||!BbNativeAsync::Finite(current))return result;
 const auto& origin=links.front().frame;
 if(links.back().frame.source!=current.source||origin.source<minimum_origin||
    current.source<origin.source||current.source-origin.source>BbNativeAsync::MaxLinks)return result;
 for(size_t k=0;k<links.size();++k){
  const auto& frame=links[k].frame;
  if(!BbNativeAsync::Finite(frame)||frame.epoch!=origin.epoch||frame.history!=origin.history||
     frame.params!=origin.params||frame.params!=current.params)return result;
  if(k&&(frame.source!=links[k-1].frame.source+1||frame.previous!=links[k-1].frame.source||frame.sr_reset))return result;
  const PersistentHistoryAdmissionClock* observed=nullptr;
  for(const auto& clock:clocks)if(clock.source==frame.source){observed=&clock;break;}
  if(!observed||observed->history!=current.history||observed->params!=frame.params||
     !std::isfinite(observed->ms)||observed->ms<0.)return result;
  result.times[k]=observed->ms;
  if(k&&result.times[k]<result.times[k-1])return result;
 }
 const double elapsed=result.times[links.size()-1]-result.times[0];
 if(!std::isfinite(elapsed)||elapsed<0.||elapsed>200.)return result;
 result.admitted=true;return result;
}
} // namespace Vulkan
