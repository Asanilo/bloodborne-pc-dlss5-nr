// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "p0_game_bridge.h"
#include "p0_nr_process.h"
#include "p0_status.h"
#include <chrono>
#include <memory>

namespace Vulkan {
// One renderer-owned session at a time. UI/frontend only submit coherent Enable requests.
class P0Controller {
public:
    P0Controller() = default;
    ~P0Controller();
    void Tick(const Instance&,Scheduler&,bool supported_route,bool menu_open);
    vk::ImageView ProcessScene(const Instance&,Scheduler&,vk::Image,vk::ImageView,
        vk::Image,vk::ImageView,vk::Image,vk::ImageView,vk::ImageLayout,u64,bool,float,
        std::array<float,2>,std::array<float,2>,std::array<float,2>,std::array<float,2>,
        bool,u64,bool,const std::array<vk::ImageCreateInfo,3>&);
    bool Shutdown(const Instance&,Scheduler&);
    bool Enabled() const noexcept { return opt_in; }
    bool FrameProcessed() const noexcept { return frame_processed; }
    bool Selected() const noexcept { return selected; }
    u32 FrameIndex() const noexcept { return frame_index; }
    std::string Session() const;
    std::string Mode() const { return mode; }
    std::string ExecutionMode() const { return execution; }
    bool AuditFull() const noexcept { return execution=="diagnostic"; }
    void PrepareReturnForMerge(Scheduler&);
    void BeginMergeTiming(Scheduler&);
    void EndMergeTiming(Scheduler&);
    void DeferRetirement(const Instance&,Scheduler&);
    void RejectPresentationAssociation();
private:
    void Configure();
    void ReadFrontend();
    void PublishStatus(bool force=false);
    void UpdateCounts();
    void ProgressExit(Scheduler&);
    void SetState(BbP0::State,std::string);
    std::atomic<bool> presentation_rejected{false};
    std::optional<double> last_hook_ms;
    bool configured=false,opt_in=false,exiting=false,menu_open=false,supported=false;
    bool frame_processed=false,selected=false,retry_requires_request=false;
    u32 frame_index=0,epoch=0,limit=0,seen_finalized=0,seen_accepted=0;
    u32 wall_seconds=900;
    u64 total_finalized=0,total_accepted=0,fallbacks=0,timeouts=0;
    u64 bypass_disabled=0,bypass_starting=0,bypass_unsupported=0,last_source=0;
    u64 external_revision=0,failed_request_revision=0;
    nlohmann::json last_frontend_request;
    std::string base_session,run_root,mode,execution,reason;
    BbP0::State state=BbP0::State::Disabled;
    std::chrono::steady_clock::time_point service_started{},last_scene{},last_status{};
    std::unique_ptr<P0NrProcess> process;
    std::unique_ptr<P0GameBridge> bridge;
};
}
