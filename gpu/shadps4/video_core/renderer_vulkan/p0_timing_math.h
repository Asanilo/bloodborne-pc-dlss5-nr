// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace Vulkan::P0Timing {
// The existing scheduler callback supplies actual target-tick completion. Neither
// query availability nor a successful Evaluate record alone permits slot reuse.
class Slots {
public:
    static constexpr unsigned Count = 4;
    std::optional<unsigned> Claim() noexcept {
        for (unsigned i=0; i<Count; ++i) {
            bool free=false;
            if (busy[i].compare_exchange_strong(free,true,std::memory_order_acq_rel)) return i;
        }
        return {};
    }
    bool Complete(unsigned i,bool gpu_tick_complete) noexcept {
        if (i>=Count || !gpu_tick_complete) return false;
        return busy[i].exchange(false,std::memory_order_acq_rel);
    }
private:
    std::array<std::atomic<bool>,Count> busy{};
};
inline std::optional<double> DurationMs(int result, const std::array<uint64_t,4>& query,
                                      unsigned bits, double period_ns, bool tick_complete) noexcept {
    // Query layout: timestamp0, availability0, timestamp1, availability1.
    // result==0 is VK_SUCCESS. NO_WAIT/not-ready/error never becomes a duration.
    if (!tick_complete || result!=0 || !query[1] || !query[3] || bits==0 || bits>64 ||
        !std::isfinite(period_ns) || period_ns<=0) return {};
    const auto mask=bits==64 ? UINT64_MAX : ((uint64_t{1}<<bits)-1);
    const double ms=double((query[2]-query[0])&mask)*period_ns/1e6;
    return std::isfinite(ms) ? std::optional<double>{ms} : std::nullopt;
}
inline bool PublishOrder(int64_t before,int64_t after,int64_t complete,int64_t selection) noexcept {
    return before<=after && after<=complete && complete<=selection;
}
} // namespace Vulkan::P0Timing
