// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"
namespace Vulkan {class Instance;class Scheduler;
// Renderer-only: whole-frame GPU finite/range flag, then original-alpha half conversion.
// No CPU flag readback/wait. Hot selected-cache means eligible binding, not pixel proof.
class NativeAsyncPreSrReturn {
public:
 explicit NativeAsyncPreSrReturn(const Instance&);
 vk::Image Record(Scheduler&,unsigned,vk::ImageView composite32,vk::ImageView raw97);
 vk::ImageView View(unsigned i)const noexcept{return *views[i];}
 static constexpr uint64_t PackedBytes=uint64_t(1280)*720*8*5;
private:
 vk::Device device;std::array<VideoCore::UniqueImage,5> images;
 std::array<vk::UniqueImageView,5> views;
 std::array<std::unique_ptr<VideoCore::Buffer>,5> flags;
 std::array<vk::DescriptorSet,5> sets{};
 vk::UniqueSampler nearest;vk::UniqueDescriptorSetLayout layout;
 vk::UniquePipelineLayout pipeline_layout;vk::UniquePipeline pipeline;
 vk::UniqueDescriptorPool pool;
};}
