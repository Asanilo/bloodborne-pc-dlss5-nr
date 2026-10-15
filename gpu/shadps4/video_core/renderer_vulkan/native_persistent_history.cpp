// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_persistent_history.h"
#include "native_persistent_history_diagnostic.h"
#include "native_async_pre_sr_return.h"
#include "native_persistent_history_shader.h"
#include "native_persistent_history_spv.h"
#include "video_core/host_shaders/native_sky_convert_comp.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include "vk_shader_util.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <vector>
namespace Vulkan {
namespace {
constexpr unsigned Width=1280,Height=720,Passes=9;
struct HistoryImage {
 VideoCore::UniqueImage image; vk::UniqueImageView view;
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
 };
 struct Clock {uint64_t source;double ms;uint64_t history,params;};
 vk::Device device; VmaAllocator allocator;
 std::array<Bank,4> banks;
 std::array<Slot,2> slots;
 HistoryImage output;
 std::unique_ptr<VideoCore::Buffer> state;
 NativeAsyncPreSrReturn returned;
 std::unique_ptr<NativePersistentHistoryDiagnostic> diagnostic;
 vk::Image current_raw{};
 uint64_t candidate_origin=0,candidate_current=0,candidate_count=0;
 vk::UniqueSampler nearest;
 vk::UniqueDescriptorSetLayout layout,conversion_layout;
 vk::UniquePipelineLayout pipeline_layout,conversion_pipeline_layout;
 vk::UniquePipeline pipeline,conversion_pipeline;
 vk::UniqueDescriptorPool pool;
 std::deque<Clock> clocks;
 BbNativeAsync::Frame current,last;
 double clock_ms=0,current_dt_ms=0;
 unsigned slot=0,active=0,candidate=2;
 bool initialized=false,prepared=false,applied=false,history=false,unknown=false,candidate_recorded=false;
 vk::Image return_image{};
 const Scheduler* timeline=nullptr;
 explicit Impl(const Instance&);
 HistoryImage Make(vk::Format);
 void Init(vk::CommandBuffer);
 void Mark(Scheduler&);
 bool Free(const Slot& s)const noexcept{return !s.proof->unknown&&(!s.proof->last||s.proof->observed.load(std::memory_order_acquire)>=s.proof->last);}
 bool Time(uint64_t,double&)const;
 std::array<vk::ImageView,3> Converted()const;
 void Convert(Scheduler&,const std::array<vk::Image,3>&);
 void Pass(Scheduler&,unsigned mode,unsigned from,unsigned to,const std::array<vk::ImageView,3>&,
           vk::ImageView extra0,vk::ImageView extra1,const BbNativeAsync::Frame&,bool valid,
           float delta_frames,float delta_ms,float origin_frames=0,float origin_ms=0);
};
HistoryImage NativePersistentHistory::Impl::Make(vk::Format format){
 HistoryImage x;x.image=VideoCore::UniqueImage(device,allocator);
 x.image.Create({.imageType=vk::ImageType::e2D,.format=format,.extent={Width,Height,1},.mipLevels=1,.arrayLayers=1,
 .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
 .usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc,
 .initialLayout=vk::ImageLayout::eUndefined});
 x.view=device.createImageViewUnique({.image=vk::Image(x.image),.viewType=vk::ImageViewType::e2D,.format=format,
 .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;return x;
}
NativePersistentHistory::Impl::Impl(const Instance& instance):device(instance.GetDevice()),allocator(instance.GetAllocator()),returned(instance){
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
 std::array<vk::DescriptorSetLayoutBinding,12> bindings{};
 for(unsigned k=0;k<12;k++)bindings[k]={.binding=k,.descriptorType=k==10?vk::DescriptorType::eStorageBuffer:k==11?vk::DescriptorType::eUniformBuffer:(k<3||k==5||k==6)?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 layout=device.createDescriptorSetLayoutUnique({.bindingCount=12,.pBindings=bindings.data()}).value;
 std::array<vk::DescriptorSetLayoutBinding,6> conv{};
 for(unsigned k=0;k<6;k++)conv[k]={.binding=k,.descriptorType=k<3?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 conversion_layout=device.createDescriptorSetLayoutUnique({.bindingCount=6,.pBindings=conv.data()}).value;
 pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*layout}).value;
 conversion_pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*conversion_layout}).value;
 const auto create=[&](std::span<const u32> code,vk::PipelineLayout pl){auto module=CompileSPV(code,device);auto p=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=pl});device.destroyShaderModule(module);return std::move(p.value);};
 pipeline=create(NativePersistentHistorySpv,*pipeline_layout);conversion_pipeline=create(NATIVE_SKY_CONVERT_COMP,*conversion_pipeline_layout);
 constexpr unsigned count=Passes*2;
 const std::array<vk::DescriptorPoolSize,4> sizes{{{vk::DescriptorType::eCombinedImageSampler,count*5+6},{vk::DescriptorType::eStorageImage,count*5+6},{vk::DescriptorType::eStorageBuffer,count},{vk::DescriptorType::eUniformBuffer,count}}};
 pool=device.createDescriptorPoolUnique({.maxSets=count+2,.poolSizeCount=4,.pPoolSizes=sizes.data()}).value;
 for(auto& s:slots){s.conversion=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*conversion_layout}).value.front();
  for(unsigned k=0;k<Passes;k++){s.cb[k]=std::make_unique<VideoCore::Buffer>(instance,0,sizeof(PersistentHistoryConstants),VideoCore::MemoryType::HostCached,"Persistent history tick owned constants");s.sets[k]=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*layout}).value.front();}
 }
 if(NativePersistentHistoryDiagnostic::Enabled())diagnostic=std::make_unique<NativePersistentHistoryDiagnostic>(instance);
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
 c.HistoryValid=valid?1:0;c.MvScaleX=c.MvScaleY=1;
 c.JitterDeltaX=(frame.previous_viewport[0]-frame.viewport[0])/float(Width);
 c.JitterDeltaY=(frame.previous_viewport[1]-frame.viewport[1])/float(Height);
 c.CurrentJitterX=frame.viewport[0];c.CurrentJitterY=frame.viewport[1];
 std::copy(frame.current_projection.begin(),frame.current_projection.end(),c.CurrentProjection);
 std::copy(frame.previous_projection.begin(),frame.previous_projection.end(),c.PreviousProjection);
 std::copy(frame.previous_view_z.begin(),frame.previous_view_z.end(),c.PreviousViewZ);
 c.DeltaFrames=delta_frames;c.DeltaMs=delta_ms;c.OriginAgeFrames=origin_frames;c.OriginAgeMs=origin_ms;
 std::memcpy(buffer.mapped_data.data(),&c,sizeof c);buffer.Flush(0,sizeof c);
 const vk::BufferMemoryBarrier2 host{.srcStageMask=vk::PipelineStageFlagBits2::eHost,.srcAccessMask=vk::AccessFlagBits2::eHostWrite,
 .dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eUniformRead,
 .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer.Handle(),.offset=0,.size=sizeof c};
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&host});
 // No descriptor in this slot is rewritten twice between actual completions.
 std::array<vk::DescriptorImageInfo,10> ii{};
 for(unsigned k=0;k<3;k++)ii[k]={.sampler=*nearest,.imageView=current_views[k],.imageLayout=vk::ImageLayout::eGeneral};
 ii[3]={.imageView=banks[from].edit.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[4]={.imageView=banks[from].meta.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[5]={.sampler=*nearest,.imageView=extra0,.imageLayout=vk::ImageLayout::eGeneral};
 ii[6]={.sampler=*nearest,.imageView=extra1,.imageLayout=vk::ImageLayout::eGeneral};
 ii[7]={.imageView=banks[to].edit.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[8]={.imageView=banks[to].meta.View(),.imageLayout=vk::ImageLayout::eGeneral};
 ii[9]={.imageView=output.View(),.imageLayout=vk::ImageLayout::eGeneral};
 const vk::DescriptorBufferInfo si{state->Handle(),0,32},bi{buffer.Handle(),0,sizeof c};
 std::array<vk::WriteDescriptorSet,12> writes{};
 for(unsigned k=0;k<12;k++)writes[k]={.dstSet=s.sets[n],.dstBinding=k,.descriptorCount=1,
 .descriptorType=k==10?vk::DescriptorType::eStorageBuffer:k==11?vk::DescriptorType::eUniformBuffer:(k<3||k==5||k==6)?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,
 .pImageInfo=k<10?&ii[k]:nullptr,.pBufferInfo=k==10?&si:k==11?&bi:nullptr};
 Barrier(cmd);
 if(mode!=PHCompose){cmd.fillBuffer(state->Handle(),to*4,4,0);Barrier(cmd);}
 device.updateDescriptorSets(writes,{});cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);
 cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*pipeline_layout,0,s.sets[n],{});cmd.dispatch(160,90,1);Barrier(cmd);Mark(scheduler);
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
bool NativePersistentHistory::BeginCurrent(Scheduler& scheduler,const std::array<vk::Image,3>& raw,const BbNativeAsync::Frame& frame,double dt){
 auto& p=*impl;p.prepared=false;p.applied=false;p.candidate_recorded=false;
 if(p.unknown||!Enabled())return false;
 if(p.timeline&&p.timeline!=&scheduler){HoldUnknown();return false;}p.timeline=&scheduler;
 if(!BbNativeAsync::Finite(frame)||!std::isfinite(dt)||dt<=0||!raw[0]||!raw[1]||!raw[2]){p.history=false;return false;}
 if(!p.clocks.empty()&&frame.source<=p.clocks.back().source){p.history=false;return false;}
 p.current_dt_ms=dt*1000.;p.clock_ms+=p.current_dt_ms;p.clocks.push_back({frame.source,p.clock_ms,frame.history,frame.params});while(p.clocks.size()>16)p.clocks.pop_front();
 const unsigned next=1-p.slot;
 // IsFree refreshes this exact Native timeline; it never compares Helper serials.
 auto& s=p.slots[next];if(s.proof->last&&scheduler.IsFree(s.proof->last))s.proof->observed.store(s.proof->last,std::memory_order_release);
 if(!p.Free(s)){p.history=false;return false;}
 // Job epoch changes are not a visual reset: history/params/reset and real
 // consecutive source lineage govern the independent active lifetime.
 const bool continuous=p.history&&p.last.source&&frame.source==p.last.source+1&&frame.previous==p.last.source&&
 frame.history==p.last.history&&frame.params==p.last.params&&!frame.sr_reset;
 if(!continuous)p.history=false;
 p.slot=next;s.used=0;s.source_id=frame.source;s.borrowed.clear();p.current=frame;p.current_raw=raw[0];
 auto cmd=scheduler.CommandBuffer();if(!p.initialized)p.Init(cmd);p.Convert(scheduler,raw);
 const auto views=p.Converted();const unsigned next_bank=1-p.active;
 p.Pass(scheduler,PHAdvance,p.active,next_bank,views,p.banks[p.active].edit.View(),p.banks[p.active].meta.View(),frame,p.history,1,float(p.current_dt_ms));
 p.active=next_bank;p.last=frame;p.prepared=true;
 return true;
}
bool NativePersistentHistory::RecordCandidate(Scheduler& scheduler,std::span<const Link> links,vk::ImageView nr){
 auto& p=*impl;
 if(!p.prepared||p.applied||p.candidate_recorded||p.unknown||p.timeline!=&scheduler||!nr||links.empty()||links.size()>BbNativeAsync::NodeCount)return false;
 if(links.back().frame.source!=p.current.source)return false;
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
 // Preserve all imported image views until this slot's real last SR/merge use.
 // The caller separately owns the NR view and converted origin packet leases.
 double previous_time=origin_time;unsigned current_bank=2;
 for(size_t k=0;k<links.size();++k){const auto& link=links[k];std::array<vk::ImageView,3> views{};
  for(unsigned j=0;j<3;j++){auto v=p.device.createImageViewUnique({.image=link.converted[j],.viewType=vk::ImageViewType::e2D,.format=j==0?vk::Format::eR32G32B32A32Sfloat:j==1?vk::Format::eR32Sfloat:vk::Format::eR32G32Sfloat,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;views[j]=*v;s.borrowed.push_back(std::move(v));}
  double ms=0;p.Time(link.frame.source,ms);const auto to=k?5-current_bank:2;
  p.Pass(scheduler,k?PHAdvance:PHCapture,current_bank,to,views,views[0],nr,link.frame,k!=0,k?1.f:0.f,k?float(ms-previous_time):0.f);
  current_bank=to;previous_time=ms;
 }
 // Destination is the spare ACTIVE bank. The old ACTIVE and both candidate
 // banks remain untouched while every pixel checks the complete candidate flag.
 const auto to=1-p.active;const auto views=p.Converted();
 p.Pass(scheduler,PHAdopt,current_bank,to,views,p.banks[p.active].edit.View(),p.banks[p.active].meta.View(),p.current,true,0,0);
 p.active=to;p.candidate=current_bank;p.history=true;p.candidate_recorded=true;p.candidate_origin=links.front().frame.source;p.candidate_current=p.current.source;++p.candidate_count;
 std::printf("PERSISTENT_HISTORY_CANDIDATE_RECORDED source=%llu origin=%llu links=%zu candidate_bank=%u active_bank=%u status=PENDING_GPU_GUARD\n",(unsigned long long)p.current.source,(unsigned long long)links.front().frame.source,links.size(),p.candidate,p.active);
 return true;
}
vk::ImageView NativePersistentHistory::Apply(Scheduler& scheduler){
 auto& p=*impl;if(!p.prepared||p.applied||p.unknown||p.timeline!=&scheduler)return {};
 const auto views=p.Converted();p.Pass(scheduler,PHCompose,p.active,1-p.active,views,p.banks[p.active].edit.View(),p.banks[p.active].meta.View(),p.current,p.history,0,0);
 p.return_image=p.returned.Record(scheduler,p.slot,p.output.View(),*p.slots[p.slot].source[0],p.history,false);p.Mark(scheduler);p.applied=true;
 if(p.diagnostic){
  NativePersistentHistoryDiagnostic::Snapshot snapshot;
  snapshot.current=p.current;snapshot.images={p.current_raw,p.return_image,vk::Image(p.banks[p.active].edit.image),vk::Image(p.banks[p.active].meta.image)};
  snapshot.state=p.state->Handle();snapshot.guard=p.returned.Guard(p.slot);snapshot.active_bank=p.active;snapshot.candidate_bank=p.candidate;
  snapshot.candidate_origin=p.candidate_origin;snapshot.candidate_current=p.candidate_current;snapshot.candidate_count=p.candidate_count;
  snapshot.host_history=p.history;snapshot.candidate_recorded=p.candidate_recorded;
  if(p.diagnostic->Record(scheduler,snapshot))p.Mark(scheduler);
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
 if(p.diagnostic)p.diagnostic->ObserveCompletion(scheduler);
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
void NativePersistentHistory::HoldUnknown()noexcept{impl->unknown=true;if(impl->diagnostic)impl->diagnostic->HoldUnknown();for(auto& s:impl->slots)s.proof->unknown=true;}
bool NativePersistentHistory::HasHistory()const noexcept{return impl->history&&!impl->unknown;}
void NativePersistentHistory::Reset()noexcept{impl->history=false;impl->prepared=false;impl->applied=false;}
uint64_t NativePersistentHistory::ImageBytes()const noexcept{
 // 4*(RGBA16 edit + RGBA32 metadata), 2 independent current conversion sets,
 // one queue-ordered RGBA32 compose, and Return's five original-alpha half slots.
 return uint64_t(Width)*Height*(4*(8+16)+2*(16+4+8)+16)+NativeAsyncPreSrReturn::PackedBytes;
}
}
