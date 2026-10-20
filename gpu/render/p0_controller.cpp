// SPDX-License-Identifier: GPL-2.0-or-later
#include "p0_controller.h"
#include "p0_wire.h"
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <signal.h>
#include <thread>
#include <unistd.h>

namespace Vulkan {
namespace {
std::string env(const char*name) {const char*v=std::getenv(name);return v?v:"";}
unsigned uint_env(const char*name,unsigned def) {
 const auto value=env(name);if(value.empty())return def;
 if(value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("P0 invalid uint env");
 const auto n=std::stoull(value);if(n>UINT32_MAX)throw std::runtime_error("P0 uint env overflow");return unsigned(n);
}
std::string read_text(const std::string&path) {
 std::error_code e;const auto n=std::filesystem::file_size(path,e);
 if(e)return {};if(n>65536)throw std::runtime_error("P0 frontend/control exceeds64KiB");
 if(std::filesystem::is_symlink(path))throw std::runtime_error("P0 frontend symlink refused");
 std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
void atomic_text(const std::string&path,const std::string&text) {
 const auto tmp=path+".tmp-"+std::to_string(getpid());
 std::ofstream out(tmp,std::ios::binary|std::ios::trunc);out.write(text.data(),text.size());out.close();
 if(!out||rename(tmp.c_str(),path.c_str())!=0)throw std::runtime_error("P0 frontend publication failed");
}
std::string hex_uuid(const auto&v) {constexpr char digits[]="0123456789abcdef";std::string s;for(auto b:v){s+=digits[b>>4];s+=digits[b&15];}return s;}
}
void P0Controller::Configure() {
 if(configured)return;configured=true;mode=env("BB_NR_P0_MODE");
 opt_in=mode=="resident"||mode=="skipEvaluate";execution=env("BB_NR_P0_EXECUTION_MODE");
 if(!opt_in&&mode!="srOnly")return;
 base_session=env("BB_NR_P0_SESSION");
 const auto candidate_root=env("BB_NR_P0_RUN_DIR");
 if(base_session.empty()||base_session.size()>63||base_session.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos||
    candidate_root.empty()||candidate_root[0]!='/'||std::filesystem::is_symlink(candidate_root)||!std::filesystem::is_directory(candidate_root)||
    read_text(candidate_root+"/profile/.p0-profile")!=base_session+"\n")
    throw std::runtime_error("P0 private case/marker invalid; no frontend write/spawn");
 run_root=candidate_root;
 limit=uint_env("BB_NR_P0_FRAME_COUNT",0);wall_seconds=uint_env("BB_NR_P0_SERVICE_WALL_SECONDS",900);
 const auto initial=uint_env("BB_NR_P0_INITIAL_ENABLE",mode=="srOnly"?0:1);
 const auto starts=uint_env("BB_NR_P0_MAX_HELPER_STARTS",mode=="srOnly"?0:32);
 if(!P0Wire::Allowed(mode,limit,execution)||initial>1||!wall_seconds||wall_seconds>900||starts>32||
    (opt_in&&!starts)||run_root.empty()||run_root[0]!='/'||base_session.empty())
    throw std::runtime_error("P0 launch bounds invalid; no helper spawned");
 BbP0::InitializeRequest(opt_in,initial!=0);service_started=std::chrono::steady_clock::now();
 PublishStatus(true);
}
void P0Controller::SetState(BbP0::State value,std::string message) {
 const bool changed=state!=value||reason!=message;state=value;reason=std::move(message);
 if(changed) {
  std::printf("P0_STATE state=%s reason=%s finalized=%llu accepted_for_merge=%llu helper_starts=%u\n",
    BbP0::StateName(state),reason.c_str(),(unsigned long long)total_finalized,(unsigned long long)total_accepted,P0NrProcess::GlobalSpawnCount());
  PublishStatus(true);
 }
}
void P0Controller::ReadFrontend() {
 if(run_root.empty())return;const auto text=read_text(run_root+"/frontend-request.json");if(text.empty())return;
 const auto req=P0Wire::Parse(text,"frontend-request");
 if(req.size()!=4||req.at("schema")!="p0-frontend-request-v1"||req.at("session")!=base_session||!req.at("requested_enable").is_boolean())
  throw std::runtime_error("P0 frontend exact schema/session/type failed");
 const auto revision=P0Wire::Uint(req,"revision");if(!revision)throw std::runtime_error("P0 frontend revision zero");
 if(revision<external_revision)return;
 if(revision==external_revision) {if(req!=last_frontend_request)throw std::runtime_error("P0 changed frontend content at accepted revision");return;}
 if(revision!=external_revision+1)throw std::runtime_error("P0 frontend skipped/future revision");
 BbP0::RequestEnable(req.at("requested_enable").get<bool>());
 external_revision=revision;last_frontend_request=req;PublishStatus(true);
}
void P0Controller::UpdateCounts() {
 if(!bridge)return;
 const auto finalized=bridge->FinalizedCount(),accepted=bridge->AcceptedCount();
 if(finalized<seen_finalized||accepted<seen_accepted)throw std::runtime_error("P0 counters regressed");
 total_finalized+=finalized-seen_finalized;total_accepted+=accepted-seen_accepted;
 seen_finalized=finalized;seen_accepted=accepted;
}
void P0Controller::PublishStatus(bool force) {
 const auto now=std::chrono::steady_clock::now();
 if(!force&&now-last_status<std::chrono::seconds(1))return;last_status=now;
 const auto request=BbP0::ReadRequest();
 const bool applied=process&&!process->Completed()&&bridge&&bridge->Prepared()&&
  state!=BbP0::State::Starting&&state!=BbP0::State::Failed&&state!=BbP0::State::Quarantined;
 BbP0::Snapshot snapshot;
 snapshot.opt_in=opt_in;snapshot.state=state;snapshot.applied_enable=applied;
 snapshot.source_game_frame=last_source;snapshot.generation=0;snapshot.frame=bridge?bridge->FinalizedCount():0;
 snapshot.session=process?process->Session():"";snapshot.reason=reason;
 snapshot.hook_cpu_ms=last_hook_ms;snapshot.evaluate_gpu_ms=bridge?bridge->EvaluateGpuMs():std::nullopt;
 snapshot.finalized=total_finalized;snapshot.accepted=total_accepted;snapshot.fallbacks=fallbacks;snapshot.timeouts=timeouts;
 snapshot.gpu_completion_unknown=bridge&&bridge->Quarantined()&&!bridge->ReleaseProtocolComplete();
 snapshot.helper_exit_observed=process&&process->Completed();snapshot.applied_request_revision=request.revision;
 BbP0::Publish(std::move(snapshot));
 if(run_root.empty())return;
 P0Wire::Json status{{"schema","p0-frontend-status-v1"},{"session",base_session},{"external_revision",external_revision},
  {"requested_enable",request.enabled},{"applied_enable",applied},{"state",BbP0::StateName(state)},
  {"helper_starts",P0NrProcess::GlobalSpawnCount()},{"finalized_count",total_finalized},
  {"accepted_for_merge_count",total_accepted},{"fallback_count",fallbacks},{"fallback_reason",reason},
  {"active_session",process?P0Wire::Json(process->Session()):P0Wire::Json(nullptr)},
  {"source_game_frame",last_source},{"nr_gpu_ms",bridge&&bridge->EvaluateGpuMs()?P0Wire::Json(*bridge->EvaluateGpuMs()):P0Wire::Json(nullptr)},{"sr_gpu_ms",nullptr},
  {"bypass_disabled_count",bypass_disabled},{"bypass_starting_count",bypass_starting},{"bypass_unsupported_count",bypass_unsupported}};
 try {atomic_text(run_root+"/frontend-status.json",status.dump());}
 catch(const std::exception&e) {static std::atomic_flag once=ATOMIC_FLAG_INIT;if(!once.test_and_set())std::fprintf(stderr,"P0_STATUS_IO_FAILED reason=%s\n",e.what());}
}
std::string P0Controller::Session() const {return process?process->Session():base_session;}

void P0Controller::ProgressExit(Scheduler& scheduler) {
    const auto exit=BbP0::ReadExitState();
    if(exit==BbP0::ExitState::NotRequested || exit==BbP0::ExitState::NormalComplete)return;
    BbP0::NoteExitDraining();
    // A missing owner is safe only because owners are removed exclusively after the
    // verified normal retirement/reap branch below (or before the first launch).
    BbP0::ExitProof proof;
    proof.helper_or_launch_owned=process || bridge || epoch!=0;
    proof.retirement_audited=!bridge || bridge->ReleaseProtocolComplete();
    proof.peer_reaped=!process || process->ReapedNormally();
    proof.current_input_finalized=!bridge || !bridge->HasInFlightTuple();
    proof.native_last_use_complete=!bridge || bridge->NativeUsesComplete(scheduler);
    proof.unknown_gpu_state=bridge && !bridge->ReleaseProtocolComplete() && bridge->Quarantined();
    if(BbP0::PublishExitProof(proof)) {
        if(!process&&!bridge&&epoch==0)
            std::printf("P0_EXIT_NORMAL managed_owner=1 no_owner_before_launch=1 P0_work_never_created=1 retirement_scope=NOT_REQUIRED_NO_OWNER peer_scope=NOT_REQUIRED_NO_OWNER total_finalized=0 helper_starts=0\n");
        else std::printf("P0_EXIT_NORMAL managed_owner=1 P0_native_last_use_complete=1 retirement_audited=1 peer_reaped=1 no_owner_before_launch=0 retired_sessions=%u total_finalized=%llu helper_starts=%u\n",
            epoch,(unsigned long long)total_finalized,P0NrProcess::GlobalSpawnCount());
    }
}
void P0Controller::Tick(const Instance& instance,Scheduler& scheduler,bool route,bool menu) {
    try {
        Configure();if(!opt_in&&mode!="srOnly")return;
        if(BbP0::ReadExitState()!=BbP0::ExitState::NotRequested) exiting=true;
        supported=route;menu_open=menu;ReadFrontend();
        if(presentation_rejected.exchange(false)&&bridge) {bridge->RejectPresentationAssociation();retry_requires_request=true;failed_request_revision=BbP0::ReadRequest().revision;}
        const auto request=BbP0::ReadRequest();
        const auto now=std::chrono::steady_clock::now();
        if(now-service_started>=std::chrono::seconds(wall_seconds)||total_finalized>=limit)
            if(bridge) bridge->BeginStop("budget");
        if(bridge&&process) {
            // Finish bootstrap even when Enable was turned off. No current game image is
            // touched here; this lets a real zero-frame import/drain complete normally.
            if(!bridge->Prepared()&&!bridge->Quarantined()&&!process->Completed()&&!process->SetupFailed()) {
                vk::PhysicalDeviceIDProperties id{};vk::PhysicalDeviceProperties2 props{.pNext=&id};
                instance.GetPhysicalDevice().getProperties2(&props);
                bridge->Prepare(instance,scheduler,*process,process->Session(),process->RunDir(),
                    hex_uuid(id.deviceUUID),hex_uuid(id.driverUUID),1920,1080);
            }
            bridge->PollManaged(instance,scheduler,*process);UpdateCounts();
            if(!request.enabled||exiting) bridge->BeginStop(exiting?"exit":"enable_off");
            else if(!route) bridge->BeginStop("unsupported");
            if(bridge->Prepared()&&!bridge->Quarantined()&&!bridge->IsRetiring()&&!bridge->HasPendingControl()&&
               bridge->NativeUsesComplete(scheduler)&&!bridge->HasInFlightTuple()) {
                const bool scene_gap=last_scene.time_since_epoch().count()&&now-last_scene>std::chrono::milliseconds(250);
                if(!bridge->IsIdle()&&(menu||scene_gap)) bridge->RequestControl("idle",menu?"pause":"load");
                if(bridge->IsIdle()&&!menu&&route&&last_scene.time_since_epoch().count()&&!scene_gap)
                    bridge->RequestControl("resume","resume");
            }
            if(bridge->ReleaseProtocolComplete()&&process->ReapedNormally()) {
                const bool was_failure=bridge->Quarantined();
                bridge.reset();process.reset();++epoch;seen_finalized=seen_accepted=0;
                if(was_failure) {retry_requires_request=true;failed_request_revision=request.revision;SetState(BbP0::State::Failed,"Expired frame drained; toggle off/on for a fresh session");}
                else SetState(BbP0::State::Disabled,exiting?"Stopped":"Session drained and private peer reaped");
            } else if(bridge->Quarantined()) {
                retry_requires_request=true;failed_request_revision=request.revision;
                SetState(BbP0::State::Quarantined,bridge->RecoverableExpiry()?"Current output expired; held for proven discard/drain":"Failure: resources held, original SR retained");
            } else if(bridge->IsRetiring()||!request.enabled||exiting||!route) SetState(BbP0::State::Draining,"Awaiting actual GPU last use, retirement and peer reap");
            else if(process->SetupFailed()||process->Completed()) {
                process->RequestAbort("P0 peer failed before normal retirement");retry_requires_request=true;failed_request_revision=request.revision;
                SetState(BbP0::State::Failed,"Private helper failure; original SR retained");
            } else if(!bridge->Prepared()) SetState(BbP0::State::Starting,"Helper/model/import initializing; original SR");
            else if(bridge->IsIdle()) SetState(BbP0::State::Idle,"Scene suspended; next resume requests reset");
            else SetState(total_accepted?BbP0::State::Active:BbP0::State::Ready,"Experimental Quality NR ready");
        } else if(!request.enabled) SetState(BbP0::State::Disabled,"NR disabled");
        ProgressExit(scheduler);
        PublishStatus();
    } catch(const std::exception&e) {
        if(process)process->RequestAbort(std::string("P0 managed controller failure: ")+e.what());
        retry_requires_request=true;failed_request_revision=BbP0::ReadRequest().revision;
        SetState(BbP0::State::Failed,e.what());
        if(BbP0::ReadExitState()!=BbP0::ExitState::NotRequested) {exiting=true;ProgressExit(scheduler);}
    }
}

vk::ImageView P0Controller::ProcessScene(const Instance& instance,Scheduler& scheduler,
 vk::Image sr,vk::ImageView srv,vk::Image depth,vk::ImageView depthv,vk::Image mv,vk::ImageView mvv,
 vk::ImageLayout layout,u64 source,bool source_reset,float frame_ms,
 std::array<float,2> viewport,std::array<float,2> prev_viewport,std::array<float,2> sr_jitter,std::array<float,2> prev_sr_jitter,
 bool sr_initial,u64 sr_initial_source,bool jitter_history,const std::array<vk::ImageCreateInfo,3>& ci) {
    frame_processed=false;selected=false;
    Tick(instance,scheduler,true,menu_open);
    last_scene=std::chrono::steady_clock::now();
    if(!opt_in)return {};
    const auto request=BbP0::ReadRequest();
    if(!request.enabled||exiting||BbP0::ReadExitState()!=BbP0::ExitState::NotRequested){++bypass_disabled;return {};}
    if(total_finalized>=limit||std::chrono::steady_clock::now()-service_started>=std::chrono::seconds(wall_seconds))return {};
    if(menu_open){++bypass_unsupported;return {};}
    if(retry_requires_request&&request.revision<=failed_request_revision)return {};
    if(!process) {
        const auto maximum=uint_env("BB_NR_P0_MAX_HELPER_STARTS",32);
        if(P0NrProcess::GlobalSpawnCount()>=maximum){SetState(BbP0::State::Failed,"Declared helper-start budget exhausted");return {};}
        retry_requires_request=false;
        process=std::make_unique<P0NrProcess>();bridge=std::make_unique<P0GameBridge>();
        bridge->SetAcceptanceBudget(execution=="hot"?500u:30000u);
        vk::PhysicalDeviceIDProperties id{};vk::PhysicalDeviceProperties2 props{.pNext=&id};instance.GetPhysicalDevice().getProperties2(&props);
        if(!process->Start(hex_uuid(id.deviceUUID),hex_uuid(id.driverUUID),1920,1080,epoch)) {
            retry_requires_request=true;failed_request_revision=request.revision;SetState(BbP0::State::Failed,process->Failure());return {};
        }
        SetState(BbP0::State::Starting,"Background model initialization; original SR");
    }
    if(bridge->Quarantined()||bridge->IsRetiring()||bridge->HasPendingControl()||bridge->HasInFlightTuple())return {};
    vk::PhysicalDeviceIDProperties id{};vk::PhysicalDeviceProperties2 props{.pNext=&id};instance.GetPhysicalDevice().getProperties2(&props);
    if(!bridge->Prepare(instance,scheduler,*process,process->Session(),process->RunDir(),hex_uuid(id.deviceUUID),hex_uuid(id.driverUUID),1920,1080)) {++bypass_starting;return {};}
    bridge->PollManaged(instance,scheduler,*process);
    if(bridge->IsIdle()) {
        if(bridge->NativeUsesComplete(scheduler))bridge->RequestControl("resume","resume");
        return {};
    }
    if(bridge->HasPendingControl()||bridge->IsRetiring()||bridge->Quarantined())return {};
    const auto begin=std::chrono::steady_clock::now();
    const auto view=bridge->ProcessFrame(instance,scheduler,*process,sr,srv,depth,depthv,mv,mvv,layout,source,source_reset,
       frame_ms,viewport,prev_viewport,sr_jitter,prev_sr_jitter,sr_initial,sr_initial_source,jitter_history,ci);
    last_scene=std::chrono::steady_clock::now();last_source=source;UpdateCounts();
    frame_processed=bridge->FrameProcessed();frame_index=bridge->FrameIndex();selected=bool(view);
    if(!view){++fallbacks;if(bridge->RecoverableExpiry())++timeouts;}
    last_hook_ms=std::chrono::duration<double,std::milli>(last_scene-begin).count();
    auto snap=BbP0::ReadSnapshot();snap.hook_cpu_ms=last_hook_ms;BbP0::Publish(std::move(snap));
    if(bridge->Quarantined()){retry_requires_request=true;failed_request_revision=request.revision;SetState(BbP0::State::Quarantined,"Current frame rejected; original SR retained");}
    return view;
}
void P0Controller::PrepareReturnForMerge(Scheduler&s){if(selected&&bridge)bridge->PrepareReturnForMerge(s);}
void P0Controller::BeginMergeTiming(Scheduler&s){if(frame_processed&&bridge)bridge->BeginMergeTiming(s);}
void P0Controller::EndMergeTiming(Scheduler&s){if(frame_processed&&bridge)bridge->EndMergeTiming(s);}
void P0Controller::DeferRetirement(const Instance&i,Scheduler&s){if(frame_processed&&bridge)bridge->DeferRetirement(i,s);}
void P0Controller::RejectPresentationAssociation(){presentation_rejected.store(true);}

bool P0Controller::Shutdown(const Instance&instance,Scheduler&scheduler) {
    if(!process)return true;
    exiting=true;if(bridge)bridge->BeginStop("exit");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(process&&std::chrono::steady_clock::now()<deadline) {
        if(!bridge||!bridge->Prepared())process->RequestAbort("P0 quit during bootstrap; abnormal no shared frame accepted");
        Tick(instance,scheduler,true,false);
        if(!process)return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(process) {
        process->RequestAbort("P0 quit drain proof deadline expired; unknown GPU/resources held");
        process->SignalVerifiedPeer(SIGTERM);
        const auto grace=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(!process->Completed()&&std::chrono::steady_clock::now()<grace)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!process->Completed()) {
            process->SignalVerifiedPeer(SIGKILL);
            const auto kill_grace=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while(!process->Completed()&&std::chrono::steady_clock::now()<kill_grace)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        SetState(BbP0::State::Quarantined,"Exit without complete retirement proof; abnormal held resources");
        std::fprintf(stderr,"P0_EXIT_ABNORMAL normal_release=0 peer_reaped=%u resources_held=1\n",process->Completed());
    }
    return false;
}
P0Controller::~P0Controller() {
    // No raw callbacks or detached audits survive us. If normal shutdown failed, deliberately
    // retain uncertain Vulkan ownership until process/device teardown rather than free it as safe.
    if(bridge&&(!bridge->ReleaseProtocolComplete()||!process||!process->ReapedNormally())) {
        (void)bridge.release();(void)process.release();
        std::fprintf(stderr,"P0_STICKY_OWNER_HELD_UNTIL_PROCESS_EXIT normal_release=0\n");
    }
}

} // namespace Vulkan
