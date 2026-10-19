// SPDX-License-Identifier: GPL-2.0-or-later
#include "p0_status.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace BbP0 {
namespace {
// One word makes enable and revision a coherent command across UI/render threads.
std::atomic<uint64_t> requested{0};
std::atomic<bool> permitted{false};
std::atomic<bool> initialized{false};
std::mutex snapshot_mutex;
Snapshot snapshot;
}
const char* StateName(State value) noexcept {
    switch (value) {
    case State::Disabled: return "Disabled";
    case State::Starting: return "Starting";
    case State::Ready: return "Ready";
    case State::Active: return "Active";
    case State::Idle: return "Idle";
    case State::Draining: return "Draining";
    case State::Quarantined: return "Quarantined";
    case State::Failed: return "Failed";
    }
    return "Invalid";
}
void InitializeRequest(bool opt_in, bool auto_enable) noexcept {
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    permitted.store(opt_in, std::memory_order_release);
    if (opt_in && auto_enable) RequestEnable(true);
}
void RequestEnable(bool enabled) noexcept {
    if (!permitted.load(std::memory_order_acquire)) return;
    auto previous = requested.load(std::memory_order_acquire);
    while (bool(previous & 1u) != enabled) {
        const uint64_t next = ((previous & ~uint64_t{1}) + 2u) | uint64_t(enabled);
        if (requested.compare_exchange_weak(previous, next,
                std::memory_order_release, std::memory_order_acquire)) return;
    }
}
EnableRequest ReadRequest() noexcept {
    const auto value = requested.load(std::memory_order_acquire);
    return {bool(value & 1u), value >> 1};
}
void Publish(Snapshot value) {
    std::scoped_lock lock{snapshot_mutex};
    snapshot = std::move(value);
}
Snapshot ReadSnapshot() {
    std::scoped_lock lock{snapshot_mutex};
    return snapshot;
}
} // namespace BbP0
