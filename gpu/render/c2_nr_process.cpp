// SPDX-License-Identifier: GPL-2.0-or-later
// C2-only private helper launcher. The renderer never forks; posix_spawn is called by this
// bounded setup worker after every argv/env/config value has been constructed in the parent.

#include "c2_nr_process.h"

#include <cerrno>
#include <algorithm>
#include <array>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace Vulkan {
namespace {

constexpr auto SetupLimit = std::chrono::seconds(60);

std::string Env(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}

bool IsAbsolute(const std::string& value) {
    return !value.empty() && value.front() == '/';
}

bool IsUuid(const std::string& value) {
    if (value.size() != 32) return false;
    for (char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool IsSession(const std::string& value) {
    if (value.empty() || value.size() > 63 || value.size() >= 80) return false;
    for (char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

std::string NormalizeWinePath(std::string value) {
    for (char& c : value) if (c == '\\') c = '/';
    for (size_t pos = 0; (pos = value.find("Z:", pos)) != std::string::npos;) {
        value.erase(pos, 2);
    }
    return value;
}

std::string ReadFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), {}) : std::string{};
}

std::string JsonStringField(const std::string& json, std::string_view key) {
    if (json.empty() || json.size() > 64 * 1024)
        throw std::runtime_error("pid-request exceeds 64 KiB or is empty");
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t first = json.find(token);
    if (first == std::string::npos || json.find(token, first + token.size()) != std::string::npos)
        throw std::runtime_error("pid-request missing/duplicate " + std::string(key));
    size_t pos = first + token.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos == json.size() || json[pos++] != ':') throw std::runtime_error("invalid pid-request JSON");
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) ++pos;
    if (pos == json.size() || json[pos++] != '"') throw std::runtime_error("pid-request string expected");
    std::string value;
    bool closed = false;
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') { closed = true; break; }
        if (c == '\\') {
            if (pos == json.size()) break;
            const char escaped = json[pos++];
            if (escaped == '"' || escaped == '\\' || escaped == '/') value.push_back(escaped);
            else if (escaped == 'b') value.push_back('\b');
            else if (escaped == 'f') value.push_back('\f');
            else if (escaped == 'n') value.push_back('\n');
            else if (escaped == 'r') value.push_back('\r');
            else if (escaped == 't') value.push_back('\t');
            else throw std::runtime_error("unsupported pid-request escape");
        } else {
            value.push_back(c);
        }
    }
    if (!closed) throw std::runtime_error("unterminated pid-request string");
    return value;
}

pid_t ParentPid(pid_t pid) {
    std::istringstream input(ReadFile("/proc/" + std::to_string(pid) + "/status"));
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("PPid:", 0) == 0) {
            try { return static_cast<pid_t>(std::stol(line.substr(5))); }
            catch (...) { return 0; }
        }
    }
    return 0;
}

bool Descendant(pid_t pid, pid_t ancestor) {
    for (unsigned depth = 0; pid > 1 && depth < 128; ++depth) {
        if (pid == ancestor) return true;
        pid = ParentPid(pid);
    }
    return false;
}

uint64_t StartTicks(pid_t pid) {
    const std::string line = ReadFile("/proc/" + std::to_string(pid) + "/stat");
    const size_t close = line.rfind(')');
    if (close == std::string::npos) return 0;
    std::istringstream fields(line.substr(close + 2));
    std::string field;
    for (unsigned i = 0; i <= 19; ++i) {
        if (!(fields >> field)) return 0;
        if (i == 19) {
            try { return std::stoull(field); } catch (...) { return 0; }
        }
    }
    return 0;
}

pid_t FindExporter(const std::string& helper, const std::string& run_dir, pid_t launcher,
                   const std::string& mode, const std::string& session) {
    const std::string expected = NormalizeWinePath(helper);
    const std::string basename = std::filesystem::path(expected).filename().string();
    std::vector<pid_t> matches;
    for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
        const std::string name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return c >= '0' && c <= '9';
            })) continue;
        const pid_t pid = static_cast<pid_t>(std::strtol(name.c_str(), nullptr, 10));
        if (pid <= 1 || pid == getpid() || !Descendant(pid, getpid()) ||
            !Descendant(pid, launcher)) continue;
        const std::string raw_cmd = ReadFile("/proc/" + name + "/cmdline");
        std::vector<std::string> argv;
        size_t begin = 0;
        for (size_t i = 0; i <= raw_cmd.size(); ++i) {
            if (i == raw_cmd.size() || raw_cmd[i] == '\0') {
                if (i > begin) argv.push_back(NormalizeWinePath(raw_cmd.substr(begin, i - begin)));
                begin = i + 1;
            }
        }
        if (argv.empty() || (argv[0] != expected && argv[0] != basename)) continue;
        const std::string normalized_run = NormalizeWinePath(run_dir);
        if (std::find(argv.begin(), argv.end(), mode) == argv.end() ||
            std::find(argv.begin(), argv.end(), session) == argv.end() ||
            std::find(argv.begin(), argv.end(), normalized_run) == argv.end()) continue;
        std::array<char, 4096> link{};
        const ssize_t len = readlink(("/proc/" + name + "/exe").c_str(), link.data(), link.size() - 1);
        if (len <= 0) continue;
        const std::string actual_exe(link.data(), static_cast<size_t>(len));
        if (actual_exe.find("wine") == std::string::npos ||
            actual_exe.find("wineserver") != std::string::npos) continue;
        const std::string maps = ReadFile("/proc/" + name + "/maps");
        if (maps.find(expected) == std::string::npos) continue;
        matches.push_back(pid);
    }
    return matches.size() == 1 ? matches.front() : -1;
}

void AtomicWrite(const std::string& path, const std::string& contents) {
    const std::string temporary = path + ".tmp-" + std::to_string(getpid());
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error("open private config: " + std::string(std::strerror(errno)));
    size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t n = write(fd, contents.data() + offset, contents.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            const int error = errno;
            close(fd);
            unlink(temporary.c_str());
            throw std::runtime_error("write private config: " + std::string(std::strerror(error)));
        }
        offset += static_cast<size_t>(n);
    }
    if (fsync(fd) != 0) {
        const int error = errno;
        close(fd);
        unlink(temporary.c_str());
        throw std::runtime_error("fsync private config: " + std::string(std::strerror(error)));
    }
    close(fd);
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        const int error = errno;
        unlink(temporary.c_str());
        throw std::runtime_error("publish private config: " + std::string(std::strerror(error)));
    }
}

std::vector<std::string> BuildChildEnvironment(const std::string& wineprefix,
                                              const std::string& overrides,
                                              const std::string& dllpath,
                                              const std::string& debug) {
    const std::array<std::pair<std::string, std::string>, 6> set_values{{
        {"WINELOADERNOEXEC", "1"}, {"WINEPREFIX", wineprefix},
        {"WINEDLLOVERRIDES", overrides}, {"WINEDLLPATH", dllpath}, {"WINEDEBUG", debug},
        {"DXVK_ENABLE_NVAPI", "1"}}};
    std::vector<std::string> values;
    for (char** entry = environ; entry && *entry; ++entry) {
        std::string value(*entry);
        const size_t equals = value.find('=');
        const std::string key = value.substr(0, equals);
        if (std::any_of(set_values.begin(), set_values.end(), [&](const auto& item) {
                return item.first == key;
            })) continue;
        values.emplace_back(std::move(value));
    }
    for (const auto& [key, value] : set_values) values.emplace_back(key + "=" + value);
    return values;
}

} // namespace

C2NrProcess::~C2NrProcess() {
    if (setup_thread.joinable()) {
        setup_thread.request_stop();
        setup_thread.join(); // worker only performs bounded filesystem setup + posix_spawn.
    }
}

bool C2NrProcess::Enabled() const noexcept {
    const char* mode = std::getenv("BB_NR_C2_MODE");
    return mode && (std::strcmp(mode, "single") == 0 ||
                    std::strcmp(mode, "skipEvaluate") == 0);
}

bool C2NrProcess::SetupReady() const noexcept {
    return ready.load(std::memory_order_acquire);
}

bool C2NrProcess::SetupFailed() const noexcept {
    return failed.load(std::memory_order_acquire);
}

pid_t C2NrProcess::ChildPid() const noexcept {
    return child_pid.load(std::memory_order_acquire);
}

pid_t C2NrProcess::ExporterPid() const noexcept {
    return exporter_pid.load(std::memory_order_acquire);
}

uint64_t C2NrProcess::ExporterStartTicks() const noexcept {
    return exporter_start_ticks.load(std::memory_order_acquire);
}

uint64_t C2NrProcess::ElapsedSinceStartMs() const noexcept {
    const auto started = start_steady_ms.load(std::memory_order_acquire);
    if (!started) return 0;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return now > started ? static_cast<uint64_t>(now - started) : 0;
}

bool C2NrProcess::ExporterIdentityCurrent() const noexcept {
    const pid_t exporter = ExporterPid();
    const pid_t launcher = ChildPid();
    const uint64_t recorded_ticks = ExporterStartTicks();
    if (exporter <= 1 || launcher <= 1 || !recorded_ticks) return false;
    try {
        if (StartTicks(exporter) != recorded_ticks || !Descendant(exporter, getpid()) ||
            !Descendant(exporter, launcher)) return false;
        const std::string proc = "/proc/" + std::to_string(exporter);
        const std::string raw_cmd = ReadFile(proc + "/cmdline");
        std::vector<std::string> argv;
        size_t begin = 0;
        for (size_t i = 0; i <= raw_cmd.size(); ++i) {
            if (i == raw_cmd.size() || raw_cmd[i] == '\0') {
                if (i > begin) argv.push_back(NormalizeWinePath(raw_cmd.substr(begin, i - begin)));
                begin = i + 1;
            }
        }
        const std::string expected = NormalizeWinePath(helper_exe);
        const std::string basename = std::filesystem::path(expected).filename().string();
        if (argv.empty() || (argv[0] != expected && argv[0] != basename) ||
            std::find(argv.begin(), argv.end(), mode) == argv.end() ||
            std::find(argv.begin(), argv.end(), session) == argv.end() ||
            std::find(argv.begin(), argv.end(), NormalizeWinePath(run_dir)) == argv.end() ||
            ReadFile(proc + "/maps").find(expected) == std::string::npos) return false;
        std::array<char, 4096> link{};
        const ssize_t len = readlink((proc + "/exe").c_str(), link.data(), link.size() - 1);
        if (len <= 0) return false;
        const std::string actual(link.data(), static_cast<size_t>(len));
        return actual.find("wine") != std::string::npos &&
               actual.find("wineserver") == std::string::npos;
    } catch (...) {
        return false;
    }
}

bool C2NrProcess::Completed() const noexcept {
    return completed.load(std::memory_order_acquire);
}

int C2NrProcess::ExitStatus() const noexcept {
    return exit_status.load(std::memory_order_acquire);
}

void C2NrProcess::SetFailure(std::string message) noexcept {
    {
        std::lock_guard lock(state_mutex);
        failure = std::move(message);
    }
    failed.store(true, std::memory_order_release);
}

bool C2NrProcess::Start(const std::string& device_uuid, const std::string& driver_uuid,
                          unsigned width, unsigned height) {
    if (!Enabled() || width != 1920 || height != 1080 || !IsUuid(device_uuid) ||
        !IsUuid(driver_uuid)) return false;
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return true;
    start_steady_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_release);

    // Copy and validate every path/session now, before the worker calls posix_spawn.
    session = Env("BB_NR_C2_SESSION");
    mode = Env("BB_NR_C2_MODE");
    run_dir = Env("BB_NR_C2_RUN_DIR");
    windows_run_dir = Env("BB_NR_C2_RUN_DIR_WINDOWS");
    const auto preloader = Env("BB_NR_C2_PRELOADER");
    const auto wine = Env("BB_NR_C2_WINE");
    const auto helper = Env("BB_NR_C2_HELPER_EXE");
    helper_exe = helper;
    const auto wineprefix = Env("BB_NR_C2_WINEPREFIX");
    const auto overrides = Env("BB_NR_C2_WINEDLLOVERRIDES");
    const auto dllpath = Env("BB_NR_C2_WINEDLLPATH");
    const auto wine_debug = Env("BB_NR_C2_WINEDEBUG");
    if ((mode != "single" && mode != "skipEvaluate") || !IsSession(session) || !IsAbsolute(run_dir) || !IsAbsolute(preloader) ||
        !IsAbsolute(wine) || !IsAbsolute(helper) || windows_run_dir.rfind("Z:", 0) != 0 ||
        !IsAbsolute(wineprefix) || overrides.empty() || dllpath.empty() || wine_debug.empty()) {
        SetFailure("C2 env missing or not absolute; no helper spawned");
        return false;
    }
    try {
        const auto session_copy = session;
        const auto mode_copy = mode;
        const auto run_dir_copy = run_dir;
        const auto windows_run_dir_copy = windows_run_dir;
        const auto wineprefix_copy = wineprefix;
        const auto overrides_copy = overrides;
        const auto dllpath_copy = dllpath;
        const auto wine_debug_copy = wine_debug;
        setup_thread = std::jthread([this, device_uuid, driver_uuid, width, height,
                                     preloader, wine, helper, session_copy, run_dir_copy,
                                     windows_run_dir_copy, wineprefix_copy, overrides_copy,
                                     dllpath_copy, wine_debug_copy, mode_copy](std::stop_token stop) {
            try {
                std::filesystem::create_directories(run_dir_copy);
                chmod(run_dir_copy.c_str(), 0700);
                const auto marker_path = run_dir_copy + "/profile/.c2-profile";
                if (ReadFile(marker_path) != session_copy + "\n")
                    throw std::runtime_error("private .c2-profile marker missing or mismatched");
                for (const char* stale : {"native.pid", "pid-request.json", "game-config.json"}) {
                    if (std::filesystem::exists(run_dir_copy + "/" + stale))
                        throw std::runtime_error(std::string("refuse existing C2 case file: ") + stale);
                }
                const std::string config =
                    "{\"session\":\"" + session_copy + "\",\"generation\":0,\"mode\":\"" + mode_copy + "\","
                    "\"profile_id\":\"hdr-run-dlaa-1to1-j0-manual-wp1-v1\",\"width\":" +
                    std::to_string(width) + ",\"height\":" + std::to_string(height) +
                    ",\"render_width\":" + std::to_string(width) +
                    ",\"render_height\":" + std::to_string(height) +
                    ",\"max_frames\":1,\"cold_init_timeout_ms\":120000,\"control_timeout_ms\":60000,"
                    "\"gpu_timeout_ms\":10000,\"frame_completion_timeout_ms\":30000,"
                    "\"manual_white_point\":1,\"passthrough\":false,\"device_uuid\":\"" + device_uuid +
                    "\",\"driver_uuid\":\"" + driver_uuid + "\"}\n";
                AtomicWrite(run_dir_copy + "/game-config.json", config);
                const std::string argv0 = preloader;
                const std::string unix_wine = wine;
                const std::string win_helper = "Z:" + helper;
                const std::string win_run = windows_run_dir_copy;
                std::filesystem::create_directories(wineprefix_copy);
                chmod(wineprefix_copy.c_str(), 0700);
                std::array<std::string, 6> args{
                    argv0, unix_wine, win_helper, mode_copy, win_run, session_copy};
                std::array<char*, 7> argv{};
                for (size_t i = 0; i < args.size(); ++i) argv[i] = args[i].data();

                auto env_strings = BuildChildEnvironment(wineprefix_copy, overrides_copy,
                                                         dllpath_copy, wine_debug_copy);
                std::vector<char*> envp;
                envp.reserve(env_strings.size() + 1);
                for (auto& item : env_strings) envp.push_back(item.data());
                envp.push_back(nullptr);
                pid_t pid = -1;
                const int rc = posix_spawn(&pid, preloader.c_str(), nullptr, nullptr,
                                           argv.data(), envp.data());
                if (rc != 0) {
                    throw std::runtime_error("posix_spawn preloader: " +
                                             std::string(std::strerror(rc)));
                }
                child_pid.store(pid, std::memory_order_release); // Wine preloader pid, not PE PID.
                AtomicWrite(run_dir_copy + "/native-launch.json",
                    "{\"status\":\"SPAWNED\",\"launcher_pid\":" +
                    std::to_string(pid) + ",\"native_game_pid\":" +
                    std::to_string(getpid()) + ",\"wine_preloader\":\"" + preloader +
                    "\",\"wine\":\"" + wine + "\",\"WINELOADERNOEXEC\":\"1\","
                    "\"DXVK_ENABLE_NVAPI\":\"1\","
                    "\"argv0\":\"wine-preloader\",\"PE_exporter_pid\":\"NOT_YET_VERIFIED\"}\n");
                const auto deadline = std::chrono::steady_clock::now() + SetupLimit;
                bool request_seen = false;
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                    const std::string request_text = ReadFile(run_dir_copy + "/pid-request.json");
                    if (!request_text.empty()) {
                        request_seen = true;
                        const std::string requested_session = JsonStringField(request_text, "session");
                        const std::string requested_exe = NormalizeWinePath(JsonStringField(request_text, "exe"));
                        if (requested_session != session_copy || requested_exe != helper) {
                            throw std::runtime_error("pid-request session/helper path mismatch");
                        }
                        const pid_t exporter = FindExporter(helper, run_dir_copy, pid, mode_copy, session_copy);
                        if (exporter > 1) {
                            const uint64_t ticks = StartTicks(exporter);
                            if (!ticks) throw std::runtime_error("exporter starttime unreadable");
                            exporter_pid.store(exporter, std::memory_order_release);
                            exporter_start_ticks.store(ticks, std::memory_order_release);
                            AtomicWrite(run_dir_copy + "/native.pid", std::to_string(exporter) + "\n");
                            AtomicWrite(run_dir_copy + "/native-exporter-identity.json",
                                "{\"session\":\"" + session_copy + "\",\"game_pid\":" +
                                std::to_string(getpid()) + ",\"launcher_pid\":" +
                                std::to_string(pid) + ",\"exporter_pid\":" +
                                std::to_string(exporter) + ",\"exporter_start_ticks\":" +
                                std::to_string(ticks) + ",\"descendant_game\":true,"
                                "\"descendant_launcher\":true,\"request_exe_match\":true,"
                                "\"unique_run_cmd\":true,\"exact_pe_maps\":true}\n");
                            ready.store(true, std::memory_order_release);
                            AtomicWrite(run_dir_copy + "/native-launch.json",
                                "{\"status\":\"EXPORTER_PID_VERIFIED\",\"launcher_pid\":" +
                                std::to_string(pid) + ",\"native_game_pid\":" +
                                std::to_string(getpid()) + ",\"exporter_pid\":" +
                                std::to_string(exporter) + ",\"exporter_start_ticks\":" +
                                std::to_string(ticks) + ",\"WINELOADERNOEXEC\":\"1\","
                                "\"preloader_argv1_is_absolute_unix_wine\":true}\n");
                            // Keep the direct child waitable while the helper runs. Its normal
                            // exit is recorded separately; absence of a quick exit is not failure.
                            while (!stop.stop_requested()) {
                                int status = 0;
                                const pid_t reaped = waitpid(pid, &status, WNOHANG);
                                if (reaped == pid) {
                                    child_pid.store(-1, std::memory_order_release);
                                    exit_status.store(status, std::memory_order_release);
                                    completed.store(true, std::memory_order_release);
                                    AtomicWrite(run_dir_copy + "/native-helper-exit.json",
                                        "{\"wait_status\":" + std::to_string(status) +
                                        ",\"normal_exit\":" +
                                        (WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "true}\n" : "false}\n"));
                                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                                        SetFailure("private Wine helper exited nonzero after PID verification");
                                    return;
                                }
                                if (reaped < 0 && errno != EINTR) {
                                    SetFailure("waitpid after exporter PID verification failed");
                                    return;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                            }
                            return;
                        }
                    }
                    int status = 0;
                    const pid_t reaped = waitpid(pid, &status, WNOHANG);
                    if (reaped == pid) {
                        child_pid.store(-1, std::memory_order_release);
                        exit_status.store(status, std::memory_order_release);
                        completed.store(true, std::memory_order_release);
                        SetFailure("Wine launcher exited before verified exporter PID publication");
                        AtomicWrite(run_dir_copy + "/native-launch-exit.json",
                            "{\"wait_status\":" + std::to_string(status) + "}\n");
                        return;
                    }
                    if (reaped < 0 && errno != EINTR) {
                        SetFailure("waitpid Wine launcher failed: " +
                                   std::string(std::strerror(errno)));
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    SetFailure("C2 exporter identity gate deadline 60000ms expired");
                    AtomicWrite(run_dir_copy + "/native-setup-timeout.json",
                        std::string("{\"phase\":\"cold_setup\",\"limit_ms\":60000,\"pid_request_seen\":") +
                        (request_seen ? "true}\n" : "false}\n"));
                    // Do not signal the child or claim its resources were released. The root's
                    // outer owned process group is the only cleanup authority.
                }
            } catch (const std::exception& e) {
                SetFailure(e.what());
            }
        });
    } catch (const std::exception& e) {
        SetFailure(std::string("create C2 setup worker: ") + e.what());
        return false;
    }
    return true;
}

} // namespace Vulkan
