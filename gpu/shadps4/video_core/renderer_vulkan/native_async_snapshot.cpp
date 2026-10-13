// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_async_snapshot.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace Vulkan {
std::string NativeAsyncSnapshot::Session()const {const char* p=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");return p?p:"";}
void NativeAsyncSnapshot::Create(const Instance& instance) {
 auto next=std::make_shared<Retention>();
 for(unsigned i=0;i<next->images.size();++i){
  const bool color=i%3==0,depth=i%3==1;
  auto& image=next->images[i];image=VideoCore::UniqueImage(instance.GetDevice(),instance.GetAllocator());
  image.Create(vk::ImageCreateInfo{.imageType=vk::ImageType::e2D,
   .format=color?vk::Format::eR16G16B16A16Sfloat:depth?vk::Format::eD32SfloatS8Uint:vk::Format::eR16G16Sfloat,
   .extent={color?1920u:1280u,color?1080u:720u,1},.mipLevels=1,.arrayLayers=1,
   .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
   .usage=vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eSampled,
   .initialLayout=vk::ImageLayout::eUndefined});
 }
 next->sky=std::make_unique<NativeSkyGpu>(instance);
 std::printf("NATIVE_SKY_RESOURCES rgba32f_history_banks=2 packets=5 packed_image_texel_bytes=%llu CPU_readback=0 cache_scale=1 jitter_delta=0 phase=UNVERIFIED model_calls=0\n",(unsigned long long)next->sky->ImageBytes());
 retained=std::move(next);owner=std::make_shared<BbNativeAsync::Owner>();
 std::printf("NATIVE_ASYNC_INIT session=%s origin1_links4=1 native_snapshots=15 CPU_readback=0 Helper=NOT_CONNECTED cache_transport=LOCAL_NOOP_ONLY\n",Session().c_str());
}
void NativeAsyncSnapshot::PollReady(Scheduler&,bool supported) {
 if(!Enabled())return;
 if(owner&&owner->ReadyCount()!=last_reported_ready){
  last_reported_ready=owner->ReadyCount();
  std::printf("NATIVE_ASYNC_READY_OBSERVED session=%s copies_complete=%llu latest_source=%llu proof=existing_actual_GPU_tick_callback Helper=NOT_CONNECTED\n",Session().c_str(),
   (unsigned long long)last_reported_ready,(unsigned long long)owner->LatestReadySource());
 }
 const bool permanent=rejected||BbNativeAsync::ExitRequested();
 if(BbNativeAsync::NeedsHold(supported,bool(retained),BbNativeAsync::ExitRequested(),rejected))stopping=true;
 if(stopping&&(!retained||retained->AllUsesReady())) {
  if(owner){owner->Stop();}
  owner.reset();retained.reset();
  if(BbNativeAsync::ExitRequested())BbNativeAsync::PublishExitComplete();
  if(!permanent){chain.Restart(true);stopping=false;}
 }
}
std::optional<BbNativeAsync::Frame> NativeAsyncSnapshot::ReadyOrigin()const noexcept {
 if(stopping||rejected||!retained||!retained->records[0]||!retained->records[0]->gpu_ready.load(std::memory_order_acquire))return {};
 return retained->records[0]->frame;
}
vk::ImageView NativeAsyncSnapshot::RecordAdvanceAndApply(Scheduler& scheduler,uint64_t current_source) {
 if(stopping||rejected||!retained||!chain.Size())return {};
 const auto& origin=chain.Origin();
 if(!chain.CompleteTo(origin.source,current_source,origin.epoch,origin.history))return {};
 const unsigned index=chain.Size()-1;
 const auto record=retained->records[index];const auto lease=retained->leases[index];
 if(!record||!lease||record->frame.source!=current_source)return {};
 // Native producer and consumer commands are in the SAME actual timeline/queue.
 // Copy->sample barriers, not CPU copyReady, establish this recorded dependency.
 // No Helper ownership/read lease exists in this explicitly LOCAL_NOOP mode.
 lease->capture=index==0?scheduler.CurrentTick():0;lease->catchup=scheduler.CurrentTick();
 const std::array<vk::Image,3> images{vk::Image(retained->images[index*3]),vk::Image(retained->images[index*3+1]),vk::Image(retained->images[index*3+2])};
 auto view=retained->sky->Record(scheduler,index,images,index==0);
 std::printf("NATIVE_SKY_RECORD origin=%llu current_source=%llu epoch=%llu history=%llu params=%llu age=%u Capture=%u Reproject=%u Pyramid=2 Apply=1 tick=%llu source_format=97 cache_format=109 model_calls=0 mode=local-noop phase=UNVERIFIED\n",
  (unsigned long long)origin.source,(unsigned long long)current_source,(unsigned long long)origin.epoch,(unsigned long long)origin.history,(unsigned long long)origin.params,index,index==0,index!=0,(unsigned long long)scheduler.CurrentTick());
 return view;
}
void NativeAsyncSnapshot::MergeRecorded(Scheduler& scheduler,uint64_t source) {
 if(!retained||chain.LastSource()!=source)return;
 const auto tick=scheduler.CurrentTick();auto hold=retained;
 // Every retained origin/link and both history banks have participated in this
 // queue-ordered cache chain. Keep them through the REAL merge final-use tick.
 for(const auto& lease:hold->leases)if(lease)lease->merge=tick;
 const auto leases=hold->leases; // immutable ownership snapshot, not mutable slot array
 scheduler.DeferPriorityOperation([hold,leases,tick]{for(const auto& lease:leases)if(lease)lease->CompleteNative(tick);});
}
void NativeAsyncSnapshot::RetireByTick(Scheduler& scheduler) {Shutdown(scheduler);}
void NativeAsyncSnapshot::Shutdown(Scheduler& scheduler) {
 stopping=true;
 if(owner){owner->Stop();}
 owner.reset();
 if(retained) {
  auto hold=std::move(retained);
  // Existing deferred target, no extra Flush/wait. Captured resource owner survives
  // destructor until the scheduler proves its current tick's actual GPU completion.
  scheduler.DeferOperation([hold=std::move(hold)]{});
 }
}
bool NativeAsyncSnapshot::TryCaptureOrigin(const Instance& instance,Scheduler& scheduler,
 const std::array<vk::Image,3>& source,const std::array<vk::ImageCreateInfo,3>& ci,BbNativeAsync::Frame meta) {
 if(!Enabled()||stopping||rejected||!BbNativeAsync::Finite(meta))return false;
 // DEPTH-only GENERAL barriers on format130 require the actually enabled feature.
 if(!instance.IsSeparateDepthStencilLayoutsEnabled())return false;
 for(unsigned i=0;i<3;++i){const auto& c=ci[i];
  const auto format=i==0?vk::Format::eR16G16B16A16Sfloat:i==1?vk::Format::eD32SfloatS8Uint:vk::Format::eR16G16Sfloat;
  if(!source[i]||c.format!=format||c.extent.width!=(i?1280u:1920u)||c.extent.height!=(i?720u:1080u)||c.extent.depth!=1||
     c.samples!=vk::SampleCountFlagBits::e1||c.mipLevels!=1||c.arrayLayers!=1||!(c.usage&vk::ImageUsageFlagBits::eTransferSrc))return false;
 }
 if(!retained)Create(instance);
 auto decision=chain.Classify(meta);
 if(decision==BbNativeAsync::Chain::Decision::Reject)return false;
 if(decision==BbNativeAsync::Chain::Decision::Full||decision==BbNativeAsync::Chain::Decision::Discontinuity){
  if(!retained->AllUsesReady())return false; // GPU incomplete: no overwrite, current SR only
  chain.Restart(decision==BbNativeAsync::Chain::Decision::Discontinuity);
  for(auto& record:retained->records)record.reset();
  for(auto& lease:retained->leases)lease.reset();
  retained->sky->Restart();
 }
 const unsigned index=chain.Size();meta.copy_tick=scheduler.CurrentTick();
 BbNativeAsync::Frame next=meta; // commit chain only if bounded owner reservation succeeds
 auto candidate=chain; if(!candidate.Append(next))return false;
 auto record=std::make_shared<BbNativeAsync::Node>(next);
 if(!owner->TryPost(record))return false;
 retained->records[index]=record; // unknown partial recording stays held
 auto lease=std::make_shared<BbNativeAsync::OriginLease>();lease->copy=next.copy_tick;retained->leases[index]=lease;
 auto cmd=scheduler.CommandBuffer();
 for(unsigned role=0;role<3;++role){
  const unsigned target=3*index+role;
  const auto aspect=role==1?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor;
  const vk::ImageMemoryBarrier2 src{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,
   .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,
   .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
   .image=source[role],.subresourceRange={aspect,0,1,0,1}};
  const auto dest=vk::Image(retained->images[target]);
  const vk::ImageMemoryBarrier2 dst{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
   .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
   .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,
   .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
   .image=dest,.subresourceRange={aspect,0,1,0,1}};
  const std::array barriers{src,dst};cmd.pipelineBarrier2({.imageMemoryBarrierCount=2,.pImageMemoryBarriers=barriers.data()});
  const vk::ImageCopy copy{.srcSubresource={aspect,0,0,1},.dstSubresource={aspect,0,0,1},.extent={role?1280u:1920u,role?720u:1080u,1}};
  cmd.copyImage(source[role],vk::ImageLayout::eGeneral,dest,vk::ImageLayout::eGeneral,copy);
  const vk::ImageMemoryBarrier2 ready{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,
   .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader|vk::PipelineStageFlagBits2::eTransfer,
   .dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eTransferRead,
   .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
   .image=dest,.subresourceRange={aspect,0,1,0,1}};
  cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&ready});
  auto restore=src;restore.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;restore.srcAccessMask=vk::AccessFlagBits2::eTransferRead;
  restore.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;restore.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
  cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&restore});
 }
 chain=candidate;last_epoch=next.epoch;
 scheduler.DeferPriorityOperation([hold=retained,record,lease,notify=owner]{
  lease->CompleteNative(record->frame.copy_tick);
  record->gpu_ready.store(true,std::memory_order_release);notify->Signal();
 });
 std::printf("NATIVE_ASYNC_SOURCE session=%s origin=%llu source=%llu previous=%llu epoch=%llu history=%llu link=%u sr_reset=%u copy_tick=%llu viewport=%.9g,%.9g previous_viewport=%.9g,%.9g sr_jitter=%.9g,%.9g previous_sr_jitter=%.9g,%.9g source_edited=0 GPU_ready=NOT_YET_OBSERVED current_SR=1\n",
  Session().c_str(),(unsigned long long)chain.Origin().source,(unsigned long long)next.source,(unsigned long long)next.previous,
  (unsigned long long)next.epoch,(unsigned long long)next.history,index,next.sr_reset,(unsigned long long)next.copy_tick,
  next.viewport[0],next.viewport[1],next.previous_viewport[0],next.previous_viewport[1],next.sr_jitter[0],next.sr_jitter[1],next.previous_sr_jitter[0],next.previous_sr_jitter[1]);
 return true;
}
}
