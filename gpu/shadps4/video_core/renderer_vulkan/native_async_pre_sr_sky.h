// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
#include "native_sky_constants.h"
#include "native_pre_sr_sky_policy.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/texture_cache/image.h"
#include <memory>
namespace Vulkan {
class Instance; class Scheduler;
// Renderer-thread-only recording. No queue submit, wait, file access, readback or model.
// Noop anchor is current unedited SR itself, NOT an NR inference or quality claim.
class NativeAsyncPreSrSky {
public:
 explicit NativeAsyncPreSrSky(const Instance&);
 vk::ImageView Record(Scheduler&,unsigned index,const std::array<vk::Image,3>&,
                      bool capture,const BbNativeAsync::Frame&,bool actual_cache_eligible=false);
 void SetCurrentMasks(vk::Buffer previous,unsigned next)noexcept{previous_mask=previous;shown_next=next;}
 bool Masked()const noexcept{return BbNativePreSrSkyPolicy::Enabled();}
 void PrepareShownCurrent(uint64_t source,double real_dt);
 void ActualSrResult(uint64_t source,bool success,Scheduler&);
 vk::ImageView FinalizeMaskedCurrent(Scheduler&,unsigned,vk::ImageView selected,const BbNativeAsync::Frame& origin,const BbNativeAsync::Frame& current);
 bool CurrentWholeEligible()const noexcept{return current_whole;}
 bool CurrentContextWritten()const noexcept{return current_context;}
 // Read-only exact-current handles, valid only for recording cold copies in the
 // same renderer command stream. Not GPU-ready/valid proof or a reusable lease.
 struct ColdCurrent {
  BbNativeAsync::Frame current,origin,previous;unsigned next_slot=0,bank=0;uint64_t oldest_contributing_origin=0;
  bool whole=false,context_written=false;
  std::array<vk::Image,3> images{};
  std::array<vk::ImageCreateInfo,3> ci{};
 };
 ColdCurrent DiagnosticCurrent(unsigned,const BbNativeAsync::Frame& current,const BbNativeAsync::Frame& origin)const;

 std::array<vk::Image,3> ConvertedOrigin()const noexcept;
 std::array<vk::Image,3> Converted(unsigned i)const noexcept{return {vk::Image(packets[i].converted[0].image),vk::Image(packets[i].converted[1].image),vk::Image(packets[i].converted[2].image)};}
 vk::ImageView RawColorView(unsigned i)const noexcept{return *packets[i].source_views[0];}
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
  std::array<Image,3> converted;Image output;BbNativeAsync::Frame frame;
  std::array<vk::UniqueImageView,3> source_views;
  std::array<std::unique_ptr<VideoCore::Buffer>,6> constants;
  std::array<vk::DescriptorSet,6> sets{};vk::DescriptorSet conversion{};
 };
 Image Make(vk::Format,unsigned,unsigned);
 void InitImages(vk::CommandBuffer);
 void Barrier(vk::CommandBuffer);
 void Pass(vk::CommandBuffer,Packet&,unsigned,const DlssNrCacheConstants&,bool,
           const std::array<Image*,12>&,const std::array<Image*,7>&,Image* context_now=nullptr,Image* context_prev=nullptr);
 void CopyCurrentGuide(vk::CommandBuffer,unsigned bank,uint64_t source);
 DlssNrCacheConstants Base(unsigned)const;
 vk::Device device;VmaAllocator allocator;
 std::array<Bank,2> banks;
 std::array<Packet,BbNativeAsync::NodeCount> packets,replay_packets;
 std::atomic<bool> replay_busy{false};
 Image dummy,exposure,stats;
 Image raw_edit;std::array<Image,2> shown_edit,shown_guide,shown_context;
 vk::Buffer previous_mask{};unsigned shown_next=0;
 struct ShownMetadata {bool eligible=false,context=false,SR_success=false;BbNativeAsync::Frame frame;uint64_t oldest_origin=0,last_use=0;std::shared_ptr<std::atomic<uint64_t>> observed=std::make_shared<std::atomic<uint64_t>>(0);};
 std::array<ShownMetadata,2> shown_metadata;
 uint64_t prepared_source=0,last_apply_source=0,last_guide_source=0;unsigned last_guide_slot=0,last_guide_bank=0;bool current_whole=false,current_context=false,last_apply_replay=false;
 BbNativePreSrSkyPolicy::FrameClock frame_clock;float current_antipop_step=0;


 vk::UniqueSampler linear,nearest;
 vk::UniqueDescriptorSetLayout layout,conversion_layout;
 vk::UniquePipelineLayout pipeline_layout,conversion_pipeline_layout;
 vk::UniquePipeline pipeline,conversion_pipeline;
 vk::UniqueDescriptorPool pool;
 bool initialized=false;unsigned next_index=0;
};
}
