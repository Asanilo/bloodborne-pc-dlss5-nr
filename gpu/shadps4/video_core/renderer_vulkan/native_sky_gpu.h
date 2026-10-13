// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
#include "native_sky_constants.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"
#include <memory>
namespace Vulkan {
class Instance; class Scheduler;
// Renderer-thread-only recording. No queue submit, wait, file access, readback or model.
// Noop anchor is current unedited SR itself, NOT an NR inference or quality claim.
class NativeSkyGpu {
public:
 explicit NativeSkyGpu(const Instance&);
 vk::ImageView Record(Scheduler&,unsigned index,const std::array<vk::Image,3>&,
                      bool capture);
 std::array<vk::Image,3> ConvertedOrigin()const noexcept;
 std::array<vk::Image,3> Converted(unsigned i)const noexcept{return {vk::Image(packets[i].converted[0].image),vk::Image(packets[i].converted[1].image),vk::Image(packets[i].converted[2].image)};}
 vk::Image Output(unsigned i)const noexcept{return vk::Image(packets[i].output.image);}
 // Resolve only an actual current packet's selected view. Replay and regular
 // outputs are different images; a flag or the origin packet is not sufficient.
 vk::Image SelectedOutput(unsigned i,vk::ImageView selected,bool& replay)const noexcept {
  if(i>=packets.size()||!selected)return {};
  if(selected==packets[i].output.View()){replay=false;return vk::Image(packets[i].output.image);}
  if(selected==replay_packets[i].output.View()){replay=true;return vk::Image(replay_packets[i].output.image);}
  return {};
 }
 vk::ImageView RecordSparseCatchup(Scheduler&,unsigned current_index,vk::ImageView actual_origin_NR);
 void CompleteSparseNativeUse()noexcept {replay_busy.store(false,std::memory_order_release);}
 bool SparseScratchBusy()const noexcept {return replay_busy.load(std::memory_order_acquire);}
 void Restart() noexcept {next_index=0;}
 uint64_t ImageBytes()const noexcept;
private:
 struct Image {VideoCore::UniqueImage image;vk::UniqueImageView view;vk::ImageView borrowed{};
 vk::ImageView View()const noexcept{return view?*view:borrowed;} };
 struct Bank {Image edit,guide,l1guide;std::array<Image,3> levels;};
 struct Packet {
  std::array<Image,3> converted;Image output;
  std::array<vk::UniqueImageView,3> source_views;
  std::array<std::unique_ptr<VideoCore::Buffer>,4> constants;
  std::array<vk::DescriptorSet,4> sets{};vk::DescriptorSet conversion{};
 };
 Image Make(vk::Format,unsigned,unsigned);
 void InitImages(vk::CommandBuffer);
 void Barrier(vk::CommandBuffer);
 void Pass(vk::CommandBuffer,Packet&,unsigned,const DlssNrCacheConstants&,bool,
           const std::array<Image*,12>&,const std::array<Image*,7>&);
 DlssNrCacheConstants Base(unsigned)const;
 vk::Device device;VmaAllocator allocator;
 std::array<Bank,2> banks;
 std::array<Packet,BbNativeAsync::NodeCount> packets,replay_packets;
 std::atomic<bool> replay_busy{false};
 Image dummy,exposure,stats;
 vk::UniqueSampler linear,nearest;
 vk::UniqueDescriptorSetLayout layout,conversion_layout;
 vk::UniquePipelineLayout pipeline_layout,conversion_pipeline_layout;
 vk::UniquePipeline pipeline,conversion_pipeline;
 vk::UniqueDescriptorPool pool;
 bool initialized=false;unsigned next_index=0;
};
}
