// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace Vulkan::FgQuality {

// Optional presentation-quality fallback, observed only at real scene boundaries.
// Two degrees per real frame is about 120 degrees/sec at 60 real FPS. The
// per-frame criterion intentionally tightens at lower real FPS, where the
// interpolator has a larger spatial gap. This never changes a game's/NR's clock.
class TurnGuard {
public:
    struct Decision { bool suppress = false; double degrees = 0.0; };
    void Reset() { previous_valid = active = false; quiet = 0; }

    Decision Observe(const std::array<float, 3>& forward, bool history_reset) {
        if (history_reset) Reset();
        double length2 = 0.0;
        for (float value : forward) length2 += double(value) * value;
        if (!std::isfinite(length2) || length2 < 1e-12) {
            Reset();
            return {true, 0.0};
        }
        std::array<double, 3> unit{};
        const double length = std::sqrt(length2);
        for (unsigned i = 0; i < 3; ++i) unit[i] = forward[i] / length;
        double degrees = 0.0;
        if (previous_valid) {
            double dot = 0.0;
            for (unsigned i = 0; i < 3; ++i) dot += previous[i] * unit[i];
            degrees = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / Pi;
        }
        previous = unit;
        previous_valid = true;
        if (degrees > EnterDegrees) { active = true; quiet = 0; }
        const bool suppress = active;
        if (active) {
            quiet = degrees < ReleaseDegrees ? quiet + 1 : 0;
            if (quiet >= QuietFrames) { active = false; quiet = 0; }
        }
        return {suppress, degrees};
    }

    static constexpr double EnterDegrees = 2.0;
    static constexpr double ReleaseDegrees = 1.0;
    static constexpr unsigned QuietFrames = 2;

private:
    static constexpr double Pi = 3.14159265358979323846;
    std::array<double, 3> previous{};
    bool previous_valid = false, active = false;
    unsigned quiet = 0;
};
}
