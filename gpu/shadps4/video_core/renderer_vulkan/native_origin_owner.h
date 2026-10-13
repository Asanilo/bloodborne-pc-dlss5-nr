// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_async_snapshot.h"
#include <mutex>
#include <thread>
namespace Vulkan {
class NativeOriginOwner {
public:
 enum class Phase {Disabled,Bootstrap,Ready,Reserved,InputRecorded,WaitingComplete,Complete,
                    FinalRecorded,Finalized,StopRequested,AwaitNativeRelease,Retired,Failed};
 struct Job {
  NativeOriginIdentity id;BbNativeAsync::Frame origin;NativeOriginResources resources;
  std::shared_ptr<NativeOriginGpuProof> input=std::make_shared<NativeOriginGpuProof>();
  std::shared_ptr<NativeOriginGpuProof> final=std::make_shared<NativeOriginGpuProof>();
  NativeAsyncSnapshot::SparseCompletion complete;
  NativeAsyncSnapshot::SparseDisposition disposition=NativeAsyncSnapshot::SparseDisposition::Unavailable;
  std::atomic<bool> consumed_published{false};
 };
 NativeOriginOwner()=default;~NativeOriginOwner();
 bool Start(const Instance&); // one managed owner; never records Scheduler commands
 bool Enabled()const noexcept;
 Phase State()const noexcept{return phase.load(std::memory_order_acquire);}
 std::shared_ptr<Job> TryReserve(const BbNativeAsync::Frame&); // bounded try_lock
 std::shared_ptr<Job> TryJob();
 void CancelReservation(const std::shared_ptr<Job>&);
 void InputRecorded(const std::shared_ptr<Job>&);
 void FinalRecorded(const std::shared_ptr<Job>&,NativeAsyncSnapshot::SparseDisposition);
 void FinalizedAck(const std::shared_ptr<Job>&);
 void RequestStop(unsigned reason)noexcept; // 1off/2exit/3profile/4budget, memory only
 std::shared_ptr<NativeOriginQuiescence> Quiescence()const{return quiescence;}
 bool ReapedNormally()const noexcept{return normal_reap.load(std::memory_order_acquire);}
private:
 void Run(std::stop_token,const Instance&);
 void Fail(const std::string&)noexcept;
 std::atomic<Phase> phase{Phase::Disabled};std::atomic<unsigned> stop_reason{0};
 std::atomic<bool> started{false},normal_reap{false};
 std::mutex mutex;std::jthread worker;std::shared_ptr<Job> job;
 std::shared_ptr<NativeOriginQuiescence> quiescence=std::make_shared<NativeOriginQuiescence>();
 NativeOriginResources registry;std::string session,mode,run,helper,wine,preloader,prefix;
 std::atomic<uint64_t> finalized{0};
 // Under mutex: a recorded INPUT cannot be issued again for the same origin,
 // even when completed CONSUMED leaves records[0] alive. Cancel before GPU input
 // leaves this unchanged; no automatic retry after an issued/failed job.
 uint64_t last_recorded_origin=0;
};
}
