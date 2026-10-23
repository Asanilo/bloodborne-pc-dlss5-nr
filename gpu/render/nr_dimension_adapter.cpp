// SPDX-License-Identifier: GPL-2.0-or-later
#include "nr_dimension_adapter.h"
#include "nr_dimensions.h"
#include "render/scheduler.h"
#include "vulkan/instance.h"
#include "vulkan/shader_module.h"
#include "bbgpu/shaders/nr_dimensions_motion_comp.h"
#include "bbgpu/shaders/nr_dimensions_apply_comp.h"
#include <stdexcept>
namespace Vulkan {
namespace {
void Barrier(vk::CommandBuffer cmd,vk::Image image,vk::ImageAspectFlags aspect,vk::AccessFlags2 dst){
 vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=dst,.oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={aspect,0,1,0,1}};cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});
}
void Memory(vk::CommandBuffer cmd){vk::MemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite};cmd.pipelineBarrier2({.memoryBarrierCount=1,.pMemoryBarriers=&b});}
}
NrDimensionAdapter::NrDimensionAdapter(const Instance&i):device(i.GetDevice()),instance(i),width(BbNRDimensions::Width()),height(BbNRDimensions::Height()){
 const auto dp=i.GetPhysicalDevice().getFormatProperties(vk::Format::eD32SfloatS8Uint).optimalTilingFeatures;
 if((dp&(vk::FormatFeatureFlagBits::eBlitSrc|vk::FormatFeatureFlagBits::eBlitDst))!=(vk::FormatFeatureFlagBits::eBlitSrc|vk::FormatFeatureFlagBits::eBlitDst))throw std::runtime_error("NR dimension adapter requires supported D32S8 nearest blits");
 for(unsigned k=0;k<3;++k){images[k]=VideoCore::HostImage(device,i.GetAllocator());auto& ci=infos[k];ci={.imageType=vk::ImageType::e2D,.format=k==0?vk::Format::eR16G16B16A16Sfloat:k==1?vk::Format::eD32SfloatS8Uint:vk::Format::eR16G16Sfloat,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst,.initialLayout=vk::ImageLayout::eUndefined};
  if(k==1)ci.usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eDepthStencilAttachment|vk::ImageUsageFlagBits::eTransferSrc|vk::ImageUsageFlagBits::eTransferDst;
  images[k].Create(ci);views[k]=device.createImageViewUnique({.image=vk::Image(images[k]),.viewType=vk::ImageViewType::e2D,.format=ci.format,.subresourceRange={k==1?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;
 }
 const auto sampler=[&](vk::Filter f){return device.createSamplerUnique({.magFilter=f,.minFilter=f,.mipmapMode=vk::SamplerMipmapMode::eNearest,.addressModeU=vk::SamplerAddressMode::eClampToEdge,.addressModeV=vk::SamplerAddressMode::eClampToEdge,.addressModeW=vk::SamplerAddressMode::eClampToEdge,.maxLod=0}).value;};nearest=sampler(vk::Filter::eNearest);linear=sampler(vk::Filter::eLinear);
 guard=std::make_unique<VideoCore::Buffer>(i,0,4,VideoCore::MemoryType::DeviceLocal,"NR dimension residual wholeframe guard");
 const std::array<vk::DescriptorSetLayoutBinding,2> mb{{{0,vk::DescriptorType::eCombinedImageSampler,1,vk::ShaderStageFlagBits::eCompute},{1,vk::DescriptorType::eStorageImage,1,vk::ShaderStageFlagBits::eCompute}}};
 const std::array<vk::DescriptorSetLayoutBinding,4> ab{{{0,vk::DescriptorType::eCombinedImageSampler,1,vk::ShaderStageFlagBits::eCompute},{1,vk::DescriptorType::eCombinedImageSampler,1,vk::ShaderStageFlagBits::eCompute},{2,vk::DescriptorType::eStorageImage,1,vk::ShaderStageFlagBits::eCompute},{3,vk::DescriptorType::eStorageBuffer,1,vk::ShaderStageFlagBits::eCompute}}};
 motion_set=device.createDescriptorSetLayoutUnique({.flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,.bindingCount=2,.pBindings=mb.data()}).value;apply_set=device.createDescriptorSetLayoutUnique({.flags=vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,.bindingCount=4,.pBindings=ab.data()}).value;
 const vk::PushConstantRange mr{vk::ShaderStageFlagBits::eCompute,0,8},ar{vk::ShaderStageFlagBits::eCompute,0,12};
 motion_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*motion_set,.pushConstantRangeCount=1,.pPushConstantRanges=&mr}).value;apply_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*apply_set,.pushConstantRangeCount=1,.pPushConstantRanges=&ar}).value;
 const auto pipeline=[&](std::span<const u32> code,vk::PipelineLayout layout){auto module=CompileSPV(code,device);auto result=device.createComputePipelineUnique({}, {.stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=module,.pName="main"},.layout=layout}).value;device.destroyShaderModule(module);return result;};motion_pipeline=pipeline(NR_DIMENSIONS_MOTION_COMP,*motion_layout);apply_pipeline=pipeline(NR_DIMENSIONS_APPLY_COMP,*apply_layout);
}
std::pair<vk::Image,vk::ImageView> NrDimensionAdapter::RenderMotion(Scheduler& scheduler,unsigned rw,unsigned rh){
 if(!render_motion||render_motion.image_ci.extent.width!=rw||render_motion.image_ci.extent.height!=rh){
  // The old allocation is no longer retained after a real Native completion.
  scheduler.Finish();render_motion_view.reset();render_motion=VideoCore::HostImage(device,instance.GetAllocator());
  render_motion.Create({.imageType=vk::ImageType::e2D,.format=vk::Format::eR16G16Sfloat,.extent={rw,rh,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,.tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferSrc,.initialLayout=vk::ImageLayout::eUndefined});
  render_motion_view=device.createImageViewUnique({.image=vk::Image(render_motion),.viewType=vk::ImageViewType::e2D,.format=vk::Format::eR16G16Sfloat,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;
 }
 auto cmd=scheduler.CommandBuffer();vk::ImageMemoryBarrier2 b{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eComputeShader,.dstAccessMask=vk::AccessFlagBits2::eShaderWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=vk::Image(render_motion),.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&b});
 return {vk::Image(render_motion),*render_motion_view};
}
std::array<vk::Image,3> NrDimensionAdapter::Prepare(Scheduler&s,const std::array<vk::Image,3>& source,const std::array<vk::ImageCreateInfo,3>& ci,unsigned rw,unsigned rh){
 if(!rw||!rh)throw std::invalid_argument("NR source render subrect empty");
 auto cmd=s.CommandBuffer();std::array<vk::Image,3> result{};
 for(unsigned k=0;k<3;++k){if(!source[k]||ci[k].format!=infos[k].format||ci[k].extent.width!=rw||ci[k].extent.height!=rh)throw std::invalid_argument("NR actual source render subrect/format mismatch");result[k]=vk::Image(images[k]);
  vk::ImageMemoryBarrier2 init{.srcStageMask=vk::PipelineStageFlagBits2::eAllCommands,.srcAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.dstStageMask=vk::PipelineStageFlagBits2::eAllCommands,.dstAccessMask=vk::AccessFlagBits2::eMemoryRead|vk::AccessFlagBits2::eMemoryWrite,.oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eGeneral,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=result[k],.subresourceRange={k==1?vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil:vk::ImageAspectFlagBits::eColor,0,1,0,1}};cmd.pipelineBarrier2({.imageMemoryBarrierCount=1,.pImageMemoryBarriers=&init});
  if(k<2){const auto aspect=k==1?vk::ImageAspectFlagBits::eDepth:vk::ImageAspectFlagBits::eColor;Barrier(cmd,source[k],aspect,vk::AccessFlagBits2::eTransferRead);vk::ImageBlit region{.srcSubresource={aspect,0,0,1},.srcOffsets=std::array{vk::Offset3D{0,0,0},vk::Offset3D{int(rw),int(rh),1}},.dstSubresource={aspect,0,0,1},.dstOffsets=std::array{vk::Offset3D{0,0,0},vk::Offset3D{int(width),int(height),1}}};cmd.blitImage(source[k],vk::ImageLayout::eGeneral,result[k],vk::ImageLayout::eGeneral,region,k==1?vk::Filter::eNearest:vk::Filter::eLinear);Barrier(cmd,result[k],aspect,vk::AccessFlagBits2::eShaderRead);}
 }
 auto mv=device.createImageViewUnique({.image=source[2],.viewType=vk::ImageViewType::e2D,.format=ci[2].format,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}}).value;
 // The descriptor view must outlive the recorded dispatch. Retire it at this queue's real fence.
 auto hold=std::make_shared<vk::UniqueImageView>(std::move(mv));s.DeferPriorityOperation([hold]{});
 Barrier(cmd,source[2],vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits2::eShaderRead);
 const std::array<vk::DescriptorImageInfo,2> info{{{*nearest,**hold,vk::ImageLayout::eGeneral},{{},*views[2],vk::ImageLayout::eGeneral}}};std::array<vk::WriteDescriptorSet,2> writes{};
 for(unsigned k=0;k<2;++k)writes[k]={.dstBinding=k,.descriptorCount=1,.descriptorType=k?vk::DescriptorType::eStorageImage:vk::DescriptorType::eCombinedImageSampler,.pImageInfo=&info[k]};cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*motion_pipeline);cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*motion_layout,0,writes);std::array<unsigned,2> size{rw,rh};cmd.pushConstants(*motion_layout,vk::ShaderStageFlagBits::eCompute,0,8,size.data());cmd.dispatch((width+7)/8,(height+7)/8,1);Memory(cmd);return result;
}
void NrDimensionAdapter::Apply(Scheduler&s,vk::ImageView edited,vk::Image target,vk::ImageView target_view,unsigned rw,unsigned rh){
 auto cmd=s.CommandBuffer();Barrier(cmd,target,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eShaderWrite);Memory(cmd);cmd.fillBuffer(guard->Handle(),0,4,0);Memory(cmd);
 const std::array<vk::DescriptorImageInfo,3> info{{{*linear,edited,vk::ImageLayout::eGeneral},{*linear,*views[0],vk::ImageLayout::eGeneral},{{},target_view,vk::ImageLayout::eGeneral}}};const vk::DescriptorBufferInfo bi{guard->Handle(),0,4};std::array<vk::WriteDescriptorSet,4> writes{};
 for(unsigned k=0;k<4;++k)writes[k]={.dstBinding=k,.descriptorCount=1,.descriptorType=k<2?vk::DescriptorType::eCombinedImageSampler:k==2?vk::DescriptorType::eStorageImage:vk::DescriptorType::eStorageBuffer,.pImageInfo=k<3?&info[k]:nullptr,.pBufferInfo=k==3?&bi:nullptr};cmd.bindPipeline(vk::PipelineBindPoint::eCompute,*apply_pipeline);cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,*apply_layout,0,writes);
 std::array<unsigned,3> p{rw,rh,0};for(unsigned pass=0;pass<2;++pass){p[2]=pass;cmd.pushConstants(*apply_layout,vk::ShaderStageFlagBits::eCompute,0,12,p.data());cmd.dispatch((rw+7)/8,(rh+7)/8,1);Memory(cmd);}
 Barrier(cmd,target,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits2::eShaderRead|vk::AccessFlagBits2::eTransferRead);
}
uint64_t NrDimensionAdapter::ImageBytes()const{return uint64_t(width)*height*(8+8+4);}
}
