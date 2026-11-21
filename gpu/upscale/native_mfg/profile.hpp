#pragma once

#include "core.hpp"

#include <cstddef>
#include <string_view>

namespace player_core {

enum class ImageRole : uint8_t { internal, color, depth, motion, result };
enum class ArgumentKind : uint8_t { scalar, address, descriptor, phase };

struct Source {
    size_t offset, size;
    std::string_view sha;
};

struct ProgramSpec {
    const char *name;
    Source data;
    bool use_texture64;
};

struct BufferSpec {
    unsigned id;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    Source initial_data;
};

struct ImageSpec {
    unsigned id;
    uint32_t format;
    VkExtent3D extent;
    VkImageUsageFlags usage;
    ImageRole role;
};

struct TextureSpec {
    unsigned id, image;
    uint32_t format, type, handle_bits;
    std::array<uint32_t, 4> swizzle;
    std::array<uint32_t, 3> address_modes;
    uint32_t min_filter, mag_filter, mip_mode, border;
    float min_lod, max_lod, lod_bias;
    bool unnormalized;
};

struct Argument {
    uint16_t offset;
    ArgumentKind kind;
    uint8_t size;
    uint32_t resource;
    uint64_t bits;
};

struct Dispatch {
    unsigned module;
    size_t size;
    std::array<uint32_t, 3> grid, block;
    uint32_t shared;
    std::span<const Argument> parameters;
};

struct BufferFill {
    unsigned before;
    unsigned buffer;
    VkDeviceSize offset, size;
    uint32_t value;
};

struct ImageClear {
    unsigned before;
    unsigned image;
    std::array<uint32_t, 4> value;
};

struct Sequence {
    std::span<const uint16_t> dispatches;
    std::span<const BufferFill> fills;
    std::span<const ImageClear> clears;
};

struct Profile {
    std::span<const ProgramSpec> programs;
    std::span<const BufferSpec> buffers;
    std::span<const ImageSpec> images;
    std::span<const TextureSpec> textures;
    std::span<const Dispatch> dispatches;
    Sequence seed;
    std::array<Sequence, 2> pair;
    std::array<Sequence, 2> phase;
};

} // namespace player_core
