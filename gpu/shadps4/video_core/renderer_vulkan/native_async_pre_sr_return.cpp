// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_async_pre_sr_return.h"
#include "vk_instance.h"
#include "vk_scheduler.h"
#include "vk_shader_util.h"
#include "video_core/host_shaders/native_async_pre_sr_return_comp.h"
#include "video_core/host_shaders/native_async_pre_sr_masked_return_comp.h"
#include <stdexcept>
#include <span>
namespace Vulkan {
NativeAsyncPreSrReturn::NativeAsyncPreSrReturn(const Instance& i):device(i.GetDevice()){
 const auto f=i.GetPhysicalDevice().getFormatProperties(vk::Format::eR16G16B16A16Sfloat).optimalTilingFeatures;
 if(!(f&vk::FormatFeatureFlagBits::eStorageImage)||!(f&vk::FormatFeatureFlagBits::eSampledImage))throw std::runtime_error("preSR private half storage/sample unsupported");
 const unsigned binding_count=MaskEnabled()?5:4;std::array<vk::DescriptorSetLayoutBinding,5> bs{};
 for(unsigned k=0;k<binding_count;k++)bs[k]={.binding=k,.descriptorType=k<2?vk::DescriptorType::eCombinedImageSampler:k==2?vk::DescriptorType::eStorageImage:vk::DescriptorType::eStorageBuffer,.descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eCompute};
 layout=device.createDescriptorSetLayoutUnique({.bindingCount=binding_count,.pBindings=bs.data()}).value;
 const vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute,0,MaskEnabled()?12u:4u};pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push}).value;
 auto module=MaskEnabled()?CompileSPV(NATIVE_ASYNC_PRE_SR_MASKED_RETURN_COMP,device):CompileSPV(NATIVE_ASYNC_PRE_SR_RETURN_COMP,device);pipeline=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=*pipeline_layout}).value;device.destroyShaderModule(module);
 nearest=device.createSamplerUnique({.magFilter=vk::Filter::eNearest,.minFilter=vk::Filter::eNearest,.mipmapMode=vk::SamplerMipmapMode::eNearest,.addressModeU=vk::SamplerAddressMode::eClampToEdge,.addressModeV=vk::SamplerAddressMode::eClampToEdge,.addressModeW=vk::SamplerAddressMode::eClampToEdge,.maxLod=0}).value;
 const std::array<vk::DescriptorPoolSize,3> sizes{{{vk::DescriptorType::eCombinedImageSampler,10},{vk::DescriptorType::eStorageImage,5},{vk::DescriptorType::eStorageBuffer,MaskEnabled()?10u:5u}}};pool=device.createDescriptorPoolUnique({.maxSets=5,.poolSizeCount=3,.pPoolSizes=sizes.data()}).value;
 for(unsigned k=0;k<5;k++){
  images[k]=VideoCore::UniqueImage(device,i.GetAllocator());images[k].Create({.imageType=vk::ImageType::e2D,.format=vk::Format::eR16G16B16A16Sfloat,.extent={1280,720,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc,.initialLayout=vk::ImageLayout::eUndefined});
  views[k]=device.createImageViewUnique({.image=vk::Image(images[k]),.viewType=vk::ImageViewType::e2D,.format=vk::Format::eR16G16B16A16Sfloat,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;
  flags[k]=std::make_unique<VideoCore::Buffer>(i,0,4,VideoCore::MemoryType::DeviceLocal,"preSR wholeframe numeric guard no CPU readback");
  sets[k]=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*layout}).value.front();
 }
 if(MaskEnabled())for(unsigned k=0;k<2;++k)masks[k]=std::make_unique<VideoCore::Buffer>(i,0,4,VideoCore::MemoryType::DeviceLocal,"Native shown numeric GPU-only mask");
}
void NativeAsyncPreSrReturn::MarkMaskUse(Scheduler&s){if(MaskEnabled())mask_use->last_use=std::max(mask_use->last_use,s.CurrentTick());}
bool NativeAsyncPreSrReturn::BeginMaskCurrent(Scheduler&s,uint64_t source){
 if(!MaskEnabled())return false;
 if(mask_use->unknown||!source||source<mask_source)throw std::runtime_error("masked return exact current source/unknown rejected");
 if(source==mask_source)return false;
 if(masks_initialized)next_mask=1-next_mask;
 auto cmd=s.CommandBuffer();
 // Same queue prior COMPUTE reads/writes -> new TRANSFER clear. No CPU result read.
 for(unsigned k=0;k<2;++k)if(!masks_initialized||k==next_mask){
  const vk::BufferMemoryBarrier2 before{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eTransfer,.dstAccessMask=vk::AccessFlagBits2::eTransferWrite,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=masks[k]->Handle(),.offset=0,.size=4};
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&before});cmd.fillBuffer(masks[k]->Handle(),0,4,0);
  auto ready=before;ready.srcStageMask=vk::PipelineStageFlagBits2::eTransfer;ready.srcAccessMask=vk::AccessFlagBits2::eTransferWrite;ready.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader;ready.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite;cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&ready});
 }
 masks_initialized=true;mask_source=source;MarkMaskUse(s);return true;
}
vk::Image NativeAsyncPreSrReturn::Record(Scheduler& s,unsigned k,vk::ImageView composite,vk::ImageView raw,bool wholeEligible,bool contextWritten){
 if(k>=5||!composite||!raw)throw std::runtime_error("preSR exact current return inputs missing");
 if(MaskEnabled()&&!masks_initialized)throw std::runtime_error("masked return requires current-source clear");
 auto cmd=s.CommandBuffer();auto buffer=flags[k]->Handle();cmd.fillBuffer(buffer,0,4,0);
 const vk::BufferMemoryBarrier2 clear{.srcStageMask=vk::PipelineStageFlagBits2::eTransfer,.srcAccessMask=vk::AccessFlagBits2::eTransferWrite,.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=buffer,.offset=0,.size=4};
 const vk::ImageMemoryBarrier2 output{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=vk::Image(images[k]),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
 cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&clear,.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&output});
 const std::array<vk::DescriptorImageInfo,3> info{{{*nearest,composite,vk::ImageLayout::eGeneral},{*nearest,raw,vk::ImageLayout::eGeneral},{{},*views[k],vk::ImageLayout::eGeneral}}};const vk::DescriptorBufferInfo bi{buffer,0,4};const vk::DescriptorBufferInfo mi{MaskEnabled()?NextMask():vk::Buffer{},0,4};std::array<vk::WriteDescriptorSet,5> writes{};const unsigned bindings=MaskEnabled()?5:4;
 for(unsigned n=0;n<bindings;n++)writes[n]={.dstSet=sets[k],.dstBinding=n,.descriptorCount=1,.descriptorType=n<2?vk::DescriptorType::eCombinedImageSampler:n==2?vk::DescriptorType::eStorageImage:vk::DescriptorType::eStorageBuffer,.pImageInfo=n<3?&info[n]:nullptr,.pBufferInfo=n==3?&bi:n==4?&mi:nullptr};device.updateDescriptorSets(std::span<const vk::WriteDescriptorSet>(writes.data(),bindings),{});cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*pipeline_layout,0,sets[k],{});
 std::array<uint32_t,3> mode{0,wholeEligible?1u:0u,contextWritten?1u:0u};const unsigned push_bytes=MaskEnabled()?12:4;cmd.pushConstants(*pipeline_layout,vk::ShaderStageFlagBits::eCompute,0,push_bytes,mode.data());cmd.dispatch(160,90,1);
 auto ready=clear;ready.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader;ready.srcAccessMask=vk::AccessFlagBits2::eShaderWrite;ready.dstAccessMask=vk::AccessFlagBits2::eShaderRead;cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&ready});
 mode[0]=1;cmd.pushConstants(*pipeline_layout,vk::ShaderStageFlagBits::eCompute,0,push_bytes,mode.data());cmd.dispatch(160,90,1);
 auto sr=output;sr.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader;sr.srcAccessMask=vk::AccessFlagBits2::eShaderWrite;sr.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands;sr.dstAccessMask=vk::AccessFlagBits2::eShaderRead;sr.oldLayout=vk::ImageLayout::eGeneral;cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&sr});
 if(MaskEnabled()){
  const vk::BufferMemoryBarrier2 next_read{.srcStageMask=vk::PipelineStageFlagBits2::eComputeShader,.srcAccessMask=vk::AccessFlagBits2::eShaderWrite,.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderRead,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=NextMask(),.offset=0,.size=4};
  cmd.pipelineBarrier2({.bufferMemoryBarrierCount=1,.pBufferMemoryBarriers=&next_read});MarkMaskUse(s);
 }
 return vk::Image(images[k]);
}
}
