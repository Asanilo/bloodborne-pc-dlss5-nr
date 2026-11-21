#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vulkan/vulkan.h>

namespace player_core {

struct Profile;
const Profile &profile_1080p_quality() noexcept;

struct Image {
    VkImage image{};
    VkImageView view{};
    VkImageSubresourceRange range{};
    VkFormat format{};
    uint32_t width{}, height{};
};

struct Camera {
    float motion_scale_x = 1.f, motion_scale_y = 1.f;
    bool depth_inverted{};
    bool has_matrices{};
    // Row-major D3D clip coordinates, row vectors. Motion is current-to-previous.
    std::array<float, 16> clip_to_prev_clip{}, prev_clip_to_clip{};
};

struct Frame {
    Image color, depth, motion;
    Camera camera{};
    bool reset{};
};

enum class RecordResult { failed, seeded, generated };

class Core {
public:
    Core();
    ~Core();
    Core(const Core &) = delete;
    Core &operator=(const Core &) = delete;

    // Initialize with a live caller-owned device configured for BDA and NVX.
    bool initialize(VkPhysicalDevice physical, VkDevice device, const Profile &profile,
                    const std::filesystem::path &sdk_library, std::string &error);
    void record_initialize(VkCommandBuffer command);
    // Upload resources can be released when the caller's initialization fence signals.
    void release_upload() noexcept;

    // Record serially and submit in this same order on one caller-owned queue.
    // Keep all external resources alive until the submission retires.
    // All external images remain in GENERAL. Outputs are ordered by increasing phase.
    // Destroy the core only after every command using it has retired.
    // Seed/reset writes history and returns seeded before touching any output.
    RecordResult record(VkCommandBuffer command, const Frame &frame, std::span<const Image> outputs,
                        std::string &error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace player_core
