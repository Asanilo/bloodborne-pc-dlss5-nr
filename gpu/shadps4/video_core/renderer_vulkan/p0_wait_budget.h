// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace Vulkan::P0Wait {
enum class BudgetKind { CompletedFast, Expired, Finite, InvalidRange };
struct Budget { BudgetKind kind; uint64_t nanoseconds; };
// Pure CPU arithmetic: no signed subtraction overflow, no Vulkan UINT64_MAX wait.
inline Budget Remaining(int64_t deadline_ns,int64_t now_ns,bool actual_tick_complete=false) noexcept {
    if(actual_tick_complete) return {BudgetKind::CompletedFast,0};
    if(deadline_ns<=now_ns) return {BudgetKind::Expired,0};
    const uint64_t delta=uint64_t(deadline_ns)-uint64_t(now_ns);
    if(delta>uint64_t(std::numeric_limits<int64_t>::max())) return {BudgetKind::InvalidRange,0};
    return {BudgetKind::Finite,delta};
}
inline int64_t EpochNs(std::chrono::steady_clock::time_point value) {
    const long double ns=std::chrono::duration<long double,std::nano>(value.time_since_epoch()).count();
    if(!std::isfinite(ns)||ns<static_cast<long double>(std::numeric_limits<int64_t>::min())||
       ns>static_cast<long double>(std::numeric_limits<int64_t>::max()))
        throw std::runtime_error("P0 timeline wait clock nanosecond range invalid");
    return static_cast<int64_t>(ns);
}
inline constexpr int32_t Success=0, Timeout=2; // statically checked against vk::Result in caller
// Success alone is not proof of the requested target. No other API result is completion.
enum class Outcome { Completed, Expired, InvalidSuccess, Error };
inline Outcome Classify(int32_t result,bool actual_tick_complete,bool before_deadline) noexcept {
    if(result==Timeout) return Outcome::Expired;
    if(result!=Success) return Outcome::Error;
    if(!actual_tick_complete) return Outcome::InvalidSuccess;
    return before_deadline?Outcome::Completed:Outcome::Expired;
}
} // namespace Vulkan::P0Wait
