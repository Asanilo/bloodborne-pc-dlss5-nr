// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace Vulkan {

// Wire-free GPU readback layout shared by the C3 renderer and shader. Keep in
// lockstep with c3_frame_validate.comp: sixteen uint32 words, exactly 64 bytes.
// Dispatch the validator only while its source and output images are owned by
// the local Vulkan queue; never read an EXTERNAL-owned image.
struct alignas(16) C3FrameValidationStats {
    std::uint32_t raw_rgb_finite{};
    std::uint32_t raw_rgb_written{};
    std::uint32_t out_rgba_finite{};
    std::uint32_t out_rgba_written{};
    std::uint32_t source_rgba_finite{};
    std::uint32_t source_alpha_finite{};
    std::uint32_t alpha_bits_equal{};
    std::uint32_t source_xor{};
    std::uint32_t source_sum{};
    std::uint32_t out_xor{};
    std::uint32_t out_sum{};
    std::uint32_t visited_pixels{};
    std::uint32_t reserved0{};
    std::uint32_t raw_rgb_nan{};
    std::uint32_t out_sentinel_count{};
    std::uint32_t reserved1{};
};

static_assert(sizeof(C3FrameValidationStats) == 64);
static_assert(offsetof(C3FrameValidationStats, raw_rgb_nan) == 13 * sizeof(std::uint32_t));
static_assert(offsetof(C3FrameValidationStats, out_sentinel_count) == 14 * sizeof(std::uint32_t));

struct C3Signature32 {
    std::uint32_t xor32{};
    std::uint32_t sum32{};
};

constexpr std::uint32_t C3Mix32(std::uint32_t bits, std::uint32_t word_index) noexcept {
    std::uint32_t value = bits ^ (word_index * 0x9e3779b9u);
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

// Reference implementation used to verify shader signatures on full-audit
// frames. `rgba_words` contains the original row-major RGBA32F bit patterns;
// word order is 4 * (y * width + x) + channel, with normal uint32 wraparound.
inline C3Signature32 C3SignatureRgba32(const std::uint32_t* rgba_words, std::uint32_t width,
                                       std::uint32_t height) noexcept {
    C3Signature32 signature{};
    const std::uint64_t word_count = static_cast<std::uint64_t>(width) * height * 4;
    if (!rgba_words) return signature;
    for (std::uint64_t index = 0; index < word_count; ++index) {
        const std::uint32_t mixed = C3Mix32(rgba_words[index], static_cast<std::uint32_t>(index));
        signature.xor32 ^= mixed;
        signature.sum32 += mixed;
    }
    return signature;
}

} // namespace Vulkan
