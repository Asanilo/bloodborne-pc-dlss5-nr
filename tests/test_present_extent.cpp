// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/render/present_extent.h"

#include <cassert>
#include <limits>

using namespace Vulkan::PresentSizing;

static void Expect(std::uint32_t w, std::uint32_t h, float ratio,
                   std::uint32_t expected_w, std::uint32_t expected_h) {
    const auto result = Fit(w, h, ratio);
    assert(result.width == expected_w && result.height == expected_h);
    const auto decoded = Unpack(Pack(result));
    assert(decoded.width == result.width && decoded.height == result.height);
}

int main() {
    constexpr float wide = 16.0f / 9.0f;
    Expect(1, 894, wide, 1, 1); // the reported thin-window failure: old fit was 1x0
    Expect(894, 1, 9.0f / 16.0f, 1, 1); // symmetric short-window failure: old fit was 0x1
    Expect(1, 1, wide, 1, 1);
    Expect(2, 894, wide, 2, 1);
    Expect(0, 894, wide, 0, 0);
    Expect(894, 0, wide, 0, 0);
    Expect(0, 0, wide, 0, 0);
    Expect(1920, 1080, wide, 1920, 1080); // restored surface, no one-pixel rounding regression
    Expect(2560, 1440, wide, 2560, 1440);
    Expect(2560, 1080, wide, 1920, 1080);
    Expect(1280, 720, wide, 1280, 720);
    const auto resized_destination = FitFrame({1920, 1080}, {1, 894});
    assert(resized_destination.width == 1 && resized_destination.height == 1);
    const auto short_destination = FitFrame({1080, 1920}, {894, 1});
    assert(short_destination.width == 1 && short_destination.height == 1);
    assert(!FitFrame({1920, 1080}, {0, 894}).Valid());
    assert(!FitFrame({0, 0}, {1920, 1080}).Valid());

    const float ratios[] = {wide, 9.0f / 16.0f, 4.0f / 3.0f, 32.0f / 9.0f,
        0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::max()};
    for (auto ratio : ratios) {
        for (std::uint32_t w = 0; w <= 128; ++w) {
            for (std::uint32_t h = 0; h <= 128; ++h) {
                const auto extent = Fit(w, h, ratio);
                assert(extent.Valid() == (w && h));
                assert(extent.width <= w && extent.height <= h);
            }
        }
        const auto max = std::numeric_limits<std::uint32_t>::max();
        const auto extent = Fit(max, max, ratio);
        assert(extent.Valid());
        const auto decoded = Unpack(Pack(extent));
        assert(decoded.width == extent.width && decoded.height == extent.height);
    }
}
