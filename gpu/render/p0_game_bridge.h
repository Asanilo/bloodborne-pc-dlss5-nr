// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "vulkan/vk_common.h"
#include "textures/image.h"
#include "buffers/buffer.h"
#include "render/c4_frame_validate.h"

namespace Vulkan {
class Instance;
class Scheduler;
class P0NrProcess;
}

namespace Vulkan {

struct P0ImportedImage {
    std::string role;
    vk::Image image{};
    vk::DeviceMemory memory{};
    vk::ImageView view{};
    vk::Format format{};
    int raw_fd = -1;
};

struct P0FdIdentity {
    int number = -1;
    std::string target;
    uint64_t device = 0;
    uint64_t inode = 0;
    bool stat_ok = false;
};

// Single-slot game-device half of P0. It never owns a second VkDevice: all imported images,
// conversion dispatches, submission ticks and merge views belong to the active game device.
class P0GameBridge {
public:
    P0GameBridge() = default;
    P0GameBridge(const P0GameBridge&) = delete;
    P0GameBridge& operator=(const P0GameBridge&) = delete;

    bool Prepare(const Instance& instance, Scheduler& scheduler, P0NrProcess& process,
                 std::string_view session, const std::string& run_dir,
                 const std::string& device_uuid, const std::string& driver_uuid,
                 u32 width, u32 height);
    vk::ImageView ProcessFrame(const Instance& instance, Scheduler& scheduler,
                               P0NrProcess& process, vk::Image sr_color,
                               vk::ImageView sr_view, vk::Image depth,
                               vk::ImageView depth_view, vk::Image motion,
                               vk::ImageView motion_view, vk::ImageLayout depth_layout,
                               u64 source_game_frame, bool source_reset,
                               float source_frame_ms,
                               std::array<float, 2> viewport_jitter,
                               std::array<float, 2> previous_viewport_jitter,
                               std::array<float, 2> sr_jitter,
                               std::array<float, 2> previous_sr_jitter,
                               bool sr_initial_reset_observed,
                               u64 sr_initial_source_game_frame,
                               bool sr_history_jitter_enabled_from_start,
                               const std::array<vk::ImageCreateInfo, 3>& source_infos);
    // Record the image dependency on the current post-fence game command buffer before the
    // original merge samples the independently owned return image.
    void PrepareReturnForMerge(Scheduler& scheduler);
    bool Quarantined() const noexcept { return quarantined; }
    bool Finished() const noexcept { return finished; }
    bool FrameProcessed() const noexcept { return frame_processed; }
    std::string_view Mode() const noexcept { return mode; }
    u32 NextFrameIndex() const noexcept { return processed_frames; }
    bool HasRemainingFrames() const noexcept { return processed_frames < frame_count; }
    u32 FrameIndex() const noexcept { return processed_frames ? processed_frames - 1 : 0; }
    bool AuditFull() const noexcept { return current_audit_full; }
    void BeginMergeTiming(Scheduler& scheduler);
    void EndMergeTiming(Scheduler& scheduler);
    void DeferRetirement(const Instance& instance, Scheduler& scheduler);
    // GPU/render thread only; no worker invokes Vulkan or mutates this owner.
    void PollManaged(const Instance& instance, Scheduler& scheduler, P0NrProcess& process);
    bool RequestControl(std::string_view action, std::string_view reason);
    bool Prepared() const noexcept { return prepared; }
    bool RecoverableExpiry() const noexcept { return soft_expired && !abort_sent; }
    bool IsIdle() const noexcept { return idle; }
    bool IsRetiring() const noexcept { return retire_requested; }
    bool HasPendingControl() const noexcept { return control_request.has_value(); }
    bool HasInFlightTuple() const noexcept { return !pending_input.empty(); }
    bool NativeUsesComplete(Scheduler& scheduler);
    u32 FinalizedCount() const noexcept { return processed_frames; }
    u32 DiscardedCount() const noexcept { return discarded_count; }
    u32 AcceptedCount() const noexcept { return accepted_count; }
    std::optional<double> EvaluateGpuMs() const noexcept { return last_helper_evaluate_ms; }
    void SetAcceptanceBudget(u32 value) { acceptance_budget_ms = value; }
    void BeginStop(std::string reason) { stop_reason=std::move(reason); stop_pending=true; }
    bool ReleaseProtocolComplete() const noexcept { return finished.load() && retirement_audited; }
    void RejectPresentationAssociation() { Quarantine("P0 guest prepare does not match pending source/merge epoch"); }

private:
    bool ImportMetadata(const Instance& instance, P0NrProcess& process);
    bool AuditFdRetired(const Instance& instance, const P0NrProcess& process);
    void CreateLocalReturnAndPipeline(const Instance& instance);
    void Quarantine(std::string_view reason);
    void ReadMergeTiming(u32 frame, u64 source);
    void Timestamp(Scheduler& scheduler, u32 index);
    std::optional<double> QueryDuration(u32 begin, u32 end);
    void RecordValidation(const Instance& instance, Scheduler& scheduler,
                          vk::ImageView source, vk::ImageView output, bool validate_return = false);
    C4FrameValidationStats ReadValidation();
    std::optional<double> last_helper_evaluate_ms;
    bool prepared = false;
    bool baseline_audited = false;
    bool imported = false;
    bool fd_retired_ready = false;
    bool retirement_scheduled = false;
    bool abort_sent = false;
    bool idle = false, stop_pending = false, retire_requested = false;
    bool release_published = false, retirement_audited = false;
    bool resume_reset_pending = false;
    u32 accepted_count = 0, discarded_count = 0;
    u32 acceptance_budget_ms = 500;
    u64 history_version = 1, control_epoch = 0, last_peer_fence_serial = 0;
    std::string stop_reason;
    std::optional<nlohmann::json> control_request;
    std::chrono::steady_clock::time_point managed_control_deadline{};
    nlohmann::json pending_input;
    bool soft_expired = false, complete_GPU_observed = false, output_sample_started = false;
    std::chrono::steady_clock::time_point late_completion_deadline{};
    std::atomic<bool> quarantined{false};
    std::atomic<bool> finished{false};
    bool frame_processed = false;
    bool local_return_initialized = false;
    bool cold_deadline_started = false;
    std::chrono::steady_clock::time_point cold_started_at{};
    std::chrono::steady_clock::time_point control_deadline{};
    std::string mode;
    std::string execution_mode;
    u32 width = 0, height = 0;
    std::string session;
    std::string run_dir;
    std::vector<P0ImportedImage> resources;
    std::vector<P0FdIdentity> exporter_raw_fds;
    std::vector<P0FdIdentity> exporter_baseline_fds;
    std::optional<u64> pending_source_frame;
    u64 last_source_frame = 0;
    u64 pending_merge_tick = 0;
    u32 frame_count = 0, processed_frames = 0;
    bool current_audit_full = false;
    u64 input_tick = 0;
    u64 last_submitted_tick = 0;
    u64 last_completed_tick = 0;
    u64 outstanding_tick = 0;
    int exporter_pid = -1;
    P0NrProcess* process_owner = nullptr;
    std::unique_ptr<VideoCore::Buffer> conversion_guard;
    vk::Device timing_device{};
    vk::UniqueQueryPool timing_pool;
    u32 timestamp_valid_bits = 0;
    double timestamp_period = 0;
    u64 frame_start_monotonic_ns = 0;
    std::unique_ptr<VideoCore::Buffer> readback;
    std::unique_ptr<VideoCore::Buffer> guide_readback;
    VideoCore::HostImage local_return;
    vk::UniqueImageView local_return_view;
    vk::ImageView returned_view{};
    vk::UniqueDescriptorSetLayout descriptor_layout;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniquePipeline pipeline;
    vk::UniqueShaderModule shader;
    vk::UniqueSampler depth_sampler;
    vk::UniqueDescriptorPool descriptor_pool;
    vk::DescriptorSet descriptor_set{}; // owned by descriptor_pool
    std::unique_ptr<VideoCore::Buffer> validation_stats;
    vk::UniqueDescriptorSetLayout validation_descriptor_layout;
    vk::UniquePipelineLayout validation_pipeline_layout;
    vk::UniquePipeline validation_pipeline;
    vk::UniquePipeline validation_return_pipeline;
    std::unique_ptr<VideoCore::Buffer> return_validation_stats;
    vk::UniqueDescriptorPool validation_descriptor_pool;
    vk::DescriptorSet validation_descriptor_set{};
};

} // namespace Vulkan
