// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/upscale/frame_generation_turn_guard.h"
#include <cassert>
#include <limits>

using Vulkan::FgQuality::TurnGuard;
static std::array<float, 3> Yaw(double degrees, double scale = 1.0) {
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    return {float(std::sin(radians) * scale), 0.0f, float(std::cos(radians) * scale)};
}
int main() {
    TurnGuard guard;
    assert(!guard.Observe(Yaw(0), false).suppress);
    for (unsigned n = 1; n < 300; ++n)
        assert(!guard.Observe(Yaw(n * 0.5), false).suppress); // normal pan, including angle wrap
    assert(guard.Observe(Yaw(160), false).suppress); // rapid turn
    assert(guard.Observe(Yaw(160.4), false).suppress);
    assert(guard.Observe(Yaw(160.8), false).suppress);
    assert(!guard.Observe(Yaw(161.2), false).suppress); // two quiet real scenes, then resume
    assert(guard.Observe(Yaw(170), false).suppress);
    for (unsigned n = 1; n <= 10; ++n)
        assert(guard.Observe(Yaw(170 + n * 1.25), false).suppress); // hysteresis prevents flapping
    guard.Reset();
    assert(!guard.Observe(Yaw(179.5), false).suppress);
    const auto wrap = guard.Observe(Yaw(-179.5), false);
    assert(!wrap.suppress && std::abs(wrap.degrees - 1.0) < 1e-4);
    assert(!guard.Observe(Yaw(-179.5, 1000), false).suppress); // normalize camera axes
    assert(guard.Observe(Yaw(0), false).suppress);
    assert(!guard.Observe(Yaw(100), true).suppress); // existing history reset establishes a new base
    for (auto value : {0.0f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()}) {
        assert(guard.Observe({value, value, value}, false).suppress);
        assert(!guard.Observe(Yaw(0), false).suppress);
    }
    assert(guard.Observe(Yaw(180), false).suppress);
    guard.Reset(); // disabling FG must discard the old camera rather than compare across an off interval
    assert(!guard.Observe(Yaw(0), false).suppress);
}
