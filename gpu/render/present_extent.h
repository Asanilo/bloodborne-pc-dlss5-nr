// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vulkan::PresentSizing {

struct Extent {
    std::uint32_t width = 0, height = 0;
    constexpr bool Valid() const { return width && height; }
};

// A zero surface is suspended. A positive surface can be smaller than one aspect-ratio pixel:
// e.g. a 1x894 niri window previously letterboxed to 1x0 and caused vmaCreateImage to fail.
inline Extent Fit(std::uint32_t width, std::uint32_t height, float ratio) {
    if (!width || !height) return {};
    if (!std::isfinite(ratio) || ratio <= 0.0f) ratio = 16.0f / 9.0f;
    const auto bounded = [](float value, std::uint32_t upper) {
        // Clamp before conversion, including float rounding of UINT32_MAX to 2^32.
        return static_cast<std::uint32_t>(std::clamp(double(value), 1.0, double(upper)));
    };
    if (float(width) / float(height) > ratio) {
        width = bounded(float(height) * ratio, width);
    } else {
        height = bounded(float(width) / ratio, height);
    }
    return {width, height};
}

inline Extent FitFrame(Extent frame, Extent target) {
    if (!frame.Valid()) return {};
    return Fit(target.width, target.height, float(frame.width) / float(frame.height));
}

constexpr std::uint64_t Pack(Extent extent) {
    return (std::uint64_t(extent.width) << 32) | extent.height;
}
constexpr Extent Unpack(std::uint64_t value) {
    return {std::uint32_t(value >> 32), std::uint32_t(value)};
}

} // namespace Vulkan::PresentSizing
