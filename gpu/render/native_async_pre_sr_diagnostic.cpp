// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_async_pre_sr_diagnostic.h"
#include "native_dev_paths.h"
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>
namespace Vulkan {
namespace {using J=nlohmann::json;
J Jitter(const BbNativeAsync::Frame& f){return {{"viewport_jitter_x",f.viewport[0]},{"viewport_jitter_y",f.viewport[1]},
 {"previous_viewport_jitter_x",f.previous_viewport[0]},{"previous_viewport_jitter_y",f.previous_viewport[1]},
 {"sr_jitter_x",f.sr_jitter[0]},{"sr_jitter_y",f.sr_jitter[1]},
 {"previous_sr_jitter_x",f.previous_sr_jitter[0]},{"previous_sr_jitter_y",f.previous_sr_jitter[1]}};}
}
NativeAsyncPreSrDiagnostic::NativeAsyncPreSrDiagnostic(const Instance& i){
 const char* r=std::getenv("BB_NR_NATIVE_ASYNC_RUN_DIR"),*s=std::getenv("BB_NR_NATIVE_ASYNC_SESSION");
 if(!BbNativeAsyncPreSr::DiagnosticEnabled()||!r||!s)throw std::runtime_error("async preSR once resident opt-in required");
 run=r;session=s;if(!BbNativeAsyncPreSr::ValidDevRun(run,session))throw std::runtime_error("async preSR once private root mismatch");
 for(auto& b:images)b=std::make_unique<VideoCore::Buffer>(i,0,BbNativeAsyncPreSr::DiagnosticImageBytes,VideoCore::MemoryType::HostCached,"Async preSR actual current97 once");
 guard=std::make_unique<VideoCore::Buffer>(i,0,4,VideoCore::MemoryType::HostCached,"Async preSR once actual GPU guard");
 if(BbNativeAsyncPreSr::MaskDiagnosticEnabled()){
  for(auto& b:mask_staging)b=std::make_unique<VideoCore::Buffer>(i,0,4,VideoCore::MemoryType::HostCached,"masked once actual Prev/Next GPU word");
  for(unsigned n=0;n<2;++n)mask_images[n]=std::make_unique<VideoCore::Buffer>(i,0,uint64_t(1280)*720*16,VideoCore::MemoryType::HostCached,"masked once CURRENT guide pair");
  if(BbNativePreSrSkyPolicy::Current()==BbNativePreSrSkyPolicy::Mode::RegionalAntiPop)mask_images[2]=std::make_unique<VideoCore::Buffer>(i,0,uint64_t(320)*180*16,VideoCore::MemoryType::HostCached,"masked once CURRENT written context");
 }
 writer=std::jthread([this](std::stop_token token){Run(token);});
}
NativeAsyncPreSrDiagnostic::~NativeAsyncPreSrDiagnostic(){writer.request_stop();if(writer.joinable())writer.join();}
bool NativeAsyncPreSrDiagnostic::RecordOnce(Scheduler& s,vk::Image raw,vk::Image selected,
 const std::array<vk::ImageCreateInfo,2>& ci,vk::Buffer gpu_guard,const Context& c,const BbNativeAsync::Chain& chain){
 if(recorded||!raw||!selected||raw==selected||!gpu_guard||c.job.session!=session||
  c.job.resource_set.empty()||c.job.job!=c.admission.cache.job||!c.job.Matches(c.admission.origin)||
  !c.admission.Eligible(chain)||c.admission.sr_read_tick!=s.CurrentTick()||c.lineage_count!=chain.Size())return false;
 for(const auto& x:ci)if(x.format!=vk::Format::eR16G16B16A16Sfloat||x.extent.width!=1280||x.extent.height!=720||
  x.extent.depth!=1||x.samples!=vk::SampleCountFlagBits::e1||x.mipLevels!=1||x.arrayLayers!=1||!(x.usage&vk::ImageUsageFlagBits::eTransferSrc))return false;
 for(unsigned n=0;n<c.lineage_count;++n)if(c.lineage[n].source!=c.admission.origin.source+n||
  c.lineage[n].epoch!=c.admission.origin.epoch||c.lineage[n].history!=c.admission.origin.history||
  c.lineage[n].params!=c.admission.origin.params||!BbNativeAsync::Finite(c.lineage[n])||
  (n&&(c.lineage[n].previous!=c.lineage[n-1].source||c.lineage[n].sr_reset)))return false;
 if(c.mask_diagnostic!=BbNativeAsyncPreSr::MaskDiagnosticEnabled())return false;
 if(c.mask_diagnostic){
  const auto& m=c.mask_current;
  if(!m.whole||m.next_slot>=2||m.bank>=2||m.current.source!=c.admission.current.source||m.current.epoch!=c.admission.current.epoch||m.current.history!=c.admission.current.history||m.current.params!=c.admission.current.params||m.origin.source!=c.admission.origin.source||!c.masks[0]||!c.masks[1]||c.masks[0]==c.masks[1])return false;
  for(unsigned n=0;n<(m.context_written?3u:2u);++n){const auto& x=m.ci[n];if(!m.images[n]||!mask_images[n]||x.format!=vk::Format::eR32G32B32A32Sfloat||x.extent.width!=(n==2?320u:1280u)||x.extent.height!=(n==2?180u:720u)||x.extent.depth!=1||x.mipLevels!=1||x.arrayLayers!=1||x.samples!=vk::SampleCountFlagBits::e1||!(x.usage&vk::ImageUsageFlagBits::eTransferSrc))return false;}
 }
 // Publish metadata before callbacks; writer reads it only after both acquire proofs.
 context=c;actual_ci=ci;copy_tick=s.CurrentTick();scheduler_id=reinterpret_cast<uintptr_t>(&s);
 timeline=reinterpret_cast<uintptr_t>(VkSemaphore(s.GetWorkSemaphore()->Handle()));
 actual_handles={reinterpret_cast<uintptr_t>(VkImage(raw)),reinterpret_cast<uintptr_t>(VkImage(selected))};recorded=true;
 const auto cmd=s.CommandBuffer();const std::array source{raw,selected};
 for(unsigned n=0;n<2;++n){
  const vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
   .dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,
   .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=source[n],.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
  cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});
  cmd.copyImageToBuffer(source[n],vk::ImageLayout::eGeneral,images[n]->Handle(),vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={1280,720,1}});
  const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead,
   .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=images[n]->Handle(),.offset=0,.size=BbNativeAsyncPreSr::DiagnosticImageBytes};
  auto restore=b;restore.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;restore.srcAccessMask=vk::AccessFlagBits2::eTransferRead;restore.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;restore.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&host,.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&restore});
 }
 const vk::BufferMemoryBarrier2 flag{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,
  .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=gpu_guard,.offset=0,.size=4};
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&flag});cmd.copyBuffer(gpu_guard,guard->Handle(),vk::BufferCopy{0,0,4});
 const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead,
  .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=guard->Handle(),.offset=0,.size=4};
 auto restore=flag;restore.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;restore.srcAccessMask=vk::AccessFlagBits2::eTransferRead;restore.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader;restore.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite;
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=2,.pBufferMemoryBarriers=std::array{host,restore}.data()});
 if(c.mask_diagnostic){
  for(unsigned n=0;n<2;++n){
   auto before=flag;before.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands;before.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;before.buffer=c.masks[n];
   cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&before});cmd.copyBuffer(c.masks[n],mask_staging[n]->Handle(),vk::BufferCopy{0,0,4});
   auto visible=host;visible.buffer=mask_staging[n]->Handle();auto after=restore;after.buffer=c.masks[n];const std::array bb{visible,after};cmd.pipelineBarrier2({.bufferMemoryBarrierCount=2,.pBufferMemoryBarriers=bb.data()});
  }
  const auto& m=c.mask_current;
  for(unsigned n=0;n<(m.context_written?3u:2u);++n){
   const auto bytes=uint64_t(m.ci[n].extent.width)*m.ci[n].extent.height*16;
   const vk::ImageMemoryBarrier2 before{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,.oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=m.images[n],.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
   cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&before});cmd.copyImageToBuffer(m.images[n],vk::ImageLayout::eGeneral,mask_images[n]->Handle(),vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent=m.ci[n].extent});
   auto visible=host;visible.buffer=mask_images[n]->Handle();visible.size=bytes;auto after=before;after.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;after.srcAccessMask=vk::AccessFlagBits2::eTransferRead;after.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;after.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite;
   cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&visible,.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&after});
  }
  std::printf("NATIVE_PRE_SR_MASK_ONCE_RECORDED session=%s source=%llu job=%llu next_slot=%u guide_bank=%u whole=%u context_written=%u target_tick=%llu status=PENDING_GPU_SR_MERGE pixels=NOT_AUDITED performance_excluded=1\n",session.c_str(),(unsigned long long)c.admission.current.source,(unsigned long long)c.job.job,m.next_slot,m.bank,m.whole,m.context_written,(unsigned long long)copy_tick);
 }
 s.DeferPriorityOperation([hold=shared_from_this()]{hold->copy_observed.store(hold->copy_tick,std::memory_order_release);});
 std::printf("NATIVE_ASYNC_PRE_SR_ONCE_RECORDED session=%s source=%llu origin=%llu job=%llu actual_SR_read_tick=%llu copy_tick=%llu pixel_bytes=14745600 guard_bytes=4 same_current97=1 extraFlush=0 performance_excluded=1 pixels=NOT_AUDITED\n",session.c_str(),(unsigned long long)c.admission.current.source,(unsigned long long)c.admission.origin.source,(unsigned long long)c.job.job,(unsigned long long)c.admission.sr_read_tick,(unsigned long long)copy_tick);
 return true;
}
void NativeAsyncPreSrDiagnostic::MergeRecorded(Scheduler& s,uint64_t source){
 if(!recorded||merge_registered||source!=context.admission.current.source)return;
 if(reinterpret_cast<uintptr_t>(&s)!=scheduler_id||s.CurrentTick()<copy_tick)throw std::runtime_error("async preSR once merge timeline/order mismatch");
 merge_tick=s.CurrentTick();merge_registered=true;
 s.DeferPriorityOperation([hold=shared_from_this()]{hold->merge_observed.store(hold->merge_tick,std::memory_order_release);});
 std::printf("NATIVE_ASYNC_PRE_SR_ONCE_MERGE session=%s source=%llu job=%llu copy_tick=%llu merge_tick=%llu actualPresent=REQUIRES_UNIQUE_NONREUSE_LEDGER\n",session.c_str(),(unsigned long long)source,(unsigned long long)context.job.job,(unsigned long long)copy_tick,(unsigned long long)merge_tick);
}
bool NativeAsyncPreSrDiagnostic::RetireFinished(){
 if(retired)return true;
 if(!recorded||!copy_observed.load(std::memory_order_acquire)||!merge_observed.load(std::memory_order_acquire)||!writer_finished.load(std::memory_order_acquire))return false;
 if(writer.joinable())writer.join();for(auto& b:images)b.reset();guard.reset();for(auto& b:mask_staging)b.reset();for(auto& b:mask_images)b.reset();retired=true;
 std::printf("NATIVE_ASYNC_PRE_SR_ONCE_RETIRED session=%s source=%llu actual_copy_SR_merge_complete=1 writer_joined=1 staging_buffers=0\n",session.c_str(),(unsigned long long)context.admission.current.source);return true;
}
void NativeAsyncPreSrDiagnostic::Run(std::stop_token stop){
 struct End{std::atomic<bool>& f;~End(){f.store(true,std::memory_order_release);}}end{writer_finished};
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(300);
 while(!stop.stop_requested()&&std::chrono::steady_clock::now()<deadline&&(!copy_observed.load(std::memory_order_acquire)||!merge_observed.load(std::memory_order_acquire)))std::this_thread::sleep_for(std::chrono::milliseconds(10));
 if(!copy_observed.load(std::memory_order_acquire)||!merge_observed.load(std::memory_order_acquire)){
  std::fprintf(stderr,"NATIVE_ASYNC_PRE_SR_ONCE_FAILED session=%s reason=GPU_copy_SR_merge_proof_NOT_OBSERVED staging_held=1\n",session.c_str());return;
 }
 try{
  const std::string dir=run+"/native-async-pre-sr-once";
  if(!std::filesystem::create_directory(dir))throw std::runtime_error("once directory exists; no overwrite or retry");
  std::filesystem::permissions(dir,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
  const char* names[]{"original-current720.rgba16f","actual-sr-input720.rgba16f"};J assets=J::array();
  for(unsigned n=0;n<2;++n){
   images[n]->Invalidate(0,BbNativeAsyncPreSr::DiagnosticImageBytes);
   const auto bytes=std::span<const uint8_t>(images[n]->mapped_data.data(),BbNativeAsyncPreSr::DiagnosticImageBytes);
   std::ofstream out(dir+"/"+names[n],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();if(!out)throw std::runtime_error("once pixel write failed");
   assets.push_back({{"file",names[n]},{"bytes",bytes.size()},{"sha256",BbNativeAsyncPreSr::DiagnosticSha256(bytes)}});
  }
  guard->Invalidate(0,4);uint32_t flag=0;std::memcpy(&flag,guard->mapped_data.data(),4);
  const auto& c=context;const auto& a=c.admission;const auto& id=c.job;J lineage=J::array();
  for(unsigned n=0;n<c.lineage_count;++n)lineage.push_back(c.lineage[n].source);
  auto image=[&](unsigned n,const char* role){const auto& ci=actual_ci[n];return J{{"role",role},{"image_handle",actual_handles[n]},{"format",uint32_t(ci.format)},{"width",ci.extent.width},{"height",ci.extent.height},{"usage",uint32_t(ci.usage)},{"aspect","COLOR"},{"bytes",BbNativeAsyncPreSr::DiagnosticImageBytes}};};
  J j{{"schema","native-async-pre-sr-once-readback-v1"},{"session",session},
   {"job_identity",{{"session",id.session},{"resource_set",id.resource_set},{"generation",id.generation},{"job",id.job},{"origin_source",id.origin_source},{"origin_epoch",id.origin_epoch},{"origin_history",id.origin_history},{"params_version",id.params_version}}},
   {"current_source",a.current.source},{"origin_source",a.origin.source},{"current_epoch",a.current.epoch},{"current_history",a.current.history},{"params_version",a.current.params},{"age",a.current.source-a.origin.source},
   {"complete_lineage_sources",lineage},{"current_source_jitter_metadata",Jitter(a.current)},{"origin_source_jitter_metadata",Jitter(a.origin)},
   {"cache_selected_host",true},{"guard_word_actual",flag},{"guard_scope","actual_GPU_wholeframe_flag_from_same_current_private97_conversion;nonzero_entire_actual_SRinput_is_raw"},
   {"input_domain_changed",c.input_domain_changed},{"source_sr_reset_requested",a.current.sr_reset},{"applied_sr_reset",c.applied_sr_reset},
   {"original_image_role_and_extent",image(0,"immutable_unedited_current97")},{"actual_SR_input_image_role_and_extent",image(1,"actual_private97_bound_to_DLSS_SR")},
   {"copy_tick",copy_tick},{"actual_SR_read_tick",a.sr_read_tick},{"merge_tick",merge_tick},
   {"native_timeline_identity",{{"scheduler_address",scheduler_id},{"timeline_semaphore",timeline},{"scope","same_Native_renderer_Scheduler_only"}}},
   {"actual_GPU_completion_proofs",{{"copy_and_SRread_target_tick",copy_tick},{"copy_and_SRread_observed_tick",copy_observed.load()},{"merge_target_tick",merge_tick},{"merge_observed_tick",merge_observed.load()},{"proof","existing_DeferPriorityOperation_after_actual_same_Native_target_complete"}}},
   {"written_asset_sha256_bytes",assets},{"readback_pixel_bytes",2*BbNativeAsyncPreSr::DiagnosticImageBytes},{"guard_readback_bytes",4},
   {"native_input_tick",a.input_tick},{"helper_serial_source_only",a.helper_serial},{"NR_phase","UNVERIFIED"},{"pixels_and_quality","NOT_AUDITED"},
   {"excluded_from_performance",true},{"same_source_SR_only1080_AB","NOT_AVAILABLE"},
   {"present_proof","REQUIRES_INDEPENDENT_ROOT_UNIQUE_NONREUSE_LEDGER"},
   {"present_join",{{"event","NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT"},{"session",session},{"source_game_frame",a.current.source},{"job",id.job},{"merge_tick",merge_tick},{"required_cache_selected",true},{"required_swapchain_present",true},{"required_nonreuse",true}}}};
  if(c.mask_diagnostic){
   const auto& m=c.mask_current;std::array<uint32_t,2> words{};
   J reads=J::array();const char* names[]{"current-owned-shown-guide720.rgba32f","current-final-bank-guide720.rgba32f","current-context320x180.rgba32f"};
   for(unsigned n=0;n<2;++n){mask_staging[n]->Invalidate(0,4);std::memcpy(&words[n],mask_staging[n]->mapped_data.data(),4);const auto data=std::span<const uint8_t>(mask_staging[n]->mapped_data.data(),4);const char* name=n?"next-mask.u32":"previous-mask.u32";std::ofstream out(dir+"/"+name,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),4);out.close();if(!out)throw std::runtime_error("masked once word write failed");reads.push_back({{"file",name},{"bytes",4},{"sha256",BbNativeAsyncPreSr::DiagnosticSha256(data)}});}
   for(unsigned n=0;n<(m.context_written?3u:2u);++n){const auto bytes=uint64_t(m.ci[n].extent.width)*m.ci[n].extent.height*16;mask_images[n]->Invalidate(0,bytes);const auto data=std::span<const uint8_t>(mask_images[n]->mapped_data.data(),bytes);std::ofstream out(dir+"/"+names[n],std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();if(!out)throw std::runtime_error("masked once image write failed");reads.push_back({{"file",names[n]},{"bytes",bytes},{"sha256",BbNativeAsyncPreSr::DiagnosticSha256(data)},{"image_handle",reinterpret_cast<uintptr_t>(VkImage(m.images[n]))},{"format",109},{"width",m.ci[n].extent.width},{"height",m.ci[n].extent.height},{"usage",uint32_t(m.ci[n].usage)}});}
   j["mask_diagnostic"]={{"scope","eligible_selected_current_only;raw_WholeInvalid_partialrecord_NOT_COVERED"},{"current_source",m.current.source},{"origin_source",m.origin.source},{"oldest_contributing_origin",m.oldest_contributing_origin},{"previous_slot_source",m.previous.source},{"epoch",m.current.epoch},{"history",m.current.history},{"params_version",m.current.params},{"job",id.job},{"previous_slot",1-m.next_slot},{"next_slot",m.next_slot},{"final_guide_bank",m.bank},{"whole_host",m.whole},{"context_written_recorded",m.context_written},{"guard_word_actual",flag},{"previous_mask_word_actual",words[0]},{"next_mask_word_actual",words[1]},{"mask_readback_bytes",8},{"image_readback_bytes",uint64_t(29491200)+(m.context_written?921600:0)},{"actual_GPU_completion_proof","same existing copy_SRread_and_merge targets"},{"written_assets",reads},{"guide_context_pixels","NOT_AUDITED"},{"context_when_unwritten","NOT_READ_NOT_DUMPED"}};
  }
  std::ofstream meta(dir+"/readback.json.tmp");meta<<j.dump(2)<<"\n";meta.close();if(!meta)throw std::runtime_error("once metadata write failed");std::filesystem::rename(dir+"/readback.json.tmp",dir+"/readback.json");
  std::printf("NATIVE_ASYNC_PRE_SR_ONCE_COMPLETE session=%s source=%llu origin=%llu job=%llu guard_word_actual=%u pixel_bytes=14745600 actual_copy_SR_merge_GPU_complete=1 pixels=NOT_AUDITED Present=REQUIRES_ROOT_JOIN\n",session.c_str(),(unsigned long long)a.current.source,(unsigned long long)a.origin.source,(unsigned long long)id.job,flag);
 }catch(const std::exception& e){std::fprintf(stderr,"NATIVE_ASYNC_PRE_SR_ONCE_FAILED session=%s reason=%s retry=0\n",session.c_str(),e.what());}
}
}
