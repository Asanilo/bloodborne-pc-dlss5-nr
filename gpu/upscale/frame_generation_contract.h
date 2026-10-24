// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Vulkan::FgContract {
// A pending native capture intentionally has no LDR image yet. Two null handles comparing
// equal must never admit a Vulkan dispatch (including the first frame after loading).
template<class Image>
constexpr bool CanInterpolate(bool wanted, bool captured, Image color, Image scene_color,
                              bool context) {
    return wanted && captured && bool(color) && bool(scene_color) &&
           color == scene_color && context;
}

constexpr bool SameRealSource(std::uint64_t captured, std::uint64_t presented) {
    return captured && presented && captured == presented;
}

template<class Image>
constexpr bool CanStampDisplay(bool wanted, bool sampled_ui, Image ui, Image captured_color,
                               std::uint64_t captured_source, std::uint64_t source,
                               std::uint32_t captured_width, std::uint32_t captured_height,
                               std::uint32_t width, std::uint32_t height) {
    return CanInterpolate(wanted, sampled_ui, ui, captured_color, true) &&
           SameRealSource(captured_source, source) && width && height &&
           width == captured_width && height == captured_height;
}

// Content identity of an actually recorded display copy, independent of NR.
// This is not GPU completion: the existing submission waits still own that proof.
struct DisplayStamp {
    std::uint64_t source = 0, address = 0, producer_tick = 0, generation = 0;
    std::uint32_t width = 0, height = 0;
    constexpr std::uint64_t Consume(std::uint64_t actual_address,
                                   std::uint64_t gpu_frame_at_prepare,
                                   std::uint64_t prepare_tick,
                                   std::uint64_t actual_generation,
                                   std::uint32_t actual_width,
                                   std::uint32_t actual_height) {
        const auto recorded = *this;
        *this = {}; // Wrong/stale destinations cannot leave a stamp for another flip.
        return recorded.source && recorded.address && recorded.producer_tick &&
                       recorded.generation && recorded.width && recorded.height &&
                       actual_address == recorded.address &&
                       gpu_frame_at_prepare > recorded.source &&
                       gpu_frame_at_prepare - recorded.source == 1 &&
                       prepare_tick >= recorded.producer_tick &&
                       actual_generation == recorded.generation &&
                       actual_width == recorded.width && actual_height == recorded.height
                   ? recorded.source : 0;
    }
};
} // namespace Vulkan::FgContract
