// SPDX-License-Identifier: GPL-2.0-or-later
#include "nr_runtime_capture.h"
#include "native_async_pre_sr_diagnostic_policy.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include <filesystem>
#include <mutex>
#include <map>
#include <atomic>
#include <set>
#include <cstdio>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <vk_mem_alloc.h>
namespace Vulkan {
namespace{std::mutex progress_mutex;std::map<uint64_t,RuntimeOutputProgress> progress;std::atomic<unsigned> pending_outputs{0};uint64_t pending_bytes=0;std::set<std::tuple<uint64_t,uint64_t,std::string>> capture_keys;}
RuntimeOutputProgress ReadRuntimeOutputProgress(uint64_t seq){std::scoped_lock lock(progress_mutex);auto it=progress.find(seq);return it==progress.end()?RuntimeOutputProgress{}:it->second;}
void RuntimeCapturePresented(const NativePersistentHistory::CaptureFrame& c,bool success){std::scoped_lock lock(progress_mutex);if(success)++progress[c.seq].presented;else progress[c.seq].failed=true;}
bool RuntimeOutputAllReady(){return pending_outputs.load(std::memory_order_acquire)==0;}

static void RecordRuntimeOutputImpl(const Instance& instance,Scheduler& scheduler,vk::Image image,
 vk::Format format,uint32_t width,uint32_t height,const NativePersistentHistory::CaptureFrame& capture,const char* stage){
 if(!capture.seq||!capture.source||width!=1920||height!=1080)throw std::runtime_error("runtime output identity/extent");
 const auto key=std::make_tuple(capture.seq,capture.source,std::string(stage));
 {std::scoped_lock lock(progress_mutex);if(capture_keys.contains(key))return;}
 const bool half=format==vk::Format::eR16G16B16A16Sfloat;
 if(!half&&format!=vk::Format::eB8G8R8A8Unorm&&format!=vk::Format::eB8G8R8A8Srgb&&format!=vk::Format::eR8G8B8A8Unorm&&format!=vk::Format::eR8G8B8A8Srgb)throw std::runtime_error("runtime output unsupported exact format");
 struct Staging{VmaAllocator allocator{};VkBuffer buffer{};VmaAllocation allocation{};VmaAllocationInfo info{};std::atomic<bool> recorded{false};~Staging(){if(buffer)vmaDestroyBuffer(allocator,buffer,allocation);}};
 auto readback=std::make_shared<Staging>();readback->allocator=instance.GetAllocator();
 const uint64_t bytes=uint64_t(width)*height*(half?8:4);
 const VkBufferCreateInfo ci{.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
 const VmaAllocationCreateInfo ai{.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
 if(vmaCreateBuffer(readback->allocator,&ci,&ai,&readback->buffer,&readback->allocation,&readback->info)!=VK_SUCCESS)throw std::runtime_error("runtime output staging allocation");
 const auto tick=scheduler.CurrentTick();const auto* completion=scheduler.GetWorkSemaphore();
 const auto source_dir=std::filesystem::path(capture.directory);
 const auto directory=source_dir.parent_path().parent_path()/("runtime-output-capture-"+std::to_string(capture.seq))/(source_dir.filename().string()+"-"+stage);
 const auto stage_name=std::string(stage);
 // Install retention before recording: a later exception cannot free a buffer
 // referenced by a partly recorded command buffer. Only the real tick callback owns it.
 {std::scoped_lock lock(progress_mutex);if(pending_bytes+bytes>uint64_t(1920)*1080*12*3)throw std::runtime_error("bounded cold output staging busy");pending_bytes+=bytes;capture_keys.insert(key);while(capture_keys.size()>96)capture_keys.erase(capture_keys.begin());}
 ++pending_outputs;
 try{scheduler.DeferPriorityOperation([readback,completion,tick,bytes,width,height,format,capture,directory,stage_name]() mutable {
  if(!completion->IsFree(tick)){new std::shared_ptr<Staging>(readback);std::fprintf(stderr,"NR_RUNTIME_OUTPUT_HELD unknown_GPU_completion=1\n");return;}
  try{
   if(!readback->recorded.load(std::memory_order_acquire))throw std::runtime_error("cold output recording incomplete; no pixel proof");
   vmaInvalidateAllocation(readback->allocator,readback->allocation,0,VK_WHOLE_SIZE);
   std::filesystem::create_directories(directory.parent_path());
   if(!std::filesystem::create_directory(directory))throw std::runtime_error("runtime output collision; no overwrite");
   const auto file=directory/"pixels.raw";FILE* out=std::fopen(file.c_str(),"wbx");if(!out)throw std::runtime_error("runtime output open");
   const auto written=std::fwrite(readback->info.pMappedData,1,bytes,out);const auto closed=std::fclose(out);
   if(written!=bytes||closed)throw std::runtime_error("runtime output short write");
   const auto hash=BbNativeAsyncPreSr::DiagnosticSha256({static_cast<const uint8_t*>(readback->info.pMappedData),size_t(bytes)});
   nlohmann::json j{{"schema","nr-runtime-output-v1"},{"stage",stage_name},{"capture_seq",capture.seq},{"source",capture.source},{"control_revision",capture.revision},{"width",width},{"height",height},{"vk_format",uint32_t(format)},{"bytes",bytes},{"file","pixels.raw"},{"sha256",hash},{"recorded_tick",tick},{"observed_tick",completion->KnownGpuTick()},{"actual_GPU_complete",true},{"capture_position",stage_name=="Present"?"postprocess game frame before swapchain letterbox and NR overlay":"DLSS SR linear HDR output"},{"comparison_scope","actual single SR history; no independent SR-only counterpart"},{"linear_HDR",format==vk::Format::eR16G16B16A16Sfloat},{"raw_NR_manifest",(std::filesystem::path(capture.directory)/"readback.json").string()}};
   const auto manifest=(directory/"output.json").string();FILE* mf=std::fopen(manifest.c_str(),"wx");if(!mf)throw std::runtime_error("runtime output manifest open");const auto text=j.dump(2)+"\n";const auto n=std::fwrite(text.data(),1,text.size(),mf);const auto c=std::fclose(mf);if(n!=text.size()||c)throw std::runtime_error("runtime output manifest write");
   {std::scoped_lock lock(progress_mutex);auto& p=progress[capture.seq];if(stage_name=="SR1080")++p.sr_complete;else ++p.present_complete;while(progress.size()>16)progress.erase(progress.begin());}
   std::printf("NR_RUNTIME_OUTPUT_COMPLETE stage=%s source=%llu seq=%llu revision=%llu target_tick=%llu observed_tick=%llu bytes=%llu path=%s\n",stage_name.c_str(),(unsigned long long)capture.source,(unsigned long long)capture.seq,(unsigned long long)capture.revision,(unsigned long long)tick,(unsigned long long)completion->KnownGpuTick(),(unsigned long long)bytes,directory.c_str());
  }catch(const std::exception& e){{std::scoped_lock lock(progress_mutex);progress[capture.seq].failed=true;}std::fprintf(stderr,"NR_RUNTIME_OUTPUT_CAPTURE_FAILED source=%llu seq=%llu reason=%s\n",(unsigned long long)capture.source,(unsigned long long)capture.seq,e.what());}
  readback.reset(); // actual VMA destruction precedes the exit-ready publication
  {std::scoped_lock lock(progress_mutex);pending_bytes-=bytes;}
  pending_outputs.fetch_sub(1,std::memory_order_release);
 });}catch(...){readback.reset();{std::scoped_lock lock(progress_mutex);pending_bytes-=bytes;capture_keys.erase(key);}pending_outputs.fetch_sub(1,std::memory_order_release);throw;}
 auto cmd=scheduler.CommandBuffer();
 const vk::ImageMemoryBarrier2 before{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,.oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
 cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&before});
 cmd.copyImageToBuffer(image,vk::ImageLayout::eGeneral,readback->buffer,vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={width,height,1}});
 auto after=before;after.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;after.srcAccessMask=vk::AccessFlagBits2::eTransferRead;after.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;after.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
 const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=readback->buffer,.offset=0,.size=bytes};
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&host,.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&after});
 readback->recorded.store(true,std::memory_order_release);
}
void RecordRuntimeOutput(const Instance& instance,Scheduler& scheduler,vk::Image image,vk::Format format,uint32_t width,uint32_t height,const NativePersistentHistory::CaptureFrame& capture,const char* stage){
 try{RecordRuntimeOutputImpl(instance,scheduler,image,format,width,height,capture,stage);}
 catch(const std::exception& e){{std::scoped_lock lock(progress_mutex);progress[capture.seq].failed=true;}std::fprintf(stderr,"NR_RUNTIME_OUTPUT_CAPTURE_FAILED source=%llu seq=%llu reason=%s no_success_claim=1\n",(unsigned long long)capture.source,(unsigned long long)capture.seq,e.what());}
}
}
