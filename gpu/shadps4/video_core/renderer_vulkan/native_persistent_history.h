// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
#include "nr_runtime_control.h"
#include "video_core/texture_cache/image.h"
#include <memory>
#include <optional>
#include <span>
#include <string>
namespace Vulkan {
class Instance; class Scheduler;
// Renderer-thread only. No queue submission, waits, IPC, model or CPU pixel readback.
// Recorded visual metadata never proves GPU validity or completion.
class NativePersistentHistory {
public:
 struct CandidateEvidence {
  std::string session,resource_set;
  uint64_t generation=0,job=0,helper_serial=0,native_input_tick=0;
 };
 struct Link { BbNativeAsync::Frame frame; std::array<vk::Image,3> converted; };
 explicit NativePersistentHistory(const Instance&);
 ~NativePersistentHistory();
 static bool Enabled() noexcept;
 bool SetControls(const BbNR::Request&); // staged; committed at a real record boundary
 BbNR::Request AppliedControls() const;
 struct RuntimeSample {
  bool available=false,timing_available=false;uint64_t source=0,revision=0,target_tick=0,observed_tick=0,monotonic_ns=0;
  float valid_fraction=0,changed_fraction=0,mean_age_frames=0,mean_age_ms=0,max_age_frames=0,max_age_ms=0;
  std::array<double,6> pass_ms{}; // mode-indexed GPU dispatch timestamps, sums for repeated modes
 };
 RuntimeSample LatestRuntimeSample() const;
 struct CaptureStatus {uint64_t seq=0,first_source=0,last_source=0,revision=0;unsigned recorded_frames=0,last_index=0;bool busy=false,complete=false,failed=false;std::string directory;};
 CaptureStatus Capture() const;
 bool CaptureFor(uint64_t source) const noexcept;
 struct CaptureFrame {uint64_t seq=0,source=0,revision=0;unsigned index=0;std::string directory;};
 std::optional<CaptureFrame> CaptureFrameForSource(uint64_t source) const;
 // False means raw fallback. No descriptor/constant/source view is rewritten
 // until the SAME scheduler timeline has completed this slot's actual last use.
 bool BeginCurrent(Scheduler&,const std::array<vk::Image,3>& raw,
                   const BbNativeAsync::Frame&,double real_dt);
 // Caller must validate exact immutable NR tuple, origin and <=4 link admission.
 // This class independently validates the full consecutive retained lineage.
 bool RecordCandidate(Scheduler&,std::span<const Link>,vk::ImageView actual_NR,
                      vk::Image actual_NR_image={},const CandidateEvidence* evidence=nullptr);
 // Records whole-frame finite guard and raw-alpha half conversion through Return.
 vk::ImageView Apply(Scheduler&);
 vk::Image Image() const noexcept;
 const vk::ImageCreateInfo& Info() const noexcept;
 bool PreparedFor(uint64_t source) const noexcept;
 void MarkUse(Scheduler&,uint64_t source);
 bool Ready() const noexcept;
 // Refresh only this scheduler's real completed targets; never wait or submit.
 void ObserveCompletion(Scheduler&);
 // Explicit stop/retirement diagnostics; no wait, poll, or invented completion.
 void ReportRetirement(const char* stage) const noexcept;
 void HoldUnknown() noexcept;
 bool HasHistory() const noexcept;
 uint64_t ImageBytes() const noexcept;
 // Logical invalidation does not release physical leases or reset last-use ticks.
 void Reset() noexcept;
private:
 struct Impl; std::unique_ptr<Impl> impl;
};
}
