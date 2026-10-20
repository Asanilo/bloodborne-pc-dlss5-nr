// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_async_pre_sr_fixed_input.h"
#include "native_async_pre_sr_origin_gpu.h"
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include <stdexcept>
#include <cstdio>
namespace Vulkan {
NativeAsyncPreSrFixedInput::NativeAsyncPreSrFixedInput(const Instance& i,std::shared_ptr<NativeAsyncPreSrOriginQuiescence> q):quiescence(std::move(q)),image(i.GetDevice(),i.GetAllocator()){
 if(!quiescence)throw std::runtime_error("fixed SR requires managed peer lifecycle");
 image.Create({.imageType=vk::ImageType::e2D,.format=vk::Format::eR16G16B16A16Sfloat,.extent={1280,720,1},
  .mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
  .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst,
  .initialLayout=vk::ImageLayout::eUndefined});
 view=i.GetDevice().createImageViewUnique({.image=vk::Image(image),.viewType=vk::ImageViewType::e2D,
  .format=vk::Format::eR16G16B16A16Sfloat,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;
 quiescence->fixed_input_absent_or_destroyed.store(false,std::memory_order_release);
}
NativeAsyncPreSrFixedInput::~NativeAsyncPreSrFixedInput(){
 const auto handle=VkImage(vk::Image(image));const auto completed=observed.load(std::memory_order_acquire);
 view.reset();image.Destroy();quiescence->fixed_input_absent_or_destroyed.store(true,std::memory_order_release);
 std::printf("NATIVE_PRE_SR_FIXED_RETIRED source=%llu image=%p last_use_tick=%llu observed_tick=%llu actual_view_destroyed=1 actual_image_destroyed=1 same_Native_GPU_lastuse_complete=%u scope=fixed_native_only\n",(unsigned long long)current_source,(void*)handle,(unsigned long long)last_use,(unsigned long long)completed,last_use&&completed>=last_use&&!unknown);
}
void NativeAsyncPreSrFixedInput::Observe(Scheduler& s){
 const auto tick=s.CurrentTick();last_use=std::max(last_use,tick);
 s.DeferPriorityOperation([hold=shared_from_this(),tick]{auto old=hold->observed.load();while(old<tick&&!hold->observed.compare_exchange_weak(old,tick,std::memory_order_release,std::memory_order_relaxed)){};});
}
vk::ImageView NativeAsyncPreSrFixedInput::RecordCopy(Scheduler& s,uint64_t source,vk::Image input,const vk::ImageCreateInfo& ci){
 if(unknown||!input||input==vk::Image(image)||!source||source<=current_source||
  ci.format!=vk::Format::eR16G16B16A16Sfloat||ci.extent.width!=1280||ci.extent.height!=720||ci.extent.depth!=1||
  ci.mipLevels!=1||ci.arrayLayers!=1||ci.samples!=vk::SampleCountFlagBits::e1||!(ci.usage&vk::ImageUsageFlagBits::eTransferSrc))throw std::runtime_error("fixed SR input exact current97 CI/source mismatch");
 const auto identity=reinterpret_cast<uintptr_t>(&s);
 if(scheduler_id&&scheduler_id!=identity)throw std::runtime_error("fixed input cannot cross Native Scheduler domains");
 scheduler_id=identity;current_source=source;copy_tick=s.CurrentTick();last_use=std::max(last_use,copy_tick);
 const auto cmd=s.CommandBuffer();
 const vk::ImageMemoryBarrier2 src{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
  .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,
  .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,
  .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=input,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
 auto dst=src;dst.dstAccessMask=vk::AccessFlagBits2::eTransferWrite;dst.image=vk::Image(image);
 dst.oldLayout=initialized?vk::ImageLayout::eGeneral:vk::ImageLayout::eUndefined;
 const std::array before{src,dst};cmd.pipelineBarrier2({.imageMemoryBarrierCount=2,.pImageMemoryBarriers=before.data()});
 cmd.copyImage(input,vk::ImageLayout::eGeneral,vk::Image(image),vk::ImageLayout::eGeneral,
  vk::ImageCopy{.srcSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.dstSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.extent={1280,720,1}});
 auto out=dst;out.oldLayout=vk::ImageLayout::eGeneral;out.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;out.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;
 out.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;out.dstAccessMask=vk::AccessFlagBits2::eMemoryRead;
 auto restore=src;restore.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;restore.srcAccessMask=vk::AccessFlagBits2::eTransferRead;restore.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;restore.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
 const std::array after{restore,out};cmd.pipelineBarrier2({.imageMemoryBarrierCount=2,.pImageMemoryBarriers=after.data()});
 initialized=true;Observe(s);return *view;
}
void NativeAsyncPreSrFixedInput::SrReadRecorded(Scheduler& s,uint64_t source,vk::Image actual){
 if(source!=current_source||actual!=vk::Image(image)||reinterpret_cast<uintptr_t>(&s)!=scheduler_id)throw std::runtime_error("fixed actual SR read identity mismatch");
 sr_read_tick=s.CurrentTick();Observe(s); // conservative includes an unsuccessful API record attempt
}
void NativeAsyncPreSrFixedInput::MergeRecorded(Scheduler& s,uint64_t source){
 if(source!=current_source||reinterpret_cast<uintptr_t>(&s)!=scheduler_id)throw std::runtime_error("fixed actual merge identity mismatch");
 Observe(s);
}
}
