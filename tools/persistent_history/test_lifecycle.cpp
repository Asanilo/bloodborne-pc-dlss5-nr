// CPU regression: request packet exhaustion/reset must not dictate visual lifetime.
#include "native_visual_timeline.h"
#include "native_origin_lease.h"
#include <cassert>
#include <cstdio>
using namespace BbNativeAsync;
int main(){
 VisualTimeline visual;Chain request;uint64_t generation=0,epoch=0;
 for(uint64_t source=1;source<=18;++source){
  Frame raw{.source=source};auto current=visual.Observe(raw);
  if(!generation)generation=current.history;
  assert(current.history==generation&&current.previous==source-1);
  auto decision=request.Classify(raw);
  if(decision==Chain::Decision::Full){request.Restart(false);}
  assert(request.Append(raw));
  assert(raw.source==source);assert(request.CompleteTo(request.Origin().source,source,raw.epoch,raw.history));
  if(source==1)epoch=raw.epoch;
  if(source>5)assert(raw.epoch>epoch);
  // Every sixth frame is beyond an older NR request's admissible age while
  // the same visual generation remains legal; GPU per-pixel TTL is separate.
  if(source==6)assert(!request.CompleteTo(1,source,epoch,1));
 }
 // Missing an actual raw source, actual reset and parameter change invalidate.
 auto gap=visual.Observe(Frame{.source=20});assert(gap.history==generation+1);
 auto reset=visual.Observe(Frame{.source=21,.sr_reset=true});assert(reset.history==gap.history+1);
 auto next=visual.Observe(Frame{.source=22});assert(next.history==reset.history);
 auto params=Frame{.source=23};params.params=2;assert(visual.Observe(params).history==reset.history+1);
 Chain model_chain;Frame a{.source=1};assert(model_chain.Append(a));Frame b{.source=2,.params=2};assert(Finite(b));assert(model_chain.Classify(b)==Chain::Decision::Discontinuity);model_chain.Restart(true);assert(model_chain.Append(b));Frame c{.source=3,.params=2};assert(model_chain.Append(c));assert(model_chain.CompleteTo(2,3,b.epoch,b.history));Frame invalid{.source=4,.params=0};assert(!Finite(invalid));
 puts("PASS request rollover independent of visual generation; gaps/resets/params invalidate");
}
