// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include "video_core/buffer_cache/buffer.h"
#include <memory>
#include <thread>
namespace Vulkan {
class Instance;class Scheduler;
class NativeSkyDiagnostic:public std::enable_shared_from_this<NativeSkyDiagnostic> {
public:
 static bool Enabled()noexcept;
 explicit NativeSkyDiagnostic(const Instance&);
 ~NativeSkyDiagnostic();
 void RecordOnce(Scheduler&,vk::Image raw,vk::Image composite,const BbNativeAsync::Frame&);
private:
 void Run(std::stop_token);
 std::array<std::unique_ptr<VideoCore::Buffer>,2> buffers;
 std::atomic<bool> recorded{false},gpu_ready{false};
 BbNativeAsync::Frame frame;uint64_t target_tick=0;
 std::string run,session;
 std::jthread writer;
};
}
