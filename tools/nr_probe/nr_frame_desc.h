// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>

// Experimental Linux little-endian wire ABI, NOT an NVIDIA NR SDK interface.
// All handles travel as SCM_RIGHTS, never as VkImage/pointer/numeric fd values.
namespace nr {
constexpr uint32_t Magic = 0x3146524e; // NRF1
constexpr uint32_t Version = 1, ImageCount = 4, FdCount = 6;
enum Role : uint32_t { Color, Depth, Motion, Output };
enum Mode : uint32_t { NoOp, PassThrough };
struct ImageDesc {
  uint32_t role, format, width, height;
  uint32_t usage, tiling, layout, aspect;
  uint32_t memory_type, fd_slot, mip_levels, array_layers;
  uint64_t allocation_size, bind_offset;
};
struct NRFrameDesc {
  uint32_t magic = Magic, version = Version, bytes = sizeof(NRFrameDesc),
           mode = PassThrough;
  uint64_t session = 1, generation = 1, sequence = 1;
  uint8_t device_uuid[VK_UUID_SIZE]{}, driver_uuid[VK_UUID_SIZE]{};
  uint32_t render_width = 64, render_height = 48, output_width = 64,
           output_height = 48;
  // Units: render pixels, +x right/+y down. MV = previous minus current; no
  // jitter in MV.
  float jitter_x = 0, jitter_y = 0, mv_scale_x = 1, mv_scale_y = 1;
  float frame_ms = 16.666667f, pre_exposure = 1, near_plane = 0.1f,
        far_plane = 1000;
  uint32_t reset = 1, color_encoding = 1, depth_encoding = 1, mv_encoding = 1;
  // 1 = linear scene color / forward device-Z [0,1] / unjittered render-pixel
  // MV.
  uint32_t image_count = ImageCount, fd_count = FdCount, ready_slot = 4,
           done_slot = 5;
  uint32_t handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  uint32_t sync_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
  uint32_t reserved[2]{};
  ImageDesc images[ImageCount]{};
};
static_assert(sizeof(ImageDesc) == 64 && sizeof(NRFrameDesc) == 424);
static_assert(offsetof(NRFrameDesc, images) == 168);
static_assert(std::endian::native == std::endian::little);
constexpr VkFormat Formats[]{VK_FORMAT_R32G32B32A32_SFLOAT,
                             VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                             VK_FORMAT_R32G32B32A32_SFLOAT};
constexpr uint32_t Channels[]{4, 1, 2, 4};
constexpr uint32_t Usage =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
inline void require(bool condition, const char *code) {
  if (!condition)
    throw std::runtime_error(code);
}
inline void validate(const NRFrameDesc &d, size_t bytes, size_t fds) {
  require(bytes == sizeof(d) && d.bytes == sizeof(d), "E_PACKET_SIZE");
  require(d.magic == Magic && d.version == Version, "E_ABI_VERSION");
  require(d.mode <= PassThrough && d.image_count == ImageCount,
          "E_MODE_IMAGES");
  require(d.session && d.generation && d.sequence, "E_SEQUENCE");
  require(d.fd_count == FdCount && fds == FdCount && d.ready_slot == 4 &&
              d.done_slot == 5,
          "E_FD_COUNT");
  require(d.handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT &&
              d.sync_type == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
          "E_HANDLE_TYPE");
  require(!d.reserved[0] && !d.reserved[1], "E_RESERVED");
  require(d.render_width && d.render_height && d.render_width <= 4096 &&
              d.render_height <= 4096 && d.output_width == d.render_width &&
              d.output_height == d.render_height,
          "E_EXTENT");
  require(d.color_encoding == 1 && d.depth_encoding == 1 &&
              d.mv_encoding == 1 && d.reset <= 1,
          "E_CONVENTION");
  require(std::isfinite(d.jitter_x) && std::isfinite(d.jitter_y) &&
              std::abs(d.jitter_x) <= 0.5f && std::abs(d.jitter_y) <= 0.5f &&
              d.mv_scale_x == 1 && d.mv_scale_y == 1 &&
              std::isfinite(d.frame_ms) && d.frame_ms > 0 &&
              d.frame_ms <= 1000 && d.pre_exposure == 1 &&
              std::isfinite(d.near_plane) && std::isfinite(d.far_plane) &&
              d.near_plane > 0 && d.far_plane > d.near_plane,
          "E_METADATA");
  for (uint32_t i = 0; i < ImageCount; ++i) {
    const auto &a = d.images[i];
    require(a.role == i && a.fd_slot == i && a.format == uint32_t(Formats[i]),
            "E_FORMAT_ROLE");
    require(a.width == d.render_width && a.height == d.render_height &&
                a.mip_levels == 1 && a.array_layers == 1 && a.usage == Usage &&
                a.tiling == VK_IMAGE_TILING_OPTIMAL &&
                a.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
                a.layout == VK_IMAGE_LAYOUT_GENERAL,
            "E_IMAGE_CONTRACT");
    require(a.memory_type < 32 && a.allocation_size &&
                a.allocation_size <= (1ull << 30) && a.bind_offset == 0,
            "E_ALLOCATION");
  }
}
} // namespace nr
