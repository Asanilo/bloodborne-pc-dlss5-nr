// SPDX-License-Identifier: GPL-2.0-or-later
#include "nr_sr_timing.h"
#include "nr_runtime_status.h"
#include "vulkan/instance.h"
#include "render/scheduler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Vulkan {
namespace {
constexpr const char* Scope="native_Dlss_Evaluate_emitted_commands_and_dependencies_only";
}
bool SrCommandTiming::Enabled() noexcept {
    if (std::getenv("BB_FG_PROFILE")) return true;
    const char* mode=std::getenv("BB_NR_P0_MODE");
    return mode && (!std::strcmp(mode,"resident") || !std::strcmp(mode,"skipEvaluate") ||
                    !std::strcmp(mode,"srOnly"));
}
SrCommandTiming::SrCommandTiming(const Instance& instance) : device(instance.GetDevice()) {
    valid_bits=instance.GetPhysicalDevice().getQueueFamilyProperties().at(
        instance.GetGraphicsQueueFamilyIndex()).timestampValidBits;
    period_ns=instance.GetPhysicalDevice().getProperties().limits.timestampPeriod;
    if (!valid_bits || valid_bits>64 || !std::isfinite(period_ns) || period_ns<=0) return;
    auto created=device.createQueryPoolUnique({.queryType=vk::QueryType::eTimestamp,
                                                     .queryCount=2*SrTimingMath::Slots::Count});
    if (created.result==vk::Result::eSuccess) pool=std::move(created.value);
}
void SrCommandTiming::NotMeasured(u64 source,const char* reason) {
    BbNRRuntime::PublishSrTiming({true,source,0,{},reason});
    std::printf("P0_SR_GPU_TIMING source_game_frame=%llu status=NOT_MEASURED reason=%s query_readback_bytes=0 scope=%s no_WAIT_BIT=1\n",
        (unsigned long long)source,reason,Scope);
}
std::optional<unsigned> SrCommandTiming::Begin(vk::CommandBuffer cmd,u64 source) {
    if (std::getenv("BB_FG_PROFILE") && source % 31 != 0) return {};
    if (!pool) {NotMeasured(source,"timestamps_unsupported_or_pool_unavailable");return {};}
    const auto slot=slots.Claim();
    if (!slot) {NotMeasured(source,"all_four_slots_pending_real_GPU_completion");return {};}
    cmd.resetQueryPool(*pool,2*(*slot),2);
    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,*pool,2*(*slot));
    return slot;
}
void SrCommandTiming::End(vk::CommandBuffer cmd,Scheduler& scheduler,unsigned slot,u64 source,bool success) {
    cmd.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,*pool,2*slot+1);
    const u64 tick=scheduler.CurrentTick();
    // Existing scheduler pending-op mechanism invokes this only after its actual
    // timeline target is reached. It does not Flush or wait on the renderer thread.
    // Shared ownership holds the pool through its final GPU use; no raw this/Scheduler
    // pointer escapes. A busy slot is never reset before this callback completes.
    scheduler.DeferPriorityOperation([owner=shared_from_this(),slot,source,tick,success] {
        std::array<uint64_t,4> q{};
        const auto result=owner->device.getQueryPoolResults(*owner->pool,2*slot,2,sizeof(q),
            q.data(),2*sizeof(uint64_t),vk::QueryResultFlagBits::e64 |
                                      vk::QueryResultFlagBits::eWithAvailability);
        const auto ms=SrTimingMath::DurationMs(int(result),q,owner->valid_bits,owner->period_ns,true);
        const char* status=!success ? "EVALUATE_RECORD_FAILED" : ms ? "MEASURED" : "NOT_MEASURED";
        const char* reason=!success ? "record_failed" : ms ? "available_after_actual_GPU_tick" :
            result==vk::Result::eNotReady ? "query_not_ready" : result!=vk::Result::eSuccess ? "query_error" : "availability_zero";
        BbNRRuntime::PublishSrTiming({true,source,tick,success?ms:std::nullopt,reason});
        char duration[32]="NOT_MEASURED";
        if(success&&ms) std::snprintf(duration,sizeof(duration),"%.9g",*ms);
        std::printf("P0_SR_GPU_TIMING source_game_frame=%llu tick=%llu status=%s reason=%s duration_ms=%s result=%d availability=%llu,%llu timestamp_valid_bits=%u timestamp_period_ns=%.9g query_readback_bytes=32 actual_GPU_tick_complete=1 scope=%s excludes=feature_create_motion_generation_sharpen_NR_merge_Present not_full_game_GPU=1 not_pure_SR=1 no_WAIT_BIT=1 wrap=masked_valid_bits\n",
            (unsigned long long)source,(unsigned long long)tick,status,reason,duration,
            int(result),(unsigned long long)q[1],(unsigned long long)q[3],owner->valid_bits,owner->period_ns,Scope);
        owner->slots.Complete(slot,true); // even NOT_READY: actual final GPU use is proved
    });
}
} // namespace Vulkan
