// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_async_state.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace BbNativeAsync {
// Only local numeric metadata. No Vulkan/Scheduler pointer, Helper, IPC or polling loop.
class Owner {
public:
 Owner();~Owner();
 bool TryPost(const std::shared_ptr<Node>&);
 void Signal() noexcept {std::scoped_lock lock(mutex);cv.notify_one();}
 bool TryObserve(const Frame&,const CompletionNotice&);
 void Stop();
 uint64_t ReadyCount()const noexcept{return ready_count.load();}
 uint64_t LatestReadySource()const noexcept{return latest_source.load();}
 uint64_t ObservedNoticeCount()const noexcept{return observed.load();}
private:
 void Run(std::stop_token);
 std::mutex mutex;std::condition_variable_any cv;
 std::array<std::shared_ptr<Node>,NodeCount> pending{};
 std::atomic<uint64_t> ready_count{0},latest_source{0},observed{0};
 bool stopped=false;std::jthread thread;
};
}
