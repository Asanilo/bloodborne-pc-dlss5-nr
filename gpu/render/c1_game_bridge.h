// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

#include "vulkan/vk_common.h"
#include "textures/image.h"
#include "buffers/buffer.h"

namespace Vulkan {
class Instance;
class Scheduler;
class C1NoopProcess;
}

namespace Vulkan {

struct C1ImportedImage {
    std::string role;
    vk::Image image{};
    vk::DeviceMemory memory{};
    vk::ImageView view{};
    vk::Format format{};
    int raw_fd = -1;
};

struct C1FdIdentity {
    int number = -1;
    std::string target;
    uint64_t device = 0;
    uint64_t inode = 0;
    bool stat_ok = false;
};

// Single-slot game-device half of C1. It never owns a second VkDevice: all imported images,
// conversion dispatches, submission ticks and merge views belong to the active game device.
class C1GameBridge {
public:
    C1GameBridge() = default;
    C1GameBridge(const C1GameBridge&) = delete;
    C1GameBridge& operator=(const C1GameBridge&) = delete;

    bool Prepare(const Instance& instance, Scheduler& scheduler, C1NoopProcess& process,
                 std::string_view session, const std::string& run_dir,
                 const std::string& device_uuid, const std::string& driver_uuid,
                 u32 width, u32 height);
    vk::ImageView ProcessFrame(const Instance& instance, Scheduler& scheduler,
                               C1NoopProcess& process, vk::Image sr_color,
                               vk::ImageView sr_view, vk::Image depth,
                               vk::ImageView depth_view, vk::Image motion,
                               vk::ImageView motion_view, vk::ImageLayout depth_layout,
                               u64 source_game_frame, bool source_reset,
                               std::array<float, 2> viewport_jitter,
                               std::array<float, 2> sr_jitter);
    bool Quarantined() const noexcept { return quarantined; }
    bool Finished() const noexcept { return finished; }
    void DeferRetirement(const Instance& instance, Scheduler& scheduler);

private:
    bool ImportMetadata(const Instance& instance, C1NoopProcess& process);
    bool AuditFdRetired(const Instance& instance, const C1NoopProcess& process);
    void CreateLocalReturnAndPipeline(const Instance& instance);
    void Quarantine(std::string_view reason);
    bool prepared = false;
    bool baseline_audited = false;
    bool imported = false;
    bool fd_retired_ready = false;
    bool retirement_scheduled = false;
    bool abort_sent = false;
    bool quarantined = false;
    bool finished = false;
    u32 width = 0, height = 0;
    std::string session;
    std::string run_dir;
    std::vector<C1ImportedImage> resources;
    std::vector<C1FdIdentity> exporter_raw_fds;
    std::vector<C1FdIdentity> exporter_baseline_fds;
    std::optional<u64> pending_source_frame;
    u64 last_source_frame = 0;
    u64 input_tick = 0;
    u64 last_submitted_tick = 0;
    u64 last_completed_tick = 0;
    u64 outstanding_tick = 0;
    int exporter_pid = -1;
    C1NoopProcess* process_owner = nullptr;
    std::unique_ptr<VideoCore::Buffer> readback;
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
};

} // namespace Vulkan
