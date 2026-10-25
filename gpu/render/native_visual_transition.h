// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vulkan {
// Logical display transition only. No GPU ownership, dispatch, or completion
// proof is implied by beginning, clearing, or reaching this deadline.
struct VisualTransition {
    static constexpr double DurationMs = 33.34;
    bool fade_new_only = false; // display-only raw -> newly valid target, never stored
    bool active = false;
    uint64_t source = 0;
    double ms = 0;

    void Clear() noexcept { active = false; source = 0; ms = 0; }
    bool Begin(uint64_t start_source, double clock) noexcept {
        Clear();
        if (!start_source || !std::isfinite(clock)) return false;
        active = true; source = start_source; ms = clock;
        return true;
    }
    bool Due(uint64_t current_source, double clock) const noexcept {
        if (!active || !source || !current_source || current_source < source ||
            !std::isfinite(ms) || !std::isfinite(clock) || clock < ms) return true;
        return current_source - source >= 2 || clock - ms >= DurationMs;
    }
    float Weight(uint64_t current_source, double clock) const noexcept {
        if (Due(current_source, clock)) return 1.f;
        const double frames = (double(current_source - source) + 1.) / 3.;
        return float(std::min(1., std::max(frames, (clock - ms) / DurationMs)));
    }
};

// Immutable history dispatch slots only: conversion, Return, copy and external
// SR/merge reads are separate commands that still need their own TrackUse.
// An existing target requires at most ONE finalize, shared by expiry and early
// replacement. new_links includes Capture and its catch-up Advance passes.
constexpr unsigned RequiredTransitionPasses(bool existing_target, unsigned new_links,
                                             bool stats) noexcept {
    return 1u + unsigned(existing_target) + unsigned(existing_target) + new_links +
           1u + unsigned(stats);
}
static_assert(RequiredTransitionPasses(true, 5, true) == 10);
} // namespace Vulkan
