// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_owner.h"
#include "common/types.h"
#include "video_core/texture_cache/image.h"
#include "video_core/renderer_vulkan/vk_common.h"
namespace Vulkan {
class Instance;class Scheduler;
class NativeAsyncSnapshot {
public:
 bool Enabled()const noexcept{return BbNativeAsync::OptIn();}
 bool TryCaptureOrigin(const Instance&,Scheduler&,const std::array<vk::Image,3>&,
             const std::array<vk::ImageCreateInfo,3>&,BbNativeAsync::Frame);
 void PollReady(Scheduler&,bool supported);
 vk::ImageView RecordAdvanceAndApply(Scheduler&,uint64_t current_source);
 void RetireByTick(Scheduler&);
 std::optional<BbNativeAsync::Frame> ReadyOrigin()const noexcept;
 void Shutdown(Scheduler&);
 void RejectAssociation()noexcept{rejected=true;}
 void Quarantine()noexcept{rejected=true;stopping=true;}
 std::string Session()const;
 uint64_t Epoch()const noexcept{return last_epoch;}
private:
 struct Retention {
  std::array<VideoCore::UniqueImage,3*BbNativeAsync::NodeCount> images;
  std::array<std::shared_ptr<BbNativeAsync::Node>,BbNativeAsync::NodeCount> records{};
  // SOURCE COPY completion only: no external reader/cache/merge lease exists yet.
  // TODO(Root contract): reuse must also prove peer Capture/cache/merge last use;
  // shared_ptr lifetime alone never authorizes overwriting this origin.
  bool CopiesReady()const noexcept {for(const auto& p:records)if(p&&!p->gpu_ready.load(std::memory_order_acquire))return false;return true;}
 };
 void Create(const Instance&);
 std::shared_ptr<Retention> retained;
 std::shared_ptr<BbNativeAsync::Owner> owner;
 BbNativeAsync::Chain chain;
 bool rejected=false,stopping=false;uint64_t last_epoch=0,last_reported_ready=0;
};
}
