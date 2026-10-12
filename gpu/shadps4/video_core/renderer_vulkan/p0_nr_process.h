#pragma once

// P0 helper process lifecycle. Vulkan resource import remains owned by the GPU thread;
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
bool P0Enabled() noexcept;

class P0NrProcess {
public:
    P0NrProcess() = default;
    ~P0NrProcess();
    P0NrProcess(const P0NrProcess&) = delete;
    P0NrProcess& operator=(const P0NrProcess&) = delete;

    // Starts an off-render initialization worker. Returns immediately. The UUID arguments
    // must come from the active game's VkPhysicalDevice; dimensions are fixed by P0's profile.
    bool Start(const std::string& device_uuid, const std::string& driver_uuid,
               unsigned width, unsigned height, unsigned session_epoch = 0);
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
    std::string Failure() const;
    const std::string& Mode() const noexcept { return mode; }
    const std::string& ExecutionMode() const noexcept { return execution_mode; }
    unsigned FrameCount() const noexcept { return frame_count; }
    const std::string& DiagnosticCase() const noexcept { return diagnostic_case; }

    // Controller only: normal shutdown keeps this object alive until the monitor observes reap.
    bool ReapedNormally() const noexcept;
    static unsigned GlobalSpawnCount() noexcept;
    bool Spawned() const noexcept { return spawned.load(std::memory_order_acquire); }
    void RequestAbort(const std::string& reason) noexcept;
    bool SignalVerifiedPeer(int signal_number) noexcept;
    unsigned ServiceWallSeconds() const noexcept { return service_wall_seconds; }
    unsigned SessionEpoch() const noexcept { return session_epoch; }
private:
    void SetFailure(std::string message) noexcept;

    std::atomic<bool> started{false};
    std::atomic<bool> spawned{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> completed{false};
    std::atomic<int> exit_status{-1};
    std::atomic<pid_t> child_pid{-1};
    std::atomic<pid_t> exporter_pid{-1};
    std::atomic<uint64_t> exporter_start_ticks{0};
    std::atomic<int64_t> start_steady_ms{0};
    mutable std::mutex state_mutex;
    std::string failure;
    std::string session;
    std::string mode;
    std::string diagnostic_case;
    std::string execution_mode;
    unsigned frame_count = 0;
    unsigned service_wall_seconds = 900;
    unsigned session_epoch = 0;
    std::string run_dir;
    std::string windows_run_dir;
    std::string helper_exe;
    std::jthread setup_thread;
};

} // namespace Vulkan
