#pragma once

// C1 helper process lifecycle. Vulkan resource import remains owned by the GPU thread;
// this object only prepares files and starts the private Wine child with posix_spawn.
// The feature is opt-in and must never block a rendering hook during initialization.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <stop_token>
#include <sys/types.h>

namespace Vulkan {

class C1NoopProcess {
public:
    C1NoopProcess() = default;
    ~C1NoopProcess();
    C1NoopProcess(const C1NoopProcess&) = delete;
    C1NoopProcess& operator=(const C1NoopProcess&) = delete;

    // Starts an off-render initialization worker. Returns immediately. The UUID arguments
    // must come from the active game's VkPhysicalDevice; dimensions are validated by C1.
    bool Start(const std::string& device_uuid, const std::string& driver_uuid,
               unsigned width, unsigned height);
    bool Enabled() const noexcept;
    // True means only exact PE/run/ancestor identity was found and native.pid published.
    // Vulkan baseline/import/FD retirement are separate bridge states and are not implied.
    bool SetupReady() const noexcept;
    bool SetupFailed() const noexcept;
    bool Completed() const noexcept;
    int ExitStatus() const noexcept;
    pid_t ChildPid() const noexcept;
    pid_t ExporterPid() const noexcept;
    uint64_t ExporterStartTicks() const noexcept;
    bool ExporterIdentityCurrent() const noexcept;
    const std::string& Session() const noexcept { return session; }
    const std::string& RunDir() const noexcept { return run_dir; }
    const std::string& WindowsRunDir() const noexcept { return windows_run_dir; }
    const std::string& Failure() const noexcept { return failure; }

private:
    void SetFailure(std::string message) noexcept;

    std::atomic<bool> started{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> completed{false};
    std::atomic<int> exit_status{-1};
    std::atomic<pid_t> child_pid{-1};
    std::atomic<pid_t> exporter_pid{-1};
    std::atomic<uint64_t> exporter_start_ticks{0};
    std::mutex state_mutex;
    std::string failure;
    std::string session;
    std::string run_dir;
    std::string windows_run_dir;
    std::string helper_exe;
    std::jthread setup_thread;
};

} // namespace Vulkan
