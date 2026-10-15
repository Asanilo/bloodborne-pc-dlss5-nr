// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_owner.h"
#include "nr_runtime_control.h"
#include "native_async_pre_sr_policy.h"
#include "native_async_pre_sr_return.h"
#include "native_async_pre_sr_fixed_input.h"
#include "native_async_pre_sr_sr_policy.h"
#include "native_async_pre_sr_diagnostic.h"
#include "native_origin_lease.h"
#include "native_persistent_history.h"
#include "native_visual_timeline.h"
#include "native_async_pre_sr_sky.h"
#include "native_async_pre_sr_origin_gpu.h"
#include "native_async_pre_sr_frontend.h"
#include "native_sky_diagnostic.h"
#include "common/types.h"
#include "video_core/texture_cache/image.h"
#include "video_core/renderer_vulkan/vk_common.h"
namespace Vulkan {
class Instance;class Scheduler;class NativeAsyncPreSrOriginOwner;
class NativeAsyncPreSr {
public:
 bool Enabled()const noexcept{return BbNativeAsyncPreSr::Enabled();}
 bool TryCaptureOrigin(const Instance&,Scheduler&,const std::array<vk::Image,3>&,
             const std::array<vk::ImageCreateInfo,3>&,BbNativeAsync::Frame);
 void PollReady(const Instance&,Scheduler&,bool supported);
 // Renderer owner only: never submit guessed/future ticks or an empty no-peer pump.
 bool NeedsExitSubmit(uint64_t current_tick)noexcept{
  if(maximum_native_use_tick>current_tick){Quarantine();return false;}
  return !rejected&&maximum_native_use_tick!=0&&maximum_native_use_tick==current_tick;
 }
 void PrepareCurrentMasks(Scheduler&,uint64_t actual_source,bool supported);
 void TrackMasks(Scheduler&);
 bool Armed()const noexcept;
 bool PersistentPrepared(uint64_t source)const noexcept{return retained&&retained->persistent&&retained->persistent->PreparedFor(source);}
 std::optional<NativePersistentHistory::CaptureFrame> RuntimeCaptureFrame(uint64_t source)const{return retained&&retained->persistent?retained->persistent->CaptureFrameForSource(source):std::nullopt;}
 bool EffectSelected()const noexcept{return effect_selected;}
 unsigned EffectJob()const noexcept{return effect_job;}
 vk::ImageView RecordAdvanceAndApply(Scheduler&,uint64_t current_source);
 bool TryRecordSparseInput(Scheduler&,const NativeAsyncPreSrOriginResources&,const NativeAsyncPreSrOriginIdentity&,
                           const std::shared_ptr<NativeAsyncPreSrOriginGpuProof>&);
 // Completion is a managed owner-verified wire record. Not a renderer file read.
 struct SparseCompletion {NativeAsyncPreSrOriginIdentity identity;uint64_t input_tick_echo=0,helper_gpu_serial=0;
  bool verified_wire=false,helper_GPU_complete=false,output_valid=false,model_valid=false,expected_skip=false;
  uint64_t evaluate_calls=0,evaluate_result=0,resolve_calls=0,model_reset=0;};
 enum class SparseDisposition {Unavailable,Captured,ObsoleteDiscard,ExpectedSkip};
 struct SparseReturn {vk::ImageView view{};SparseDisposition disposition=SparseDisposition::Unavailable;};
 SparseReturn TryRecordSparseComplete(Scheduler&,const NativeAsyncPreSrOriginResources&,const SparseCompletion&,
                                     uint64_t current_source,const std::shared_ptr<NativeAsyncPreSrOriginGpuProof>&);
 bool FinalizeSparseLease(const NativeAsyncPreSrOriginIdentity& id,const std::shared_ptr<NativeAsyncPreSrOriginGpuProof>& proof,bool consumed_published_by_owner)noexcept;
 void DrainUnavailable(Scheduler&);
 void MergeRecorded(Scheduler&,uint64_t current_source);
 void SrReadRecorded(Scheduler&,uint64_t source,vk::Image image,bool success,bool domain_changed,bool applied_reset);
 vk::Image ReturnImage()const noexcept{return returned_image;}
 vk::ImageView RecordFixedInput(const Instance&,Scheduler&,uint64_t source,vk::Image input,const vk::ImageCreateInfo&);
 vk::Image FixedImage()const noexcept{return fixed_input?fixed_input->Image():vk::Image{};}
 bool FixedThisSource()const noexcept{return actual_fixed_input;}
 bool FixedTransition()const noexcept{return prior_fixed_input!=actual_fixed_input;}
 std::optional<vk::ImageCreateInfo> ReturnInfo(uint64_t source,vk::Image image)const noexcept;
 bool ChangeInputDomain(bool nr)noexcept{return input_domain.Change(nr);}
 void BeginSource(uint64_t source,bool supported)noexcept;
 uint64_t Source()const noexcept{return actual_source;}
 bool Supported()const noexcept{return actual_supported;}
 void RetireByTick(Scheduler&);
 std::optional<BbNativeAsync::Frame> ReadyOrigin()const noexcept;
 // Recorded producer ordering, not CPU copyReady or permission to publish INPUT.
 std::optional<BbNativeAsync::Frame> RecordedOrigin(const Scheduler&)const noexcept;
 void Shutdown(Scheduler&);
 void RejectAssociation()noexcept{rejected=true;}
 void QuarantineRecorded(Scheduler&)noexcept;
 void Quarantine()noexcept{BbNativeAsync::PublishExitHeld();rejected=true;stopping=true;if(fixed_input)fixed_input->HoldUnknown();if(retained&&retained->returns)retained->returns->HoldMasksUnknown();if(retained&&retained->persistent)retained->persistent->HoldUnknown();if(retained)for(const auto& p:retained->leases)if(p)p->native.unknown=true;}
 std::string Session()const;
 uint64_t Epoch()const noexcept{return last_epoch;}
 uint64_t OriginSource()const noexcept{return chain.Size()?chain.Origin().source:0;}
private:
 struct Retention {
  std::array<VideoCore::UniqueImage,3*BbNativeAsync::NodeCount> images;
  std::array<std::shared_ptr<BbNativeAsync::Node>,BbNativeAsync::NodeCount> records{};
  std::array<std::shared_ptr<BbNativeAsyncPreSr::SourceLease>,BbNativeAsync::NodeCount> leases{};
  std::unique_ptr<NativeAsyncPreSrSky> sky;
 std::unique_ptr<NativeAsyncPreSrReturn> returns;
 std::unique_ptr<NativePersistentHistory> persistent;
  std::shared_ptr<NativeAsyncPreSrOriginQuiescence> quiescence;
  ~Retention(){
   if(quiescence){uint64_t actual=0;for(const auto& lease:leases)if(lease)actual=std::max(actual,lease->native.observed.load(std::memory_order_acquire));
    auto old=quiescence->observed_tick.load();while(old<actual&&!quiescence->observed_tick.compare_exchange_weak(old,actual)){}
   }
   const bool visual_retired=!persistent||persistent->Ready();
   persistent.reset();returns.reset();sky.reset();for(auto& x:images)x=VideoCore::UniqueImage{};
   if(quiescence&&visual_retired)quiescence->stopped_and_destroyed.store(true,std::memory_order_release);}
  // Snapshot/converted/output/shown/context and their immutable CB/sets all
  // participated in the actual source cohort through SR/merge. The current
  // raw path has recorded ONLY mask clear, not any current image/CB use yet.
  bool PacketAndImageUsesReady()const noexcept {for(const auto& p:leases)if(p&&!p->CanReuse())return false;return true;}
  bool AllUsesReady()const noexcept {return PacketAndImageUsesReady()&&(!returns||returns->MasksReady())&&(!persistent||persistent->Ready());}

 };
 vk::ImageView RecordPersistentAdvanceAndApply(Scheduler&,uint64_t);
 void Create(const Instance&);
 bool ApplyRuntimeControls(Scheduler&);
 void PublishRuntimeStatus();
 BbNR::Request runtime_request{};
 BbNR::Snapshot runtime_status{};
 bool runtime_model_pending=false;
 uint64_t runtime_model_params=1,last_candidate_capture_ns=0;
 void TrackUse(Scheduler&);
 void PollPeer(Scheduler&,bool supported);
 static void HoldUnknown(std::shared_ptr<Retention>);
 std::shared_ptr<NativeAsyncPreSrFixedInput> fixed_input;bool actual_fixed_input=false,prior_fixed_input=false;
 static void HoldFixed(std::shared_ptr<NativeAsyncPreSrFixedInput>);
 const Instance* diagnostic_instance=nullptr; // borrowed renderer Instance outlives this managed controller
 std::shared_ptr<Retention> retained;
 std::shared_ptr<NativeAsyncPreSrOriginOwner> peer;
 std::unique_ptr<BbNativeAsyncPreSr::Frontend> frontend;
 std::shared_ptr<NativeSkyDiagnostic> diagnostic,edit_diagnostic;
 std::optional<NativeSkyDiagnostic::EditContext> edit_anchor;
 struct DiagnosticOrigin {NativeAsyncPreSrOriginIdentity job;BbNativeAsync::Frame frame;uint64_t input_tick=0,helper_serial=0;};
 std::optional<DiagnosticOrigin> once_origin;
 std::shared_ptr<NativeAsyncPreSrDiagnostic> once_diagnostic;bool once_attempted=false;
 static void HoldDiagnostic(std::shared_ptr<NativeAsyncPreSrDiagnostic>);
 bool snapshot_creation_attempted=false,peer_creation_attempted=false,peer_ready_observed=false;
 uint64_t maximum_native_use_tick=0;
 bool sparse_anchor=false,effect_selected=false;unsigned effect_job=0;
 uint64_t source_count=0;
 uint64_t play_first_source=0,play_last_source=0,play_source_gaps=0;bool play_summary_printed=false;
 std::shared_ptr<BbNativeAsync::Owner> owner;
 std::optional<NativeAsyncPreSrOriginIdentity> pending_sparse;
 std::optional<BbNativeAsync::RecordedOrigin> recorded_origin;
 std::shared_ptr<NativeAsyncPreSrOriginGpuProof> sparse_input_proof,sparse_final_proof;
 uint64_t last_helper_serial=0;

 BbNativeAsync::VisualTimeline visual_timeline;
 BbNativeAsync::Chain chain;
 BbNativeAsyncPreSr::InputDomain input_domain;vk::Image returned_image{};uint64_t actual_source=0;bool actual_supported=false;
 bool rejected=false,stopping=false;uint64_t last_epoch=0,last_reported_ready=0;
 uint64_t last_supported_source=0,last_supported_native_ns=0;double actual_source_dt=0;
};
}
