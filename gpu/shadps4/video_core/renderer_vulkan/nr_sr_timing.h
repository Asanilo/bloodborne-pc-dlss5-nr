// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <optional>
#include "common/types.h"
#include "sr_timing_math.h"
#include "video_core/renderer_vulkan/vk_common.h"
namespace Vulkan {
class Instance;
class Scheduler;
// Local telemetry only: no Helper/wire/image ownership or renderer wait changes.
class SrCommandTiming : public std::enable_shared_from_this<SrCommandTiming> {
public:
    static bool Enabled() noexcept;
    explicit SrCommandTiming(const Instance&);
    std::optional<unsigned> Begin(vk::CommandBuffer,u64 source_game_frame);
    void End(vk::CommandBuffer,Scheduler&,unsigned slot,u64 source_game_frame,bool record_success);
private:
    void NotMeasured(u64 source,const char* reason);
    vk::Device device;
    vk::UniqueQueryPool pool;
    SrTimingMath::Slots slots;
    unsigned valid_bits=0;
    double period_ns=0;
};
} // namespace Vulkan
