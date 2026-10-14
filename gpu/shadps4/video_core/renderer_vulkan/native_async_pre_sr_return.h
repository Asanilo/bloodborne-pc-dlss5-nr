// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include "native_pre_sr_sky_policy.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"
namespace Vulkan {class Instance;class Scheduler;
// Renderer-only: whole-frame GPU finite/range flag, then original-alpha half conversion.
// No CPU flag readback/wait. Hot selected-cache means eligible binding, not pixel proof.
class NativeAsyncPreSrReturn {
public:
 explicit NativeAsyncPreSrReturn(const Instance&);
 vk::Image Record(Scheduler&,unsigned,vk::ImageView composite32,vk::ImageView raw97,bool wholeEligible=false,bool currentContextWritten=false);
 struct MaskUse {std::atomic<uint64_t> observed{0};uint64_t last_use=0;bool unknown=false;};
 bool BeginMaskCurrent(Scheduler&,uint64_t actual_source);
 bool MaskEnabled()const noexcept{return BbNativePreSrSkyPolicy::Enabled();}
 vk::Buffer PreviousMask()const noexcept{return masks[1-next_mask]->Handle();}
 vk::Buffer NextMask()const noexcept{return masks[next_mask]->Handle();}
 unsigned NextMaskIndex()const noexcept{return next_mask;}
 std::shared_ptr<MaskUse> MaskProof()const noexcept{return mask_use;}
 bool MasksReady()const noexcept{return !MaskEnabled()||(!mask_use->unknown&&(!mask_use->last_use||mask_use->observed.load(std::memory_order_acquire)>=mask_use->last_use));}
 void MarkMaskUse(Scheduler&);
 void HoldMasksUnknown()noexcept{mask_use->unknown=true;}
 vk::ImageView View(unsigned i)const noexcept{return *views[i];}
 const vk::ImageCreateInfo& Info(unsigned i)const noexcept{return images[i].image_ci;}
 vk::Buffer Guard(unsigned i)const noexcept{return flags[i]->Handle();}
 static constexpr uint64_t PackedBytes=uint64_t(1280)*720*8*5;
private:
 vk::Device device;std::array<VideoCore::UniqueImage,5> images;
 std::array<vk::UniqueImageView,5> views;
 std::array<std::unique_ptr<VideoCore::Buffer>,5> flags;
 std::array<vk::DescriptorSet,5> sets{};
 vk::UniqueSampler nearest;vk::UniqueDescriptorSetLayout layout;
 vk::UniquePipelineLayout pipeline_layout;vk::UniquePipeline pipeline;
 std::array<std::unique_ptr<VideoCore::Buffer>,2> masks;
 std::shared_ptr<MaskUse> mask_use=std::make_shared<MaskUse>();
 unsigned next_mask=0;uint64_t mask_source=0;bool masks_initialized=false;
 vk::UniqueDescriptorPool pool;
};}
