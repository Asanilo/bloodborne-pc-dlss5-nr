// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/native_async_pre_sr_sr_policy.h"
#include <stdexcept>
#include <iostream>
namespace P=BbNativeAsyncPreSrSrPolicy;
int main(){unsigned checks=0;auto need=[&](bool p){if(!p)throw std::runtime_error("SR policy failclosed CPU failure");++checks;};
 need(P::ParseHistory(nullptr)==P::History::DomainReset&&P::ParseInput(nullptr)==P::Input::Switching);
 need(P::ParseHistory("game_only")==P::History::GameOnly&&P::ParseInput("fixed")==P::Input::Fixed);
 need(P::ParseHistory("")==P::History::Invalid&&P::ParseInput("Fixed")==P::Input::Invalid);
 for(const char* h:{"domain_reset","game_only"})for(const char* i:{"switching","fixed"}){
  setenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY",h,1);setenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT",i,1);need(P::ExplicitModes());
  need(P::AppliedReset(true,false,false));need(P::AppliedReset(false,false,true));need(!P::AppliedReset(false,false,false));
  need(P::AppliedReset(false,true,false)==(P::HistoryMode()==P::History::DomainReset));
 }
 unsetenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY");unsetenv("BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT");need(!P::ExplicitModes());need(P::AppliedReset(false,true,false));
 std::cout<<"{\"status\":\"PASS_CPU_SR_HISTORY_INPUT_FOUR_COMBINATIONS_ONLY\",\"checks\":"<<checks<<",\"GPU_fixed_image_lastuse\":\"NOT_RUN\"}\n";
}
