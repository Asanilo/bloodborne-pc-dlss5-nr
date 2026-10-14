// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_async_pre_sr_sky.h"
#include "native_sky_r2_spv.h"
#include "native_sky_masked_spv.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include "vk_shader_util.h"
#include "video_core/host_shaders/native_sky_convert_comp.h"
#include <cstring>
#include <stdexcept>
#include <span>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace Vulkan {
NativeAsyncPreSrSky::Image NativeAsyncPreSrSky::Make(vk::Format f,unsigned w,unsigned h){
 Image x;x.image=VideoCore::UniqueImage(device,allocator);
 x.image.Create({.imageType=vk::ImageType::e2D,.format=f,.extent={w,h,1},.mipLevels=1,.arrayLayers=1,
  .samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,
  .usage=vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eTransferSrc,
  .initialLayout=vk::ImageLayout::eUndefined});
 x.view=device.createImageViewUnique({.image=vk::Image(x.image),.viewType=vk::ImageViewType::e2D,.format=f,
  .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;return x;
}
NativeAsyncPreSrSky::NativeAsyncPreSrSky(const Instance& i):device(i.GetDevice()),allocator(i.GetAllocator()){
 for(auto f:{vk::Format::eR32G32B32A32Sfloat,vk::Format::eR32Sfloat,vk::Format::eR32G32Sfloat,vk::Format::eR32Uint}){
  const auto flags=i.GetPhysicalDevice().getFormatProperties(f).optimalTilingFeatures;
  const auto required=vk::FormatFeatureFlagBits::eStorageImage|vk::FormatFeatureFlagBits::eSampledImage|vk::FormatFeatureFlagBits::eTransferDst|vk::FormatFeatureFlagBits::eTransferSrc;
  if((flags&required)!=required)throw std::runtime_error("Native Sky format support missing");
  if(f==vk::Format::eR32G32B32A32Sfloat&&!(flags&vk::FormatFeatureFlagBits::eSampledImageFilterLinear))throw std::runtime_error("Native Sky RGBA32F linear unsupported");
  if(f==vk::Format::eR32Uint&&!(flags&vk::FormatFeatureFlagBits::eStorageImageAtomic))throw std::runtime_error("Native Sky R32UI atomic unsupported");
 }
 if(!i.GetPhysicalDevice().getFeatures().shaderStorageImageExtendedFormats)throw std::runtime_error("Native Sky extended storage formats unsupported");
 const auto limits=i.GetPhysicalDevice().getProperties().limits;
 if(limits.maxPerStageDescriptorStorageImages<7||limits.maxPerStageDescriptorSampledImages<(Masked()?14u:12u)||limits.maxUniformBufferRange<256)throw std::runtime_error("Native Sky descriptor limits");
 for(auto& b:banks){b.edit=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);b.guide=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);b.l1guide=Make(vk::Format::eR32G32B32A32Sfloat,320,180);unsigned w=320,h=180;for(auto& l:b.levels){l=Make(vk::Format::eR32G32B32A32Sfloat,w,h);w=(w+3)/4;h=(h+3)/4;}}
 dummy=Make(vk::Format::eR32G32B32A32Sfloat,1,1);exposure=Make(vk::Format::eR32G32B32A32Sfloat,1,1);stats=Make(vk::Format::eR32Uint,4,1);
 const auto sampler=[&](vk::Filter f){return device.createSamplerUnique({.magFilter=f,.minFilter=f,.mipmapMode=vk::SamplerMipmapMode::eNearest,.addressModeU=vk::SamplerAddressMode::eClampToEdge,.addressModeV=vk::SamplerAddressMode::eClampToEdge,.addressModeW=vk::SamplerAddressMode::eClampToEdge,.maxLod=0}).value;};
 linear=sampler(vk::Filter::eLinear);nearest=sampler(vk::Filter::eNearest);
 const unsigned binding_count=Masked()?24:21,dispatch_slots=Masked()?6:4;
 std::array<vk::DescriptorSetLayoutBinding,24> bindings{};
 for(unsigned k=0;k<binding_count;k++)bindings[k]={.binding=k,.descriptorType=k==23?vk::DescriptorType::eStorageBuffer:k==0?vk::DescriptorType::eUniformBuffer:k<=12?vk::DescriptorType::eSampledImage:k<=19?vk::DescriptorType::eStorageImage:k==20?vk::DescriptorType::eSampler:vk::DescriptorType::eSampledImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 layout=device.createDescriptorSetLayoutUnique({.bindingCount=binding_count,.pBindings=bindings.data()}).value;
 std::array<vk::DescriptorSetLayoutBinding,6> cb{};for(unsigned k=0;k<6;k++)cb[k]={.binding=k,.descriptorType=k<3?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 conversion_layout=device.createDescriptorSetLayoutUnique({.bindingCount=6,.pBindings=cb.data()}).value;
 pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*layout}).value;
 conversion_pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*conversion_layout}).value;
 const auto create=[&](std::span<const u32> code,vk::PipelineLayout pl,const char* entry){auto mod=CompileSPV(code,device);auto p=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=mod,.pName=entry},.layout=pl});device.destroyShaderModule(mod);return std::move(p.value);};
 pipeline=Masked()?create(NativeSkyMaskedSpv,*pipeline_layout,"CSMain"):create(NativeSkyR2Spv,*pipeline_layout,"CSMain");conversion_pipeline=create(NATIVE_SKY_CONVERT_COMP,*conversion_pipeline_layout,"main");
 const unsigned sets_count=dispatch_slots*10;std::array<vk::DescriptorPoolSize,6> sizes{{{vk::DescriptorType::eUniformBuffer,sets_count},{vk::DescriptorType::eSampledImage,sets_count*(Masked()?14u:12u)},{vk::DescriptorType::eStorageImage,sets_count*7+15},{vk::DescriptorType::eSampler,sets_count},{vk::DescriptorType::eCombinedImageSampler,15},{vk::DescriptorType::eStorageBuffer,Masked()?sets_count:1u}}};
 pool=device.createDescriptorPoolUnique({.maxSets=sets_count+5,.poolSizeCount=Masked()?6u:5u,.pPoolSizes=sizes.data()}).value;
 for(auto& p:packets){p.converted[0]=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);p.converted[1]=Make(vk::Format::eR32Sfloat,1280,720);p.converted[2]=Make(vk::Format::eR32G32Sfloat,1280,720);p.output=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);
  for(unsigned j=0;j<dispatch_slots;j++){p.constants[j]=std::make_unique<VideoCore::Buffer>(i,0,256,VideoCore::MemoryType::HostCached,"Native Sky tick-owned constants");p.sets[j]=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*layout}).value.front();}
  p.conversion=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*conversion_layout}).value.front();
 }
 for(auto& p:replay_packets){p.output=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);for(unsigned j=0;j<dispatch_slots;j++){p.constants[j]=std::make_unique<VideoCore::Buffer>(i,0,256,VideoCore::MemoryType::HostCached,"Native Sky sparse catchup tick-owned constants");p.sets[j]=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*layout}).value.front();}}

 if(Masked()){
  raw_edit=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);
  for(unsigned k=0;k<2;++k){shown_edit[k]=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);shown_guide[k]=Make(vk::Format::eR32G32B32A32Sfloat,1280,720);shown_context[k]=Make(vk::Format::eR32G32B32A32Sfloat,320,180);}
 }

}
void NativeAsyncPreSrSky::Barrier(vk::CommandBuffer cmd){const vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite};cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b});}
void NativeAsyncPreSrSky::InitImages(vk::CommandBuffer cmd){
 vk::ClearColorValue zero{};for(unsigned k=0;k<4;k++)zero.uint32[k]=0;
 const auto init=[&](Image& x){const vk::ImageMemoryBarrier2 b{.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=vk::Image(x.image),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});cmd.clearColorImage(vk::Image(x.image),vk::ImageLayout::eGeneral,zero,vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor,0,1,0,1});};
 for(auto& b:banks){init(b.edit);init(b.guide);init(b.l1guide);for(auto& l:b.levels)init(l);}for(auto& p:packets){for(auto& x:p.converted)init(x);init(p.output);}for(auto& p:replay_packets)init(p.output);init(dummy);init(exposure);init(stats);if(Masked()){init(raw_edit);for(unsigned k=0;k<2;++k){init(shown_edit[k]);init(shown_guide[k]);init(shown_context[k]);}}Barrier(cmd);initialized=true;
}
DlssNrCacheConstants NativeAsyncPreSrSky::Base(unsigned m)const {DlssNrCacheConstants c{};c.Mode=m;c.Width=1280;c.Height=720;c.DepthWidth=c.MotionWidth=1280;c.DepthHeight=c.MotionHeight=720;c.MvScaleX=c.MvScaleY=1;c.Epsilon=1.f/512;c.DepthTolerance=.1f;c.ColourTolerance=.5f;c.HighDecay=.97f;c.RefreshBlend=c.LowGain=c.HighGain=1;c.Bilateral=1;c.ExposurePreMul=1;c.MaxLumaEdit=1.5f;return c;}
void NativeAsyncPreSrSky::Pass(vk::CommandBuffer cmd,Packet& p,unsigned slot,const DlssNrCacheConstants& c,bool whole,const std::array<Image*,12>& reads,const std::array<Image*,7>& writes,Image* context_now,Image* context_prev){
 auto& buf=*p.constants[slot];std::memcpy(buf.mapped_data.data(),&c,256);const uint32_t v=whole?1:0;std::memcpy(buf.mapped_data.data()+156,&v,4);buf.Flush(0,256);
 const vk::BufferMemoryBarrier2 hb{.srcStageMask=vk::PipelineStageFlagBits2::eHost,.srcAccessMask=vk::AccessFlagBits2::eHostWrite,.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eUniformRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buf.Handle(),.offset=0,.size=256};cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&hb});
 const vk::DescriptorBufferInfo bi{buf.Handle(),0,256};std::array<vk::DescriptorImageInfo,22> ii{};std::array<vk::WriteDescriptorSet,24> ds{};const unsigned binding_count=Masked()?24:21;
 if(Masked()&&!previous_mask)throw std::runtime_error("masked Sky actual previous GPU buffer absent");
 const vk::DescriptorBufferInfo mask_info{previous_mask,0,4};
 for(unsigned k=0;k<12;k++)ii[k]={.imageView=reads[k]->View(),.imageLayout=vk::ImageLayout::eGeneral};for(unsigned k=0;k<7;k++)ii[k+12]={.imageView=writes[k]->View(),.imageLayout=vk::ImageLayout::eGeneral};ii[19].sampler=*linear;
 if(Masked()){ii[20]={.imageView=context_now?context_now->View():dummy.View(),.imageLayout=vk::ImageLayout::eGeneral};ii[21]={.imageView=context_prev?context_prev->View():dummy.View(),.imageLayout=vk::ImageLayout::eGeneral};}
 for(unsigned k=0;k<binding_count;k++)ds[k]={.dstSet=p.sets[slot],.dstBinding=k,.descriptorCount=1,.descriptorType=k==23?vk::DescriptorType::eStorageBuffer:k==0?vk::DescriptorType::eUniformBuffer:k<=12?vk::DescriptorType::eSampledImage:k<=19?vk::DescriptorType::eStorageImage:k==20?vk::DescriptorType::eSampler:vk::DescriptorType::eSampledImage,.pImageInfo=k&&k!=23?&ii[k-1]:nullptr,.pBufferInfo=k==0?&bi:k==23?&mask_info:nullptr};
 device.updateDescriptorSets(std::span<const vk::WriteDescriptorSet>(ds.data(),binding_count),{});cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*pipeline_layout,0,p.sets[slot],{});cmd.dispatch((c.Width+7)/8,(c.Height+7)/8,1);Barrier(cmd);
}
vk::ImageView NativeAsyncPreSrSky::Record(Scheduler& scheduler,unsigned index,const std::array<vk::Image,3>& source,bool capture,const BbNativeAsync::Frame& frame,bool actual_cache_eligible){
 if(index!=next_index||index>=packets.size()||capture!=(index==0))throw std::runtime_error("Native Sky actual consecutive lineage required");
 auto cmd=scheduler.CommandBuffer();if(!initialized)InitImages(cmd);auto& p=packets[index];p.frame=frame;
 for(unsigned k=0;k<3;k++){p.source_views[k]=device.createImageViewUnique({.image=source[k],.viewType=vk::ImageViewType::e2D,.format=k==0?vk::Format::eR16G16B16A16Sfloat:k==1?vk::Format::eD32SfloatS8Uint:vk::Format::eR16G16Sfloat,.subresourceRange={k==1?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;}
 std::array<vk::DescriptorImageInfo,6> ci{};std::array<vk::WriteDescriptorSet,6> cw{};
 for(unsigned k=0;k<6;k++){ci[k]={.sampler=k<3?*nearest:vk::Sampler{},.imageView=k<3?*p.source_views[k]:*p.converted[k-3].view,.imageLayout=vk::ImageLayout::eGeneral};cw[k]={.dstSet=p.conversion,.dstBinding=k,.descriptorCount=1,.descriptorType=k<3?vk::DescriptorType::eCombinedImageSampler:vk::DescriptorType::eStorageImage,.pImageInfo=&ci[k]};}
 Barrier(cmd);device.updateDescriptorSets(cw,{});cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*conversion_pipeline);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*conversion_pipeline_layout,0,p.conversion,{});cmd.dispatch(160,90,1);Barrier(cmd);
 auto& prev=banks[index%2];auto& next=banks[(index+1)%2];auto& color=p.converted[0];auto& depth=p.converted[1];auto& mv=p.converted[2];
 // Controlled noop origin: edited source is exactly the same converted immutable SR.
 std::array<Image*,12> reads{&prev.edit,&prev.guide,&color,&depth,&mv,&color,&dummy,&dummy,&dummy,&dummy,&exposure,&dummy};
 auto c=Base(capture?2:1);c.HistoryValid=capture?0:1;c.JitterDeltaX=(frame.previous_viewport[0]-frame.viewport[0])/1280.f;c.JitterDeltaY=(frame.previous_viewport[1]-frame.viewport[1])/720.f;Pass(cmd,p,0,c,true,reads,{&next.edit,&next.guide,&p.output,&next.levels[0],&next.l1guide,&stats,&dummy});
 for(unsigned j=1;j<3;j++){auto d=Base(3);d.Width=next.levels[j].image.image_ci.extent.width;d.Height=next.levels[j].image.image_ci.extent.height;d.SourceWidth=next.levels[j-1].image.image_ci.extent.width;d.SourceHeight=next.levels[j-1].image.image_ci.extent.height;reads[5]=&next.levels[j-1];Pass(cmd,p,j,d,true,reads,{&next.levels[j],&dummy,&p.output,&dummy,&dummy,&stats,&dummy});}
 reads={&next.edit,&next.guide,&color,&depth,&mv,&next.levels[0],&next.l1guide,&next.levels[1],&next.levels[2],&dummy,&exposure,&dummy};auto apply=Base(4);if(Masked()){apply.LowTemporal=.95f;apply.Temporal=0;apply.CrossfadeOn=0;}Pass(cmd,p,3,apply,true,reads,{&p.output,&dummy,&dummy,Masked()?&raw_edit:&dummy,&dummy,&stats,&dummy});
 if(Masked()){last_apply_source=frame.source;last_apply_replay=false;if(actual_cache_eligible)CopyCurrentGuide(cmd,(index+1)%2,frame.source);}
 next_index++;return *p.output.view;
}

std::array<vk::Image,3> NativeAsyncPreSrSky::ConvertedOrigin()const noexcept {return {vk::Image(packets[0].converted[0].image),vk::Image(packets[0].converted[1].image),vk::Image(packets[0].converted[2].image)};}
vk::ImageView NativeAsyncPreSrSky::RecordSparseCatchup(Scheduler& s,unsigned current,vk::ImageView nr){
 if(!nr||current>=next_index||current>=packets.size()||replay_busy.exchange(true,std::memory_order_acq_rel))return {};
 // A fresh, independent descriptor/UBO set for every actual retained link. Earlier
 // local-noop/per-frame packets may STILL be in flight; never rewrite those sets.
 // The renderer validates complete identity/lineage before entering this API.
 auto cmd=s.CommandBuffer();Image edited;edited.borrowed=nr;
 for(unsigned index=0;index<=current;index++){
  auto& p=replay_packets[index];auto& src=packets[index];auto& prev=banks[index%2];auto& next=banks[(index+1)%2];
  auto& color=src.converted[0];auto& depth=src.converted[1];auto& mv=src.converted[2];
  std::array<Image*,12> reads{&prev.edit,&prev.guide,&color,&depth,&mv,&edited,&dummy,&dummy,&dummy,&dummy,&exposure,&dummy};
  auto c=Base(index?1:2);c.HistoryValid=index?1:0;c.JitterDeltaX=(src.frame.previous_viewport[0]-src.frame.viewport[0])/1280.f;c.JitterDeltaY=(src.frame.previous_viewport[1]-src.frame.viewport[1])/720.f;
  Barrier(cmd);Pass(cmd,p,0,c,true,reads,{&next.edit,&next.guide,&p.output,&next.levels[0],&next.l1guide,&stats,&dummy});
  for(unsigned j=1;j<3;j++){auto d=Base(3);d.Width=next.levels[j].image.image_ci.extent.width;d.Height=next.levels[j].image.image_ci.extent.height;d.SourceWidth=next.levels[j-1].image.image_ci.extent.width;d.SourceHeight=next.levels[j-1].image.image_ci.extent.height;reads[5]=&next.levels[j-1];Pass(cmd,p,j,d,true,reads,{&next.levels[j],&dummy,&p.output,&dummy,&dummy,&stats,&dummy});}
  // Only the final CURRENT link may grant a composite. Earlier replay output is
  // private scratch, never a displayed NR frame. Apply remains the frozen r2 pass.
  if(index==current){reads={&next.edit,&next.guide,&color,&depth,&mv,&next.levels[0],&next.l1guide,&next.levels[1],&next.levels[2],&dummy,&exposure,&dummy};auto apply=Base(4);if(Masked()){apply.LowTemporal=.95f;apply.Temporal=0;apply.CrossfadeOn=0;}Pass(cmd,p,3,apply,true,reads,{&p.output,&dummy,&dummy,Masked()?&raw_edit:&dummy,&dummy,&stats,&dummy});if(Masked())CopyCurrentGuide(cmd,(index+1)%2,src.frame.source);}
 }
 if(Masked()){last_apply_source=packets[current].frame.source;last_apply_replay=true;}
 return *replay_packets[current].output.view;
}

void NativeAsyncPreSrSky::CopyCurrentGuide(vk::CommandBuffer cmd,unsigned bank,uint64_t source){
 if(!Masked()||bank>=2||shown_next>=2||prepared_source!=source)throw std::runtime_error("masked guide copy current identity mismatch");
 auto& b=banks[bank];
 // Guide snapshot is THIS final current bank, not origin or an intermediate link.
 const vk::ImageMemoryBarrier2 from{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferRead,.oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eTransferSrcOptimal,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=vk::Image(b.guide.image),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
 auto to=from;to.dstAccessMask=vk::AccessFlagBits2::eTransferWrite;to.newLayout=vk::ImageLayout::eTransferDstOptimal;to.image=vk::Image(shown_guide[shown_next].image);
 const std::array before{from,to};cmd.pipelineBarrier2({.imageMemoryBarrierCount=2,.pImageMemoryBarriers=before.data()});cmd.copyImage(vk::Image(b.guide.image),vk::ImageLayout::eTransferSrcOptimal,vk::Image(shown_guide[shown_next].image),vk::ImageLayout::eTransferDstOptimal,vk::ImageCopy{.srcSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.dstSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.extent={1280,720,1}});
 auto restore=from;restore.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;restore.srcAccessMask=vk::AccessFlagBits2::eTransferRead;restore.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader;restore.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite;restore.oldLayout=vk::ImageLayout::eTransferSrcOptimal;restore.newLayout=vk::ImageLayout::eGeneral;
 auto ready=restore;ready.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;ready.oldLayout=vk::ImageLayout::eTransferDstOptimal;ready.image=vk::Image(shown_guide[shown_next].image);const std::array after{restore,ready};cmd.pipelineBarrier2({.imageMemoryBarrierCount=2,.pImageMemoryBarriers=after.data()});
 last_guide_source=source;last_guide_slot=shown_next;last_guide_bank=bank;
}

void NativeAsyncPreSrSky::PrepareShownCurrent(uint64_t source,double real_dt){
 if(!Masked())return;
 if(source==prepared_source)return;
 if(!source||source<prepared_source||shown_next>=2)throw std::runtime_error("masked current actual source order");
 prepared_source=source;current_whole=false;current_context=false;current_antipop_step=0;
 // This current's mask was GPU-cleared separately. Raw/invalid frames retire
 // metadata in the SAME chosen next slot; an old shown slot is never relabeled.
 shown_metadata[shown_next]=ShownMetadata{};shown_metadata[shown_next].frame.source=source;
 current_antipop_step=frame_clock.Update(real_dt);
}
void NativeAsyncPreSrSky::ActualSrResult(uint64_t source,bool success,Scheduler& scheduler){
 if(!Masked()||prepared_source!=source)return;
 auto& m=shown_metadata[shown_next];m.SR_success=success;m.last_use=std::max(m.last_use,scheduler.CurrentTick());
 // Numeric/context validity is NOT set here; only return shader q0 writes it.
 auto observed=m.observed;const auto tick=scheduler.CurrentTick();
 scheduler.DeferPriorityOperation([observed,tick]{auto old=observed->load();while(old<tick&&!observed->compare_exchange_weak(old,tick,std::memory_order_release,std::memory_order_relaxed)){};});
}
vk::ImageView NativeAsyncPreSrSky::FinalizeMaskedCurrent(Scheduler& scheduler,unsigned index,vk::ImageView selected,const BbNativeAsync::Frame& origin,const BbNativeAsync::Frame& current){
 if(!Masked())return selected;
 if(index>=packets.size()||prepared_source!=current.source||last_apply_source!=current.source||packets[index].frame.source!=current.source||shown_next>=2||!previous_mask)throw std::runtime_error("masked final CURRENT Apply identity absent");
 const bool replay=selected==replay_packets[index].output.View();
 if((!replay&&selected!=packets[index].output.View())||replay!=last_apply_replay)throw std::runtime_error("masked final current view/pass mismatch");
 const bool whole=origin.source>0&&current.source>=origin.source&&current.source-origin.source<=BbNativeAsync::MaxLinks&&origin.epoch==current.epoch&&origin.history==current.history&&origin.params==current.params&&!current.sr_reset;
 if(!whole){current_whole=false;return {};}
 auto& p=replay?replay_packets[index]:packets[index];auto& b=banks[(index+1)%2];auto& src=packets[index];auto& old=shown_metadata[1-shown_next];
 const bool prior=BbNativePreSrSkyPolicy::PreviousLegal(old.eligible,old.SR_success,old.frame,current,old.oldest_origin);
 const bool context_on=BbNativePreSrSkyPolicy::Current()==BbNativePreSrSkyPolicy::Mode::RegionalAntiPop;
 // Independent actual completed-context observation is conservative; no wait or
 // counter query. GPU bit1 remains additionally mandatory inside Mode10.
 const bool old_context=prior&&old.context&&old.last_use&&old.observed->load(std::memory_order_acquire)>=old.last_use;
 const float step=context_on&&old_context?current_antipop_step:0.f;
 auto cmd=scheduler.CommandBuffer();std::array<Image*,12> reads{&b.edit,&b.guide,&src.converted[0],&src.converted[1],&src.converted[2],&dummy,&dummy,&dummy,&dummy,&dummy,&exposure,&dummy};
 if(context_on){auto ctx=Base(12);ctx.Width=320;ctx.Height=180;ctx.SourceWidth=1280;ctx.SourceHeight=720;Pass(cmd,p,4,ctx,true,reads,{&shown_context[shown_next],&dummy,&dummy,&dummy,&dummy,&stats,&dummy},&shown_context[shown_next],&shown_context[1-shown_next]);}
 if(last_guide_source!=current.source||last_guide_slot!=shown_next||last_guide_bank!=(index+1)%2)throw std::runtime_error("masked final current independent guide copy absent");
 auto c=Base(10);c.LowTemporal=.95f;c.Temporal=0;c.CrossfadeOn=0;c.TemporalValid=prior?1:0;c.JitterDeltaX=(current.previous_viewport[0]-current.viewport[0])/1280.f;c.JitterDeltaY=(current.previous_viewport[1]-current.viewport[1])/720.f;
 struct Tail{uint32_t whole;float step,tolerance;uint32_t width,height;};static_assert(sizeof(Tail)==20);static_assert(offsetof(DlssNrCacheConstants,JitterDeltaY)==152);const Tail tail{1,step,.15f,320,180};std::memcpy(reinterpret_cast<unsigned char*>(&c)+156,&tail,sizeof tail);
 reads[5]=&raw_edit;reads[6]=&shown_edit[1-shown_next];reads[7]=&shown_guide[1-shown_next];
 Pass(cmd,p,5,c,true,reads,{&p.output,&dummy,&dummy,&shown_edit[shown_next],&dummy,&stats,&dummy},context_on?&shown_context[shown_next]:&dummy,old_context?&shown_context[1-shown_next]:&dummy);
 auto& next=shown_metadata[shown_next];next.eligible=true;next.context=context_on;next.frame=current;next.oldest_origin=prior?std::min(old.oldest_origin,origin.source):origin.source;next.last_use=scheduler.CurrentTick();
 current_whole=true;current_context=context_on;
 // All metadata remains recorded/PENDING. GPU numeric bit0/bit1 and actual SR
 // success independently gate the next read; no CPU numeric-valid claim exists.
 std::printf("NATIVE_PRE_SR_MASKED_RECORDED source=%llu origin=%llu oldest_contributing_origin=%llu selector=%s prior_host_eligible=%u prior_context_observed=%u anti_pop_step=%.9g current_context_written=%u current_guide_bank=%u mask_next=%u target_tick=%llu status=PENDING_GPU_NUMERIC_AND_SR_RESULT no_CPU_mask_read=1\n",(unsigned long long)current.source,(unsigned long long)origin.source,(unsigned long long)next.oldest_origin,BbNativePreSrSkyPolicy::Name(),prior,old_context,step,context_on,(index+1)%2,shown_next,(unsigned long long)scheduler.CurrentTick());
 return p.output.View();
}

uint64_t NativeAsyncPreSrSky::ImageBytes()const noexcept{uint64_t n=0;const auto add=[&](const Image& x){const auto& c=x.image.image_ci;n+=uint64_t(c.extent.width)*c.extent.height*(c.format==vk::Format::eR32G32B32A32Sfloat?16:c.format==vk::Format::eR32G32Sfloat?8:4);};for(const auto& b:banks){add(b.edit);add(b.guide);add(b.l1guide);for(const auto& l:b.levels)add(l);}for(const auto& p:packets){for(const auto& x:p.converted)add(x);add(p.output);}for(const auto& p:replay_packets)add(p.output);add(dummy);add(exposure);add(stats);if(Masked()){add(raw_edit);for(unsigned k=0;k<2;++k){add(shown_edit[k]);add(shown_guide[k]);add(shown_context[k]);}}return n;}
}
