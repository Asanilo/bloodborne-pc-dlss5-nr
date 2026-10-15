// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
#include "native_persistent_history.h"
#include "native_persistent_history_shader.h"
#include "vk_common.h"
#include <memory>
namespace Vulkan {
class Instance; class Scheduler;
// Opt-in cold image readback. Physical completion, writer completion and unknown
// state remain separate; this class never waits/submits on the renderer thread.
class NativePersistentHistoryDiagnostic {
public:
 struct Snapshot {
  BbNativeAsync::Frame current;
  // Exact original raw97, final guarded half97, active edit97, active meta109.
  std::array<vk::Image,6> images{};
  vk::Buffer state{},guard{};
  unsigned active_bank=0,candidate_bank=2;
  uint64_t candidate_origin=0,candidate_current=0,candidate_count=0;
  bool host_history=false,candidate_recorded=false;
  uint64_t control_revision=0,capture_seq=0;BbNR::Config controls;
 };
 struct CandidateSnapshot {
  NativePersistentHistory::CandidateEvidence identity;
  BbNativeAsync::Frame origin,current;
  // Immutable origin32, acquired actual Resolve720 NR32, origin depth32.
  std::array<vk::Image,3> images{};
  PersistentHistoryConstants capture_parameters;
  unsigned bank=2;uint64_t count=0;
 };
 explicit NativePersistentHistoryDiagnostic(const Instance&,uint64_t capture_seq=0,unsigned frame_count=0);
 ~NativePersistentHistoryDiagnostic();
 static bool Enabled() noexcept;
 bool Record(Scheduler&,const Snapshot&);
 bool RecordCandidate(Scheduler&,const CandidateSnapshot&);
 void ObserveCompletion(Scheduler&);
 bool Ready() const noexcept;
 bool Finished() const noexcept;
 bool Succeeded() const noexcept;
 std::string Directory() const;
 uint64_t LastSource() const noexcept;
 void HoldUnknown() noexcept;
 void ReportRetirement(const char*) const noexcept;
 uint64_t StagingBytes() const noexcept;
private:
 struct Impl;std::unique_ptr<Impl> impl;
};
}
