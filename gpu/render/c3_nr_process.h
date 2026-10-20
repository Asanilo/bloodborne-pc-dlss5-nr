#pragma once

// C3 helper process lifecycle. Vulkan resource import remains owned by the GPU thread;
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

class C3NrProcess {
public:
    C3NrProcess() = default;
    ~C3NrProcess();
    C3NrProcess(const C3NrProcess&) = delete;
    C3NrProcess& operator=(const C3NrProcess&) = delete;

    // Starts an off-render initialization worker. Returns immediately. The UUID arguments
    // must come from the active game's VkPhysicalDevice; dimensions are fixed by C3's profile.
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
    uint64_t ElapsedSinceStartMs() const noexcept;
    bool ExporterIdentityCurrent() const noexcept;
    const std::string& Session() const noexcept { return session; }
    const std::string& RunDir() const noexcept { return run_dir; }
    const std::string& WindowsRunDir() const noexcept { return windows_run_dir; }
    const std::string& Failure() const noexcept { return failure; }
    const std::string& Mode() const noexcept { return mode; }
    unsigned FrameCount() const noexcept { return frame_count; }
    const std::string& DiagnosticCase() const noexcept { return diagnostic_case; }

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
    std::atomic<int64_t> start_steady_ms{0};
    std::mutex state_mutex;
    std::string failure;
    std::string session;
    std::string mode;
    std::string diagnostic_case;
    unsigned frame_count = 0;
    std::string run_dir;
    std::string windows_run_dir;
    std::string helper_exe;
    std::jthread setup_thread;
};

} // namespace Vulkan
