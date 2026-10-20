// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_persistent_history.h"
#include "native_persistent_history_diagnostic.h"
#include "native_async_pre_sr_return.h"
#include "native_persistent_history_shader.h"
#include "native_visual_transition.h"
#include "native_persistent_history_spv.h"
#include "bbgpu/shaders/native_sky_convert_comp.h"
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include "vulkan/shader_module.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>
namespace Vulkan {
namespace {
constexpr unsigned Width=1280,Height=720,Passes=10;
static_assert(RequiredTransitionPasses(true,BbNativeAsync::NodeCount,true)<=Passes);
constexpr uint64_t GroupCount=(Width/8)*(Height/8),GroupBytes=GroupCount*6*4;
constexpr unsigned QueriesPerSlot=Passes*2;
struct HistoryImage {
 VideoCore::HostImage image; vk::UniqueImageView view;
 vk::ImageView View() const noexcept{return view?*view:vk::ImageView{};}
};
void Barrier(vk::CommandBuffer cmd){
 const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,
 .srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,
 .dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,
 .dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite};
 cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b});
}
bool Same(const BbNativeAsync::Frame& a,const BbNativeAsync::Frame& b){
 return a.epoch==b.epoch&&a.history==b.history&&a.params==b.params;
}
}
struct NativePersistentHistory::Impl {
 struct Proof {std::atomic<uint64_t> observed{0}; uint64_t last=0; bool unknown=false;};
 struct Bank {HistoryImage edit,meta;};
 struct Slot {
  std::array<HistoryImage,3> converted;
  std::array<vk::UniqueImageView,3> source;
  std::vector<vk::UniqueImageView> borrowed;
  std::array<std::unique_ptr<VideoCore::Buffer>,Passes> cb;
  std::array<vk::DescriptorSet,Passes> sets{};
  vk::DescriptorSet conversion{};
  std::shared_ptr<Proof> proof=std::make_shared<Proof>();
  unsigned used=0; uint64_t source_id=0;
  std::unique_ptr<VideoCore::Buffer> stats;bool sampled=false,stats_pending=false;
  std::array<unsigned,Passes> modes{};uint64_t sample_revision=0,sample_generation=0;
 };
 struct Clock {uint64_t source;double ms;uint64_t history,params;};
 const Instance* instance=nullptr;
 vk::Device device; VmaAllocator allocator;
 std::array<Bank,4> banks;
 std::array<Slot,2> slots;
 HistoryImage output;
 std::unique_ptr<VideoCore::Buffer> state;
 NativeAsyncPreSrReturn returned;
 std::unique_ptr<NativePersistentHistoryDiagnostic> diagnostic;
 std::deque<NativePersistentHistory::CaptureFrame> capture_frames;
 NativePersistentHistory::CaptureStatus capture_status;uint64_t handled_capture=0;bool env_capture_pending=false;
 vk::Image current_raw{};
 uint64_t candidate_origin=0,candidate_current=0,candidate_count=0;
 PersistentHistoryConstants last_capture;
 vk::UniqueSampler nearest;
 vk::UniqueDescriptorSetLayout layout,conversion_layout;
 vk::UniquePipelineLayout pipeline_layout,conversion_pipeline_layout;
 vk::UniquePipeline pipeline,cubic_pipeline,conversion_pipeline;
 vk::UniqueDescriptorPool pool;
 std::deque<Clock> clocks;
 BbNativeAsync::Frame current,last;
 double clock_ms=0,current_dt_ms=0;
 unsigned slot=0,active=0,candidate=2;
 PersistentHistoryResetWindow reset_window;
 VisualTransition transition;
 BbNR::Request pending_controls{},applied_controls{};
 NativePersistentHistory::RuntimeSample latest_sample;
 unsigned runtime_samples_logged=0;
 vk::UniqueQueryPool queries;float timestamp_period=0;unsigned timestamp_bits=0;
 bool initialized=false,prepared=false,applied=false,history=false,unknown=false,candidate_recorded=false;
 vk::Image return_image{};
 const Scheduler* timeline=nullptr;
 explicit Impl(const Instance&);
 HistoryImage Make(vk::Format);
 void Init(vk::CommandBuffer);
 void Mark(Scheduler&);
 void ReadSample(Slot&);
 void FinishTransition(Scheduler&);
 bool Free(const Slot& s)const noexcept{return !s.proof->unknown&&(!s.proof->last||s.proof->observed.load(std::memory_order_acquire)>=s.proof->last);}
 bool Time(uint64_t,double&)const;
 std::array<vk::ImageView,3> Converted()const;
 void Convert(Scheduler&,const std::array<vk::Image,3>&);
 void Pass(Scheduler&,unsigned mode,unsigned from,unsigned to,const std::array<vk::ImageView,3>&,
           vk::ImageView extra0,vk::ImageView extra1,const BbNativeAsync::Frame&,bool valid,
           float delta_frames,float delta_ms,float origin_frames=0,float origin_ms=0);
};
HistoryImage NativePersistentHistory::Impl::Make(vk::Format format){
 HistoryImage x;x.image=VideoCore::HostImage(device,allocator);
 x.image.Create({.imageType=vk::ImageType::e2D,.format=format,.extent={Width,Height,1},.mipLevels=1,.arrayLayers=1,
 .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
 .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc,
 .initialLayout=vk::ImageLayout::eUndefined});
 x.view=device.createImageViewUnique({.image=vk::Image(x.image),.viewType=vk::ImageViewType::e2D,.format=format,
 .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;return x;
}
NativePersistentHistory::Impl::Impl(const Instance& instance):device(instance.GetDevice()),allocator(instance.GetAllocator()),returned(instance){
 this->instance=&instance;
 if(!instance.GetPhysicalDevice().getFeatures().shaderStorageImageExtendedFormats)throw std::runtime_error("persistent history extended image formats unavailable");
 for(const auto f:{vk::Format::eR16G16B16A16Sfloat,vk::Format::eR32G32B32A32Sfloat,vk::Format::eR32Sfloat,vk::Format::eR32G32Sfloat}){
  const auto need=vk::FormatFeatureFlagBits::eStorageImage|vk::FormatFeatureFlagBits::eSampledImage|vk::FormatFeatureFlagBits::eTransferDst|vk::FormatFeatureFlagBits::eTransferSrc;
  if((instance.GetPhysicalDevice().getFormatProperties(f).optimalTilingFeatures&need)!=need)throw std::runtime_error("persistent history image format unavailable");
 }
 const auto limits=instance.GetPhysicalDevice().getProperties().limits;
 if(limits.maxPerStageDescriptorStorageImages<5||limits.maxPerStageDescriptorSampledImages<5||limits.maxUniformBufferRange<sizeof(PersistentHistoryConstants))throw std::runtime_error("persistent history descriptor limits");
 for(auto& b:banks){b.edit=Make(vk::Format::eR16G16B16A16Sfloat);b.meta=Make(vk::Format::eR32G32B32A32Sfloat);}
 for(auto& s:slots){s.converted[0]=Make(vk::Format::eR32G32B32A32Sfloat);s.converted[1]=Make(vk::Format::eR32Sfloat);s.converted[2]=Make(vk::Format::eR32G32Sfloat);}
 output=Make(vk::Format::eR32G32B32A32Sfloat);
 state=std::make_unique<VideoCore::Buffer>(instance,0,32,VideoCore::MemoryType::DeviceLocal,"Persistent visual bank validity");
 nearest=device.createSamplerUnique({.magFilter=vk::Filter::eNearest,.minFilter=vk::Filter::eNearest,
 .mipmapMode=vk::SamplerMipmapMode::eNearest,.addressModeU=vk::SamplerAddressMode::eClampToEdge,
 .addressModeV=vk::SamplerAddressMode::eClampToEdge,.addressModeW=vk::SamplerAddressMode::eClampToEdge,.maxLod=0}).value;
 std::array<vk::DescriptorSetLayoutBinding,14> bindings{};
 for(unsigned k=0;k<14;k++)bindings[k]={.binding=k,.descriptorType=((k==10)||(k>=12))?vk::DescriptorType::eStorageBuffer:k==11?vk::DescriptorType::eUniformBuffer:(k<3||k==5||k==6)?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 layout=device.createDescriptorSetLayoutUnique({.bindingCount=14,.pBindings=bindings.data()}).value;
 std::array<vk::DescriptorSetLayoutBinding,6> conv{};
 for(unsigned k=0;k<6;k++)conv[k]={.binding=k,.descriptorType=k<3?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 conversion_layout=device.createDescriptorSetLayoutUnique({.bindingCount=6,.pBindings=conv.data()}).value;
 pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*layout}).value;
 conversion_pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*conversion_layout}).value;
 const auto create=[&](std::span<const u32> code,vk::PipelineLayout pl,const vk::SpecializationInfo* specialization=nullptr){auto module=CompileSPV(code,device);auto p=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main",.pSpecializationInfo=specialization},.layout=pl});device.destroyShaderModule(module);return std::move(p.value);};
 // Compile both immutable variants during initialization. Nearest specialization
 // removes cubic sampling; a disabled option must not keep its register cost.
 const uint32_t enable_cubic=1;const vk::SpecializationMapEntry entry{0,0,sizeof(enable_cubic)};
 const vk::SpecializationInfo cubic_spec{1,&entry,sizeof(enable_cubic),&enable_cubic};
 pipeline=create(NativePersistentHistorySpv,*pipeline_layout);
 cubic_pipeline=create(NativePersistentHistorySpv,*pipeline_layout,&cubic_spec);
 conversion_pipeline=create(NATIVE_SKY_CONVERT_COMP,*conversion_pipeline_layout);
 constexpr unsigned count=Passes*2;
 const std::array<vk::DescriptorPoolSize,4> sizes{{{vk::DescriptorType::eCombinedImageSampler,count*5+6},{vk::DescriptorType::eStorageImage,count*5+6},{vk::DescriptorType::eStorageBuffer,count*3},{vk::DescriptorType::eUniformBuffer,count}}};
 pool=device.createDescriptorPoolUnique({.maxSets=count+2,.poolSizeCount=4,.pPoolSizes=sizes.data()}).value;
 const auto family=instance.GetPhysicalDevice().getQueueFamilyProperties()[instance.GetGraphicsQueueFamilyIndex()];
 timestamp_bits=family.timestampValidBits;timestamp_period=instance.GetPhysicalDevice().getProperties().limits.timestampPeriod;
 if(timestamp_bits&&timestamp_period>0)queries=device.createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,.queryCount=QueriesPerSlot*2}).value;
 for(auto& s:slots){s.stats=std::make_unique<VideoCore::Buffer>(instance,0,GroupBytes+QueriesPerSlot*16,VideoCore::MemoryType::HostCached,"Bounded persistent HUD GPU sample");s.conversion=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*conversion_layout}).value.front();
  for(unsigned k=0;k<Passes;k++){s.cb[k]=std::make_unique<VideoCore::Buffer>(instance,0,sizeof(PersistentHistoryConstants),VideoCore::MemoryType::HostCached,"Persistent history tick owned constants");s.sets[k]=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*layout}).value.front();}
 }
 env_capture_pending=NativePersistentHistoryDiagnostic::Enabled();
}
void NativePersistentHistory::Impl::Init(vk::CommandBuffer cmd){
 const auto init=[&](HistoryImage& x){const vk::ImageMemoryBarrier2 b{.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=vk::Image(x.image),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
 cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});cmd.clearColorImage(vk::Image(x.image),vk::ImageLayout::eGeneral,vk::ClearColorValue{},vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor,0,1,0,1});};
 for(auto& b:banks){init(b.edit);init(b.meta);}for(auto& s:slots)for(auto& x:s.converted)init(x);init(output);
 const std::array<uint32_t,8> initial{1,1,1,1,0,0,0,0};cmd.updateBuffer<uint32_t>(state->Handle(),0,initial);Barrier(cmd);initialized=true;
}
void NativePersistentHistory::Impl::Mark(Scheduler& scheduler){
 auto proof=slots[slot].proof;const auto tick=scheduler.CurrentTick();proof->last=std::max(proof->last,tick);
 scheduler.DeferPriorityOperation([proof,tick]{auto old=proof->observed.load(std::memory_order_relaxed);while(old<tick&&!proof->observed.compare_exchange_weak(old,tick,std::memory_order_release,std::memory_order_relaxed)){};});
}
bool NativePersistentHistory::Impl::Time(uint64_t source,double& ms)const {for(const auto& c:clocks)if(c.source==source){ms=c.ms;return true;}return false;}
std::array<vk::ImageView,3> NativePersistentHistory::Impl::Converted()const{return {slots[slot].converted[0].View(),slots[slot].converted[1].View(),slots[slot].converted[2].View()};}
void NativePersistentHistory::Impl::Convert(Scheduler& scheduler,const std::array<vk::Image,3>& raw){
 auto& s=slots[slot];auto cmd=scheduler.CommandBuffer();
 for(unsigned k=0;k<3;k++)s.source[k]=device.createImageViewUnique({.image=raw[k],.viewType=vk::ImageViewType::e2D,.format=k==0?vk::Format::eR16G16B16A16Sfloat:k==1?vk::Format::eD32SfloatS8Uint:vk::Format::eR16G16Sfloat,.subresourceRange={k==1?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;
 std::array<vk::DescriptorImageInfo,6> ii{};std::array<vk::WriteDescriptorSet,6> writes{};
 for(unsigned k=0;k<6;k++){ii[k]={.sampler=k<3?*nearest:vk::Sampler{},.imageView=k<3?*s.source[k]:s.converted[k-3].View(),.imageLayout=vk::ImageLayout::eGeneral};writes[k]={.dstSet=s.conversion,.dstBinding=k,.descriptorCount=1,.descriptorType=k<3?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.pImageInfo=&ii[k]};}
 Barrier(cmd);device.updateDescriptorSets(writes,{});cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*conversion_pipeline);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*conversion_pipeline_layout,0,s.conversion,{});cmd.dispatch(160,90,1);Barrier(cmd);Mark(scheduler);
}
void NativePersistentHistory::Impl::Pass(Scheduler& scheduler,unsigned mode,unsigned from,unsigned to,
 const std::array<vk::ImageView,3>& current_views,vk::ImageView extra0,vk::ImageView extra1,
 const BbNativeAsync::Frame& frame,bool valid,float delta_frames,float delta_ms,float origin_frames,float origin_ms){
 auto& s=slots[slot];if(s.used>=Passes)throw std::runtime_error("persistent history immutable dispatch slot exhausted");
 const auto n=s.used++;auto& buffer=*s.cb[n];auto cmd=scheduler.CommandBuffer();
 PersistentHistoryConstants c{};c.Mode=mode;c.Width=Width;c.Height=Height;c.SourceState=from;c.DestState=to;c.FallbackState=active;
 const auto& control=applied_controls.config;
 c.ReprojectionMode=float(static_cast<uint32_t>(control.reprojection));
 if(transition.active&&(mode==PHCompose||mode==PHStats)){
  c.TransitionEnabled=1.f;c.TransitionWeight=transition.Weight(current.source,clock_ms);c.FallbackState=candidate;
 }
 c.DisplayStrength=control.display_strength;c.MaxFrames=float(control.ttl_frames);c.MaxMs=control.ttl_ms;
 c.DepthTolerance=control.depth_tolerance;c.ColourTolerance=control.colour_tolerance;c.MinConfidence=control.min_confidence;
 c.HistoryValid=valid?1:0;c.MvScaleX=c.MvScaleY=1;
 c.JitterDeltaX=(frame.previous_viewport[0]-frame.viewport[0])/float(Width);
 c.JitterDeltaY=(frame.previous_viewport[1]-frame.viewport[1])/float(Height);
 c.CurrentJitterX=frame.viewport[0];c.CurrentJitterY=frame.viewport[1];
 std::copy(frame.current_projection.begin(),frame.current_projection.end(),c.CurrentProjection);
 std::copy(frame.previous_projection.begin(),frame.previous_projection.end(),c.PreviousProjection);
 std::copy(frame.previous_view_z.begin(),frame.previous_view_z.end(),c.PreviousViewZ);
 c.DeltaFrames=delta_frames;c.DeltaMs=delta_ms;c.OriginAgeFrames=origin_frames;c.OriginAgeMs=origin_ms;
 if(diagnostic&&mode==PHCapture)last_capture=c;
 std::memcpy(buffer.mapped_data.data(),&c,sizeof c);buffer.Flush(0,sizeof c);
 const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eHost,.srcAccessMask=vk::AccessFlagBits2::eHostWrite,
 .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eUniformRead,
 .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer.Handle(),.offset=0,.size=sizeof c};
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&host});
 // No descriptor in this slot is rewritten twice between actual completions.
 std::array<vk::DescriptorImageInfo,10> ii{};
 for(unsigned k=0;k<3;k++)ii[k]={.sampler=*nearest,.imageView=current_views[k],.imageLayout=vk::ImageLayout::eGeneral};
 // Stats reuses guide slots for target edit/meta; binding5 remains guarded output.
 if(mode==PHStats&&transition.active){ii[1].imageView=banks[candidate].edit.View();ii[2].imageView=banks[candidate].meta.View();}
 ii[3]={.imageView=banks[from].edit.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[4]={.imageView=banks[from].meta.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[5]={.sampler=*nearest,.imageView=extra0,.imageLayout=vk::ImageLayout::eGeneral};
 ii[6]={.sampler=*nearest,.imageView=extra1,.imageLayout=vk::ImageLayout::eGeneral};
 ii[7]={.imageView=banks[to].edit.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[8]={.imageView=banks[to].meta.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[9]={.imageView=output.View(),.imageLayout=vk::ImageLayout::eGeneral};
 const vk::DescriptorBufferInfo si{state->Handle(),0,32},bi{buffer.Handle(),0,sizeof c};
 std::array<vk::WriteDescriptorSet,14> writes{};
 const vk::DescriptorBufferInfo guard{mode==PHStats?returned.Guard(slot):state->Handle(),0,4},stats{s.stats->Handle(),0,GroupBytes};
 for(unsigned k=0;k<14;k++)writes[k]={.dstSet=s.sets[n],.dstBinding=k,.descriptorCount=1,
 .descriptorType=((k==10)||(k>=12))?vk::DescriptorType::eStorageBuffer:k==11?vk::DescriptorType::eUniformBuffer:(k<3||k==5||k==6)?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,
 .pImageInfo=k<10?&ii[k]:nullptr,.pBufferInfo=k==10?&si:k==11?&bi:k==12?&guard:k==13?&stats:nullptr};
 Barrier(cmd);
 if(mode!=PHCompose&&mode!=PHStats){cmd.fillBuffer(state->Handle(),to*4,4,0);Barrier(cmd);}
 device.updateDescriptorSets(writes,{});
 cmd.bindPipeline(vk::PipelineBindPoint::eCompute,mode==PHAdvance&&control.reprojection==BbNR::Reprojection::ClampedCubic?*cubic_pipeline:*pipeline);
 cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*pipeline_layout,0,s.sets[n],{});
 s.modes[n]=mode;if(s.sampled&&queries)cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,*queries,slot*QueriesPerSlot+n*2);
 cmd.dispatch(160,90,1);
 if(s.sampled&&queries)cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe,*queries,slot*QueriesPerSlot+n*2+1);
 Barrier(cmd);Mark(scheduler);
}
void NativePersistentHistory::Impl::FinishTransition(Scheduler& scheduler){
 if(!transition.active)return;
 const unsigned to=1-active;
 Pass(scheduler,PHAdopt,candidate,to,Converted(),banks[active].edit.View(),banks[active].meta.View(),current,true,0,0);
 active=to;history=true;
 std::printf("PERSISTENT_HISTORY_TRANSITION_FINISH source=%llu origin=%llu target_bank=%u active_bank=%u start_source=%llu scope=GPU_conditional_target_or_old_fallback\n",
  (unsigned long long)current.source,(unsigned long long)candidate_origin,candidate,active,(unsigned long long)transition.source);
 transition.Clear();
}
void NativePersistentHistory::Impl::ReadSample(Slot& s){
 if(!s.stats_pending||!Free(s)||s.source_id<=latest_sample.source)return;
 if(s.sample_revision!=applied_controls.revision||s.sample_generation!=reset_window.generation){s.stats_pending=false;return;}
 s.stats->Invalidate(0,GroupBytes+QueriesPerSlot*16);std::array<uint64_t,4> sums{};uint32_t max_frames=0,max_ms=0;
 const auto* data=reinterpret_cast<const uint32_t*>(s.stats->mapped_data.data());
 for(uint64_t group=0;group<GroupCount;group++){for(unsigned k=0;k<4;k++)sums[k]+=data[group*6+k];max_frames=std::max(max_frames,data[group*6+4]);max_ms=std::max(max_ms,data[group*6+5]);}
 NativePersistentHistory::RuntimeSample result;result.available=true;result.source=s.source_id;result.revision=s.sample_revision;result.target_tick=s.proof->last;result.observed_tick=s.proof->observed.load(std::memory_order_acquire);result.monotonic_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
 result.valid_fraction=float(double(sums[0])/(Width*Height));result.changed_fraction=float(double(sums[1])/(Width*Height));
 if(sums[0]){result.mean_age_frames=float(double(sums[2])/65536./double(sums[0]));result.mean_age_ms=float(double(sums[3])/256./double(sums[0]));}
 result.max_age_frames=float(max_frames)/65536.f;result.max_age_ms=float(max_ms)/256.f;
 if(queries){const auto* times=reinterpret_cast<const uint64_t*>(s.stats->mapped_data.data()+GroupBytes);bool all=true;const uint64_t mask=timestamp_bits>=64?UINT64_MAX:((uint64_t(1)<<timestamp_bits)-1);
  for(unsigned n=0;n<s.used;n++){if(!times[n*4+1]||!times[n*4+3]){all=false;continue;}const auto delta=(times[n*4+2]-times[n*4])&mask;result.pass_ms[s.modes[n]]+=double(delta)*timestamp_period/1e6;}result.timing_available=all;
 }
 // Already completed, sparse readback; no extra GPU query, wait or pixel copy.
 // Bound session output independently of gameplay duration. Zero pass time can
 // mean that pass was absent from this source, not that it is globally free.
 if(runtime_samples_logged<2048){
  std::printf("NR_HISTORY_GPU_SAMPLE source=%llu revision=%llu target_tick=%llu observed_tick=%llu monotonic_ns=%llu timing_available=%u clear_ms=%.6f capture_ms=%.6f advance_ms=%.6f adopt_ms=%.6f compose_ms=%.6f stats_ms=%.6f valid=%.6f edited_buffer_changed=%.6f mean_age_frames=%.4f mean_age_ms=%.4f scope=history_dispatch_only\n",
   (unsigned long long)result.source,(unsigned long long)result.revision,(unsigned long long)result.target_tick,(unsigned long long)result.observed_tick,(unsigned long long)result.monotonic_ns,unsigned(result.timing_available),
   result.pass_ms[0],result.pass_ms[1],result.pass_ms[2],result.pass_ms[3],result.pass_ms[4],result.pass_ms[5],double(result.valid_fraction),double(result.changed_fraction),double(result.mean_age_frames),double(result.mean_age_ms));
  if(++runtime_samples_logged==2048)std::printf("NR_HISTORY_GPU_SAMPLE_LIMIT samples=2048 subsequent_samples=HUD_ONLY\n");
 }
 latest_sample=result;s.stats_pending=false;
}
NativePersistentHistory::NativePersistentHistory(const Instance& i):impl(std::make_unique<Impl>(i)){}
NativePersistentHistory::~NativePersistentHistory(){
 if(!impl)return;
 ReportRetirement("destructor");
 // A failed shutdown proof deliberately retains resources. Destruction must not
 // convert an unobserved or unknown GPU upper bound into a successful release.
 if(!Ready()){
  std::fprintf(stderr,"PERSISTENT_HISTORY_HOLD unknown_or_pending=1 resources_retained=1 destroyed=0\n");
  (void)impl.release();return;
 }
 const auto bytes=ImageBytes();
 // Print success only AFTER image views, descriptors, buffers, images, pipelines
 // and Return are actually destroyed. Ready alone is a permission, not a release.
 impl.reset();
 std::printf("PERSISTENT_HISTORY_DESTROYED all_slots_complete=1 unknown=0 image_bytes=%llu destroyed=1\n",(unsigned long long)bytes);
}
bool NativePersistentHistory::Enabled()noexcept{const char* value=std::getenv("BB_NATIVE_PERSISTENT_HISTORY");return value&&std::strcmp(value,"1")==0;}
bool NativePersistentHistory::SetControls(const BbNR::Request& request){
 auto& p=*impl;if(request.version!=BbNR::ApiVersion||request.revision<p.pending_controls.revision||request.revision<p.applied_controls.revision||!BbNR::Validate(request.config))return false;
 p.pending_controls=request;return true;
}
BbNR::Request NativePersistentHistory::AppliedControls()const{return impl->applied_controls;}
NativePersistentHistory::RuntimeSample NativePersistentHistory::LatestRuntimeSample()const{return impl->latest_sample;}
NativePersistentHistory::CaptureStatus NativePersistentHistory::Capture()const{return impl->capture_status;}
bool NativePersistentHistory::CaptureFor(uint64_t source)const noexcept{for(const auto& frame:impl->capture_frames)if(frame.source==source)return true;return false;}
std::optional<NativePersistentHistory::CaptureFrame> NativePersistentHistory::CaptureFrameForSource(uint64_t source)const{for(auto i=impl->capture_frames.rbegin();i!=impl->capture_frames.rend();++i)if(i->source==source)return *i;return {};}
bool NativePersistentHistory::BeginCurrent(Scheduler& scheduler,const std::array<vk::Image,3>& raw,const BbNativeAsync::Frame& frame,double dt){
 auto& p=*impl;p.prepared=false;p.applied=false;p.candidate_recorded=false;
 if(p.unknown||!Enabled())return false;
 if(p.timeline&&p.timeline!=&scheduler){HoldUnknown();return false;}p.timeline=&scheduler;
 if(!BbNativeAsync::Finite(frame)||!std::isfinite(dt)||dt<=0||!raw[0]||!raw[1]||!raw[2]){p.history=false;p.transition.Clear();return false;}
 if(!p.clocks.empty()&&frame.source<=p.clocks.back().source){p.history=false;p.transition.Clear();return false;}
 p.current_dt_ms=dt*1000.;p.clock_ms+=p.current_dt_ms;p.clocks.push_back({frame.source,p.clock_ms,frame.history,frame.params});while(p.clocks.size()>16)p.clocks.pop_front();
 const unsigned next=1-p.slot;
 // IsFree refreshes this exact Native timeline; it never compares Helper serials.
 auto& s=p.slots[next];if(s.proof->last&&scheduler.IsFree(s.proof->last))s.proof->observed.store(s.proof->last,std::memory_order_release);
 if(!p.Free(s)){p.history=false;p.transition.Clear();return false;}
 p.ReadSample(s);
 if(p.pending_controls.revision!=p.applied_controls.revision||p.pending_controls.reset_seq!=p.applied_controls.reset_seq||p.pending_controls.config!=p.applied_controls.config){
  const bool log_commit=p.pending_controls.revision!=p.applied_controls.revision||p.pending_controls.reset_seq!=p.applied_controls.reset_seq;
  const auto& a=p.applied_controls.config;const auto& b=p.pending_controls.config;
  if(BbNR::RequiresVisualReset(a,b)||p.pending_controls.reset_seq!=p.applied_controls.reset_seq){
   p.history=false;p.transition.Clear();p.reset_window.ResetAt(frame.source);
   p.clocks.clear();p.clocks.push_back({frame.source,p.clock_ms,frame.history,frame.params});
  }
  p.applied_controls=p.pending_controls;p.latest_sample.available=false;p.latest_sample.timing_available=false;
  if(log_commit)std::printf("PERSISTENT_HISTORY_CONTROLS_COMMITTED source=%llu revision=%llu reset_seq=%llu minimum_candidate_origin=%llu reset_generation=%llu params=%llu reprojection=%u\n",
   (unsigned long long)frame.source,(unsigned long long)p.applied_controls.revision,(unsigned long long)p.applied_controls.reset_seq,
   (unsigned long long)p.reset_window.minimum_origin,(unsigned long long)p.reset_window.generation,(unsigned long long)frame.params,static_cast<unsigned>(p.applied_controls.config.reprojection));
 }
 if(p.diagnostic&&p.diagnostic->Finished()&&p.diagnostic->Ready()){
  p.capture_status.busy=false;p.capture_status.complete=true;p.capture_status.failed=!p.diagnostic->Succeeded();p.diagnostic.reset();
 }
 if(!p.diagnostic&&(p.pending_controls.capture_seq>p.handled_capture||p.env_capture_pending)){
  const auto sequence=p.pending_controls.capture_seq>p.handled_capture?p.pending_controls.capture_seq:0;
  p.diagnostic=std::make_unique<NativePersistentHistoryDiagnostic>(*p.instance,sequence,sequence?3:0);
  p.handled_capture=sequence;p.env_capture_pending=false;p.capture_status={};p.capture_status.seq=sequence;p.capture_status.busy=true;p.capture_status.revision=p.applied_controls.revision;p.capture_status.directory=p.diagnostic->Directory();
 }
 // Job epoch changes are not a visual reset: history/params/reset and real
 // consecutive source lineage govern the independent active lifetime.
 const bool continuous=p.history&&p.last.source&&frame.source==p.last.source+1&&frame.previous==p.last.source&&
 frame.history==p.last.history&&frame.params==p.last.params&&!frame.sr_reset;
 if(!continuous){p.history=false;p.transition.Clear();}
 p.slot=next;s.used=0;s.source_id=frame.source;s.borrowed.clear();p.current=frame;p.current_raw=raw[0];
 s.sampled=p.applied_controls.config.hud&&frame.source%30==0;s.stats_pending=false;s.sample_revision=p.applied_controls.revision;s.sample_generation=p.reset_window.generation;
 auto cmd=scheduler.CommandBuffer();if(s.sampled&&p.queries)cmd.resetQueryPool(*p.queries,p.slot*QueriesPerSlot,QueriesPerSlot);if(!p.initialized)p.Init(cmd);p.Convert(scheduler,raw);
 const auto views=p.Converted();const unsigned next_bank=1-p.active;
 p.Pass(scheduler,PHAdvance,p.active,next_bank,views,p.banks[p.active].edit.View(),p.banks[p.active].meta.View(),frame,p.history,1,float(p.current_dt_ms));
 p.active=next_bank;p.last=frame;p.prepared=true;
 if(p.transition.active){
  const unsigned next_target=5-p.candidate;
  p.Pass(scheduler,PHAdvance,p.candidate,next_target,views,p.banks[p.candidate].edit.View(),p.banks[p.candidate].meta.View(),frame,true,1,float(p.current_dt_ms));
  p.candidate=next_target;p.candidate_current=frame.source;
  if(p.transition.Due(frame.source,p.clock_ms))p.FinishTransition(scheduler);
 }
 return true;
}
bool NativePersistentHistory::RecordCandidate(Scheduler& scheduler,std::span<const Link> links,vk::ImageView nr,vk::Image nr_image,const CandidateEvidence* evidence){
 auto& p=*impl;
 if(!p.prepared||p.applied||p.candidate_recorded||p.unknown||p.timeline!=&scheduler||!nr||links.empty()||links.size()>BbNativeAsync::NodeCount)return false;
 if(links.back().frame.source!=p.current.source)return false;
 if(!p.reset_window.Admits(links.front().frame.source))return false;
 if(p.current.source<links.front().frame.source||p.current.source-links.front().frame.source>BbNativeAsync::MaxLinks)return false;
 double origin_time=0,latest_time=0;if(!p.Time(links.front().frame.source,origin_time)||!p.Time(p.current.source,latest_time))return false;
 if(latest_time-origin_time>200.)return false;
 for(size_t k=0;k<links.size();++k){const auto& f=links[k].frame;double ms;
  if(!BbNativeAsync::Finite(f)||!Same(f,links.front().frame)||!p.Time(f.source,ms)||!links[k].converted[0]||!links[k].converted[1]||!links[k].converted[2])return false;
  bool visual=false;for(const auto& clock:p.clocks)if(clock.source==f.source)visual=clock.history==p.current.history&&clock.params==p.current.params;
  if(!visual)return false;
  if(k&&(f.source!=links[k-1].frame.source+1||f.previous!=links[k-1].frame.source||f.sr_reset))return false;
 }
 auto& s=p.slots[p.slot];
 if(!std::isfinite(p.clock_ms)||!p.current.source)return false;
 // Validate the whole incoming candidate before touching an in-flight transition.
 // Reserve Compose and sampled Stats too; early arrival and expiry share ONE finalize.
 const unsigned required=unsigned(links.size())+unsigned(p.transition.active)+1u+unsigned(s.sampled);
 if(s.used+required>Passes)return false;
 if(p.transition.active)p.FinishTransition(scheduler);
 // Preserve all imported image views until this slot's real last SR/merge use.
 // The caller separately owns the NR view and converted origin packet leases.
 double previous_time=origin_time;unsigned current_bank=2;
 for(size_t k=0;k<links.size();++k){const auto& link=links[k];std::array<vk::ImageView,3> views{};
  for(unsigned j=0;j<3;j++){auto v=p.device.createImageViewUnique({.image=link.converted[j],.viewType=vk::ImageViewType::e2D,.format=j==0?vk::Format::eR32G32B32A32Sfloat:j==1?vk::Format::eR32Sfloat:vk::Format::eR32G32Sfloat,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;views[j]=*v;s.borrowed.push_back(std::move(v));}
  double ms=0;p.Time(link.frame.source,ms);const auto to=k?5-current_bank:2;
  p.Pass(scheduler,k?PHAdvance:PHCapture,current_bank,to,views,views[0],nr,link.frame,k!=0,k?1.f:0.f,k?float(ms-previous_time):0.f);
  current_bank=to;previous_time=ms;
 }
 // The origin was admitted at <=age4 above. Keep its visual target independent
 // of old active until the short display transition ends; GPU guards decide per pixel.
 p.candidate=current_bank;p.history=true;p.candidate_recorded=true;p.candidate_origin=links.front().frame.source;p.candidate_current=p.current.source;++p.candidate_count;
 if(!p.transition.Begin(p.current.source,p.clock_ms))throw std::runtime_error("invalid visual transition clock after candidate recording");
 std::printf("PERSISTENT_HISTORY_TRANSITION_BEGIN source=%llu origin=%llu target_bank=%u active_bank=%u weight=%.6f scope=GPU_guard_pending_no_age_refresh\n",
  (unsigned long long)p.current.source,(unsigned long long)p.candidate_origin,p.candidate,p.active,p.transition.Weight(p.current.source,p.clock_ms));
 if(p.diagnostic){
  if(!nr_image||!evidence)throw std::runtime_error("cold candidate actual image and exact identity required");
  NativePersistentHistoryDiagnostic::CandidateSnapshot snapshot;
  snapshot.identity=*evidence;snapshot.origin=links.front().frame;snapshot.current=p.current;
  snapshot.images={links.front().converted[0],nr_image,links.front().converted[1]};
  snapshot.capture_parameters=p.last_capture;snapshot.bank=current_bank;snapshot.count=p.candidate_count;
  if(p.diagnostic->RecordCandidate(scheduler,snapshot))p.Mark(scheduler);
 }
 std::printf("PERSISTENT_HISTORY_CANDIDATE_RECORDED source=%llu origin=%llu links=%zu candidate_bank=%u active_bank=%u status=PENDING_GPU_GUARD\n",(unsigned long long)p.current.source,(unsigned long long)links.front().frame.source,links.size(),p.candidate,p.active);
 return true;
}
vk::ImageView NativePersistentHistory::Apply(Scheduler& scheduler){
 auto& p=*impl;if(!p.prepared||p.applied||p.unknown||p.timeline!=&scheduler)return {};
 const auto views=p.Converted();const unsigned display_target=p.transition.active?p.candidate:p.active;
 p.Pass(scheduler,PHCompose,p.active,1-p.active,views,p.banks[display_target].edit.View(),p.banks[display_target].meta.View(),p.current,p.history,0,0);
 p.return_image=p.returned.Record(scheduler,p.slot,p.output.View(),*p.slots[p.slot].source[0],p.history,false);p.Mark(scheduler);p.applied=true;
 if(p.diagnostic){
  NativePersistentHistoryDiagnostic::Snapshot snapshot;
  snapshot.current=p.current;snapshot.images={p.current_raw,p.return_image,vk::Image(p.banks[p.active].edit.image),vk::Image(p.banks[p.active].meta.image),vk::Image(p.slots[p.slot].converted[1].image),vk::Image(p.slots[p.slot].converted[2].image),vk::Image(p.banks[p.candidate].edit.image),vk::Image(p.banks[p.candidate].meta.image)};
  snapshot.transition_active=p.transition.active;snapshot.transition_weight=p.transition.Weight(p.current.source,p.clock_ms);
  snapshot.transition_start_source=p.transition.source;snapshot.transition_start_ms=p.transition.ms;
  snapshot.minimum_candidate_origin=p.reset_window.minimum_origin;snapshot.reset_generation=p.reset_window.generation;snapshot.reset_seq=p.applied_controls.reset_seq;
  snapshot.control_revision=p.applied_controls.revision;snapshot.controls=p.applied_controls.config;snapshot.capture_seq=p.capture_status.seq;
  snapshot.state=p.state->Handle();snapshot.guard=p.returned.Guard(p.slot);snapshot.active_bank=p.active;snapshot.candidate_bank=p.candidate;
  snapshot.candidate_origin=p.candidate_origin;snapshot.candidate_current=p.candidate_current;snapshot.candidate_count=p.candidate_count;
  snapshot.host_history=p.history;snapshot.candidate_recorded=p.candidate_recorded;
  if(p.diagnostic->Record(scheduler,snapshot)){p.Mark(scheduler);if(!p.capture_status.first_source)p.capture_status.first_source=p.current.source;p.capture_status.last_source=p.current.source;p.capture_status.last_index=p.capture_status.recorded_frames++;
   p.capture_frames.push_back({p.capture_status.seq,p.current.source,p.applied_controls.revision,p.capture_status.last_index,p.capture_status.directory+"/frame-"+std::to_string(p.capture_status.last_index)+"-source-"+std::to_string(p.current.source)});while(p.capture_frames.size()>16)p.capture_frames.pop_front();}
 }
 auto& sample=p.slots[p.slot];
 if(sample.sampled){
  p.Pass(scheduler,PHStats,p.active,1-p.active,views,p.returned.View(p.slot),p.banks[p.active].meta.View(),p.current,p.history,0,0);
  auto cmd=scheduler.CommandBuffer();if(p.queries)cmd.copyQueryPoolResults(*p.queries,p.slot*QueriesPerSlot,sample.used*2,sample.stats->Handle(),GroupBytes,16,vk::QueryResultFlagBits::e64|vk::QueryResultFlagBits::eWithAvailability);
  const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eHost,.dstAccessMask=vk::AccessFlagBits2::eHostRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=sample.stats->Handle(),.offset=0,.size=GroupBytes+QueriesPerSlot*16};
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&host});sample.stats_pending=true;p.Mark(scheduler);
 }
 return p.returned.View(p.slot);
}
vk::Image NativePersistentHistory::Image()const noexcept{return impl->applied?impl->return_image:vk::Image{};}
const vk::ImageCreateInfo& NativePersistentHistory::Info()const noexcept{return impl->returned.Info(impl->slot);}
bool NativePersistentHistory::PreparedFor(uint64_t source)const noexcept{return impl->prepared&&!impl->unknown&&source&&impl->current.source==source;}
void NativePersistentHistory::MarkUse(Scheduler& scheduler,uint64_t source){
 auto& p=*impl;if(!source)return;
 if(p.timeline!=&scheduler){HoldUnknown();return;}
 for(auto& s:p.slots)if(s.source_id==source){
  const auto tick=scheduler.CurrentTick();auto proof=s.proof;proof->last=std::max(proof->last,tick);
  scheduler.DeferPriorityOperation([proof,tick]{auto old=proof->observed.load(std::memory_order_relaxed);while(old<tick&&!proof->observed.compare_exchange_weak(old,tick,std::memory_order_release,std::memory_order_relaxed)){};});return;
 }
 // An actual use with no retained source identity cannot be proved retired.
 HoldUnknown();
}
bool NativePersistentHistory::Ready()const noexcept {const auto& p=*impl;if(p.unknown||(p.diagnostic&&!p.diagnostic->Ready()))return false;for(const auto& s:p.slots)if(!p.Free(s))return false;return true;}
void NativePersistentHistory::ObserveCompletion(Scheduler& scheduler){
 auto& p=*impl;if(p.unknown)return;
 // An instance with no recorded use has no timeline to observe. A mismatch
 // after recording is unknown, never permission to compare unrelated counters.
 if(!p.timeline)return;
 if(p.timeline!=&scheduler){HoldUnknown();return;}
 if(p.diagnostic){p.diagnostic->ObserveCompletion(scheduler);if(p.diagnostic->Finished()&&p.diagnostic->Ready()){p.capture_status.busy=false;p.capture_status.complete=true;p.capture_status.failed=!p.diagnostic->Succeeded();p.diagnostic.reset();}}
 for(auto& slot:p.slots){auto proof=slot.proof;
  const auto target=proof->last;
  // CurrentTick is still recording/future; never label it completed. IsFree
  // refreshes the actual SAME Native timeline, not any Helper serial.
  if(proof->unknown||!target||target>=scheduler.CurrentTick()||
     proof->observed.load(std::memory_order_acquire)>=target)continue;
  if(scheduler.IsFree(target)){
   auto observed=proof->observed.load(std::memory_order_relaxed);
   while(observed<target&&!proof->observed.compare_exchange_weak(observed,target,std::memory_order_release,std::memory_order_relaxed)){}
  }
 }
 for(auto& sample:p.slots)p.ReadSample(sample);
}
void NativePersistentHistory::ReportRetirement(const char* stage)const noexcept{
 if(!impl)return;const auto& p=*impl;const char* label=stage?stage:"unspecified";
 if(p.diagnostic)p.diagnostic->ReportRetirement(label);
 for(unsigned i=0;i<p.slots.size();++i){const auto& s=p.slots[i];const auto observed=s.proof->observed.load(std::memory_order_acquire);
  std::printf("PERSISTENT_HISTORY_RETIRE stage=%s slot=%u source=%llu target_tick=%llu observed_tick=%llu unknown=%u slot_ready=%u all_ready=%u resources_destroyed=0\n",
   label,i,(unsigned long long)s.source_id,(unsigned long long)s.proof->last,(unsigned long long)observed,
   p.unknown||s.proof->unknown,p.Free(s),Ready());
 }
}
void NativePersistentHistory::HoldUnknown()noexcept{impl->unknown=true;impl->latest_sample.available=false;impl->latest_sample.timing_available=false;if(impl->diagnostic)impl->diagnostic->HoldUnknown();for(auto& s:impl->slots)s.proof->unknown=true;}
bool NativePersistentHistory::HasHistory()const noexcept{return impl->history&&!impl->unknown;}
void NativePersistentHistory::Reset()noexcept{
 auto& p=*impl;p.history=false;p.prepared=false;p.applied=false;p.transition.Clear();
 p.reset_window.ResetAt(p.last.source==UINT64_MAX?UINT64_MAX:p.last.source+1);
 p.clocks.clear();p.latest_sample.available=false;p.latest_sample.timing_available=false;
 // Slots, descriptors, CBs, last-use proofs and unknown state remain untouched.
}
uint64_t NativePersistentHistory::ImageBytes()const noexcept{
 // 4*(RGBA16 edit + RGBA32 metadata), 2 independent current conversion sets,
 // one queue-ordered RGBA32 compose, and Return's five original-alpha half slots.
 return uint64_t(Width)*Height*(4*(8+16)+2*(16+4+8)+16)+NativeAsyncPreSrReturn::PackedBytes;
}
}
