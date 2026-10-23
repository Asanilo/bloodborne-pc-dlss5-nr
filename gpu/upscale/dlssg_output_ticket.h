// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include "textures/image.h"

namespace Vulkan {
// A bounded raw-output resource, not an additional queued game frame. The pool
// may reuse it only after both the consumer writer and Present encoder release
// their ticket references. The latter reference lives through its actual GPU tick.
struct DlssgRawOutput {
    VideoCore::HostImage image;
    vk::Format format{};
    u32 width = 1920, height = 1080;
};
struct DlssgOutputTicket {
    std::shared_ptr<DlssgRawOutput> raw;
    std::shared_future<nlohmann::json> ready; // Helper + Native copy/release + ACK complete.
    u64 source = 0, previous_source = 0, display = 0, capture_cpu_ns = 0;
    u64 guide_record_cpu_ns = 0, producer_submit_cpu_ns = 0;
    std::shared_ptr<std::atomic<bool>> recorded = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<bool>> eligible;
};
} // namespace Vulkan
