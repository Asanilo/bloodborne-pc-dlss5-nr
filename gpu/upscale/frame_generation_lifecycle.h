// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>

namespace Vulkan::FgLifecycle {
// The window requests retirement; only the owning renderer/presenter publish completion.
// These flags never substitute for the scheduler's actual GPU completion waits.
inline std::atomic<bool> enabled{false}, exit_requested{false}, draw_retired{false}, present_retired{false};
inline std::atomic<bool> held{false};
inline bool Pending() {
    return enabled.load() && exit_requested.load() && !draw_retired.load();
}
inline bool Complete() {
    return !enabled.load() || (draw_retired.load() && present_retired.load());
}
}
