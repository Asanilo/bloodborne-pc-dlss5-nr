// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace Vulkan {
// One persistent CPU service thread; never a queue of game frames. The caller
// joins each transaction before admitting another. Move-only jobs own their data.
class DlssgSerialWorker {
    std::mutex mutex;
    std::condition_variable cv;
    std::packaged_task<void()> job;
    bool stopping = false;
    std::thread thread{[this] {
        for (;;) {
            std::packaged_task<void()> next;
            {
                std::unique_lock lock{mutex};
                cv.wait(lock, [this] { return stopping || job.valid(); });
                if (!job.valid()) return;
                next = std::move(job);
            }
            next();
        }
    }};
public:
    ~DlssgSerialWorker() {
        { std::scoped_lock lock{mutex}; stopping = true; }
        cv.notify_one();
        thread.join();
    }
    template<class F> void Submit(F&& work) {
        {
            std::scoped_lock lock{mutex};
            if (stopping || job.valid()) throw std::runtime_error("DLSSG service queue occupied");
            job = std::packaged_task<void()>(std::forward<F>(work));
        }
        cv.notify_one();
    }
};
} // namespace Vulkan
