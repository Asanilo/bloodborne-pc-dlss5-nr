// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "nr_dimensions.h"
#include "textures/image.h"
#include <atomic>
#include <memory>
namespace Vulkan {class Instance;class Scheduler;struct NativeAsyncPreSrOriginQuiescence;
// Native-only persistent97, never an immutable origin or Helper resource. Renderer
// records same-queue read->write->read barriers; actual GPU proof gates API retirement.
class NativeAsyncPreSrFixedInput:public std::enable_shared_from_this<NativeAsyncPreSrFixedInput>{
public:
 NativeAsyncPreSrFixedInput(const Instance&,std::shared_ptr<NativeAsyncPreSrOriginQuiescence>);
 ~NativeAsyncPreSrFixedInput();
 vk::ImageView RecordCopy(Scheduler&,uint64_t source,vk::Image,const vk::ImageCreateInfo&);
 void SrReadRecorded(Scheduler&,uint64_t source,vk::Image);
 void MergeRecorded(Scheduler&,uint64_t source);
 vk::Image Image()const noexcept{return vk::Image(image);}
 const vk::ImageCreateInfo& Info()const noexcept{return image.image_ci;}
 bool Ready()const noexcept{return !unknown&&last_use&&observed.load(std::memory_order_acquire)>=last_use;}
 void HoldUnknown()noexcept{unknown=true;}
 uint64_t LastUse()const noexcept{return last_use;}
 uint64_t CopyTick()const noexcept{return copy_tick;}
 uint64_t ReadTick()const noexcept{return sr_read_tick;}
 static inline const uint64_t PackedBytes=uint64_t(BbNRDimensions::Width())*BbNRDimensions::Height()*8;
private:
 void Observe(Scheduler&);
 std::shared_ptr<NativeAsyncPreSrOriginQuiescence> quiescence;
 VideoCore::HostImage image;vk::UniqueImageView view;
 bool initialized=false,unknown=false;uint64_t current_source=0,scheduler_id=0;
 uint64_t copy_tick=0,sr_read_tick=0,last_use=0;std::atomic<uint64_t> observed{0};
};}
