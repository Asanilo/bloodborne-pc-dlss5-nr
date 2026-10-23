// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "buffers/buffer.h"
#include "textures/image.h"
#include <array>
#include <memory>
namespace Vulkan {
class Instance;class Scheduler;
// One bounded execution domain, immutable for the Helper session. Render guides
// are resampled on the Native queue; this class never submits/waits or owns an FD.
class NrDimensionAdapter {
public:
 explicit NrDimensionAdapter(const Instance&);
 std::array<vk::Image,3> Prepare(Scheduler&,const std::array<vk::Image,3>&,
                 const std::array<vk::ImageCreateInfo,3>&,unsigned,unsigned);
 const std::array<vk::ImageCreateInfo,3>& Infos()const{return infos;}
 void Apply(Scheduler&,vk::ImageView edited,vk::Image target,vk::ImageView target_view,unsigned,unsigned);
 vk::Image Motion()const{return vk::Image(images[2]);}
 vk::ImageView MotionView()const{return *views[2];}
 uint64_t ImageBytes()const;
private:
 vk::Device device;unsigned width,height;
 std::array<VideoCore::HostImage,3> images;
 std::array<vk::UniqueImageView,3> views;
 std::array<vk::ImageCreateInfo,3> infos;
 std::unique_ptr<VideoCore::Buffer> guard;
 vk::UniqueSampler nearest,linear;
 vk::UniqueDescriptorSetLayout motion_set,apply_set;
 vk::UniquePipelineLayout motion_layout,apply_layout;
 vk::UniquePipeline motion_pipeline,apply_pipeline;
};
}
