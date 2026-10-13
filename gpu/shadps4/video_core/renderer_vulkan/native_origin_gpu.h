// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include <memory>
#include <string>
namespace Vulkan {
class Scheduler;
struct NativeOriginIdentity {
 std::string session,resource_set;
 uint64_t generation=0,job=0,origin_source=0,origin_epoch=0,origin_history=0,params_version=1;
 bool Matches(const BbNativeAsync::Frame&)const noexcept;
};
// Borrowed API handles; owner must have performed the canonical UUID/PID/FD/CI
// import audit. Shared ownership is lifetime ONLY, not release/fence authority.
struct NativeOriginResources {
 std::string session,resource_set;
 std::array<vk::Image,4> images{};
 std::array<vk::ImageView,4> views{};
 std::array<vk::ImageCreateInfo,4> infos{};
 std::shared_ptr<void> import_owner;
 bool actual_import_fd_retired=false;
 uint32_t native_queue_family=VK_QUEUE_FAMILY_IGNORED;
 bool Valid(const NativeOriginIdentity&)const noexcept;
};
struct NativeOriginQuiescence {
 std::atomic<uint64_t> last_use_tick{0},observed_tick{0};
 std::atomic<bool> stopped_and_destroyed{false};
};
struct NativeOriginGpuProof {
 uint64_t native_target_tick=0;
 std::atomic<bool> native_GPU_complete{false};
};
// Renderer commands ONLY. No file IO, queue submission, CPU waiting or external
// semaphore machinery. Native serial and Helper serial have separate domains.
void RecordOriginInput(Scheduler&,const NativeOriginResources&,
                       const NativeOriginIdentity&,const std::array<vk::Image,3>& converted,
                       const std::shared_ptr<NativeOriginGpuProof>&);
void AcquireOriginOutput(Scheduler&,const NativeOriginResources&,const NativeOriginIdentity&);
void ReleaseOriginOutput(Scheduler&,const NativeOriginResources&,const NativeOriginIdentity&,
                         const std::shared_ptr<NativeOriginGpuProof>&);
}
