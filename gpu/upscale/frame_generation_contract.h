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
} // namespace Vulkan::FgContract
