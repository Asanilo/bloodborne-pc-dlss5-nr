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

// The HDR prepass precedes this frame's guide capture. Accept only the current
// or immediately previous real source in exactly the same resource domain.
// This is content compatibility, never a completion or NR-history proof.
constexpr bool CompatibleGuideDomain(bool enabled, bool guides,
                                    std::uint64_t accepted, std::uint64_t source,
                                    std::uint32_t rw, std::uint32_t rh,
                                    std::uint32_t ow, std::uint32_t oh,
                                    std::uint32_t actual_rw, std::uint32_t actual_rh,
                                    std::uint32_t actual_ow, std::uint32_t actual_oh) {
    return enabled && guides && accepted && source >= accepted && source-accepted <= 1 &&
           rw && rh && ow && oh && rw==actual_rw && rh==actual_rh &&
           ow==actual_ow && oh==actual_oh;
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

constexpr DisplayStamp BindDisplay(const DisplayStamp& pending, std::uint64_t address,
                                   std::uint64_t gpu_frame_at_draw, std::uint64_t draw_tick,
                                   std::uint64_t image_generation,
                                   std::uint32_t width, std::uint32_t height) {
    if (!pending.source || pending.address!=address || !pending.producer_tick ||
        !image_generation || !width || !height || pending.width!=width || pending.height!=height ||
        gpu_frame_at_draw<=pending.source || gpu_frame_at_draw-pending.source!=1 ||
        draw_tick<pending.producer_tick) return {};
    return {pending.source,address,draw_tick,image_generation,width,height};
}
} // namespace Vulkan::FgContract
