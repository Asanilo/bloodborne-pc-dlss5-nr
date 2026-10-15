// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
#include "video_core/texture_cache/image.h"
#include <memory>
#include <span>
namespace Vulkan {
class Instance; class Scheduler;
// Renderer-thread only. No queue submission, waits, IPC, model or CPU pixel readback.
// Recorded visual metadata never proves GPU validity or completion.
class NativePersistentHistory {
public:
 struct Link { BbNativeAsync::Frame frame; std::array<vk::Image,3> converted; };
 explicit NativePersistentHistory(const Instance&);
 ~NativePersistentHistory();
 static bool Enabled() noexcept;
 // False means raw fallback. No descriptor/constant/source view is rewritten
 // until the SAME scheduler timeline has completed this slot's actual last use.
 bool BeginCurrent(Scheduler&,const std::array<vk::Image,3>& raw,
                   const BbNativeAsync::Frame&,double real_dt);
 // Caller must validate exact immutable NR tuple, origin and <=4 link admission.
 // This class independently validates the full consecutive retained lineage.
 bool RecordCandidate(Scheduler&,std::span<const Link>,vk::ImageView actual_NR);
 // Records whole-frame finite guard and raw-alpha half conversion through Return.
 vk::ImageView Apply(Scheduler&);
 vk::Image Image() const noexcept;
 const vk::ImageCreateInfo& Info() const noexcept;
 bool PreparedFor(uint64_t source) const noexcept;
 void MarkUse(Scheduler&,uint64_t source);
 bool Ready() const noexcept;
 void HoldUnknown() noexcept;
 bool HasHistory() const noexcept;
 uint64_t ImageBytes() const noexcept;
 // Logical invalidation does not release physical leases or reset last-use ticks.
 void Reset() noexcept;
private:
 struct Impl; std::unique_ptr<Impl> impl;
};
}
