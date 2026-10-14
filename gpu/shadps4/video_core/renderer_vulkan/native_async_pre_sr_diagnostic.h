// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_pre_sr_diagnostic_policy.h"
#include "native_async_pre_sr_origin_gpu.h"
#include "native_async_pre_sr_sky.h"
#include "video_core/buffer_cache/buffer.h"
#include <thread>
namespace Vulkan {
class Instance;class Scheduler;
// Independent actual97/720 staging; old NativeSkyDiagnostic1080 remains unchanged.
class NativeAsyncPreSrDiagnostic:public std::enable_shared_from_this<NativeAsyncPreSrDiagnostic>{
public:
 struct Context {
  BbNativeAsyncPreSr::DiagnosticAdmission admission;
  NativeAsyncPreSrOriginIdentity job;
  std::array<BbNativeAsync::Frame,5> lineage{};unsigned lineage_count=0;
  bool input_domain_changed=false,applied_sr_reset=false;
  bool mask_diagnostic=false;
  NativeAsyncPreSrSky::ColdCurrent mask_current;
  std::array<vk::Buffer,2> masks{}; // actual Prev then Next, GPU-written only

 };
 explicit NativeAsyncPreSrDiagnostic(const Instance&);
 ~NativeAsyncPreSrDiagnostic();
 bool RecordOnce(Scheduler&,vk::Image original,vk::Image actual_SR_input,
  const std::array<vk::ImageCreateInfo,2>&,vk::Buffer actual_guard,const Context&,const BbNativeAsync::Chain&);
 void MergeRecorded(Scheduler&,uint64_t source);
 bool RetireFinished(); // GPU proof + writer completion, never a GPU/model wait
 bool SafeRetired()const noexcept{return retired;}
 bool Recorded()const noexcept{return recorded;}
private:
 void Run(std::stop_token);
 std::array<std::unique_ptr<VideoCore::Buffer>,2> images;
 std::unique_ptr<VideoCore::Buffer> guard;
 std::array<std::unique_ptr<VideoCore::Buffer>,2> mask_staging;
 std::array<std::unique_ptr<VideoCore::Buffer>,3> mask_images;

 Context context;std::array<vk::ImageCreateInfo,2> actual_ci{};
 std::array<uint64_t,2> actual_handles{};
 std::string run,session;uint64_t copy_tick=0,merge_tick=0,timeline=0,scheduler_id=0;
 bool recorded=false,merge_registered=false,retired=false;
 std::atomic<uint64_t> copy_observed{0},merge_observed{0};
 std::atomic<bool> writer_finished{false};std::jthread writer;
};
}
