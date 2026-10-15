// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
namespace BbNativeAsync {
// Independent actual-source visual identity. Request packet rollover never calls
// this a reset, and never rewrites the immutable request Frame or tuple.
class VisualTimeline {
 uint64_t last=0,generation=1,params=0;
public:
 Frame Observe(Frame raw) noexcept {
  if(last&&(raw.source!=last+1||raw.sr_reset||raw.params!=params))++generation;
  raw.previous=last;raw.history=generation;
  last=raw.source;params=raw.params;return raw;
 }
};
}
