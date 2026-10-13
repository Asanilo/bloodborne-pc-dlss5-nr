// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
namespace BbNativeAsync {
inline constexpr unsigned MaxLinks=4, NodeCount=MaxLinks+1;
struct Frame {
 uint64_t source=0, previous=0, epoch=0, history=1, copy_tick=0;
 bool sr_reset=false, source_edited=false;
 uint64_t params=1;
 std::array<float,2> viewport{},previous_viewport{},sr_jitter{},previous_sr_jitter{};
};
inline bool Finite(const Frame& f) noexcept {
 for(const auto& a:{f.viewport,f.previous_viewport,f.sr_jitter,f.previous_sr_jitter})
  for(float v:a) if(!std::isfinite(v))return false;
 return f.source!=0&&!f.source_edited&&f.params==1;
}
class Chain {
public:
 enum class Decision {Reject,Origin,Link,Full,Discontinuity};
 Decision Classify(const Frame& f)const noexcept {
  if(!Finite(f)||f.source<=last)return Decision::Reject;
  if(!used)return Decision::Origin;
  if(f.sr_reset||f.source!=last+1)return Decision::Discontinuity;
  return used==NodeCount?Decision::Full:Decision::Link;
 }
 bool Append(Frame& f) noexcept {
  const auto d=Classify(f);if(d!=Decision::Origin&&d!=Decision::Link)return false;
  f.previous=last;f.epoch=epoch;f.history=history;nodes[used++]=f;last=f.source;return true;
 }
 bool CompleteTo(uint64_t origin,uint64_t current,uint64_t epoch_id,uint64_t history_id)const noexcept {
  if(!used||nodes[0].source!=origin||last!=current||nodes[0].epoch!=epoch_id||nodes[0].history!=history_id)return false;
  for(unsigned i=1;i<used;++i)if(nodes[i].source!=nodes[i-1].source+1||nodes[i].previous!=nodes[i-1].source||nodes[i].sr_reset||nodes[i].epoch!=epoch_id||nodes[i].history!=history_id)return false;
  return current>=origin&&current-origin<=MaxLinks;
 }
 void Restart(bool history_changed) noexcept {
  used=0;++epoch;if(history_changed)++history;
 }
 unsigned Size()const noexcept{return used;}
 uint64_t LastSource()const noexcept{return last;}
 const Frame& Origin()const noexcept{return nodes[0];}
private:
 std::array<Frame,NodeCount> nodes{};unsigned used=0;uint64_t last=0,epoch=1,history=1;
};
inline bool NeedsHold(bool supported,bool snapshots,bool exit,bool rejected) noexcept {
 return exit||rejected||(!supported&&snapshots);
}
struct Node {
 Frame frame;std::atomic<bool> gpu_ready{false};
 explicit Node(Frame f):frame(f){}
};
// Numeric observation of this Native copy timeline only. Never compare a Helper
// fence serial or another timeline against it. Not an edit grant/cache/FD/wire contract.
struct CompletionNotice {
 uint64_t origin=0,epoch=0,history=0,native_copy_observed_tick=0;bool real_gpu_completed=false;
};
inline bool NoticeMatches(const Frame& origin,const CompletionNotice& n) noexcept {
 return n.real_gpu_completed&&n.origin==origin.source&&n.epoch==origin.epoch&&
        n.history==origin.history&&n.native_copy_observed_tick>=origin.copy_tick&&origin.copy_tick!=0;
}
bool OptIn() noexcept;
void RequestExit() noexcept;
bool ExitRequested() noexcept;
void PublishExitComplete() noexcept;
bool ExitComplete() noexcept;
void PublishAbnormalEmptyExitComplete() noexcept;
bool ExitAbnormalEmpty() noexcept;
} // namespace BbNativeAsync
