// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_state.h"
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
  std::array<vk::Image,4> images{};
  vk::Buffer state{},guard{};
  unsigned active_bank=0,candidate_bank=2;
  uint64_t candidate_origin=0,candidate_current=0,candidate_count=0;
  bool host_history=false,candidate_recorded=false;
 };
 explicit NativePersistentHistoryDiagnostic(const Instance&);
 ~NativePersistentHistoryDiagnostic();
 static bool Enabled() noexcept;
 bool Record(Scheduler&,const Snapshot&);
 void ObserveCompletion(Scheduler&);
 bool Ready() const noexcept;
 void HoldUnknown() noexcept;
 void ReportRetirement(const char*) const noexcept;
 uint64_t StagingBytes() const noexcept;
private:
 struct Impl;std::unique_ptr<Impl> impl;
};
}
