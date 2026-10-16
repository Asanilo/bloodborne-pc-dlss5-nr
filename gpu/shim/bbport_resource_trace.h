// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// CPU-only cold diagnostics. One shared trace per linked image: at most 4096
// events + ONE truncation line, each <=512 bytes (including newline). No resource
// reads, GPU calls, worker threads, or inferred completion. All hooks should live
// in libbbgpu so the cap/id space also covers all its translation units.
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/syscall.h>
#include <unistd.h>

namespace BbResourceTrace {
inline constexpr uint64_t EventCap = 4096;
inline constexpr size_t LineBytes = 512;
inline constexpr size_t TokenBytes = 24;

inline bool Enabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("BB_NR_RESOURCE_TRACE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
namespace Detail {
struct State {
    std::mutex mutex;
    uint64_t emitted = 0;
    bool truncated = false;
    std::atomic<uint64_t> next_id{1};
};
// Resource destruction hooks can run during static teardown; retain this
// tiny CPU-only sink until process exit instead of imposing destructor order.
inline State& Get() { static State* state = new State; return *state; }
inline void Token(char (&out)[TokenBytes+1], const char* input) noexcept {
    size_t n = 0;
    if (input) for (; n < TokenBytes && input[n]; ++n) {
        const unsigned char c = input[n];
        out[n] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-') ? char(c) : '_';
    }
    if (!n) { std::memcpy(out, "unknown", 7); n = 7; }
    out[n] = 0;
}
} // namespace Detail

// Allocate once at resource creation, retain through schedule/actual-retire.
// A reused Vulkan handle gets a NEW id; 0 means tracing was disabled, never proof.
inline uint64_t NewId() noexcept {
    if (!Enabled()) return 0;
    try { return Detail::Get().next_id.fetch_add(1, std::memory_order_relaxed); }
    catch (...) { return 0; }
}
struct Event {
    const char* type;
    const char* event;
    uint64_t id;
    uint64_t handle;
    uint64_t bda;
    uint64_t bytes;
    uint64_t scheduler = 0; // opaque CPU identity, never dereferenced
    uint64_t retire_tick = 0;
    uint64_t observed_tick = 0;
};

// Returns whether an event was emitted. Tick values are caller observations:
// unknown MUST be 0, and even two zero/equal ticks NEVER imply completion here.
// Do not pass a new NewId per event: repeat the resource lifetime id instead.
inline bool Emit(const Event& data) noexcept {
    if (!Enabled()) return false;
    const int saved_errno = errno;
    bool accepted = false;
    try {
        auto& state = Detail::Get();
        std::lock_guard lock{state.mutex};
        if (state.emitted < EventCap || !state.truncated) {
            const bool truncated = state.emitted >= EventCap;
            const uint64_t seq = truncated ? EventCap+1 : ++state.emitted;
            if (truncated) state.truncated = true;
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            char kind[TokenBytes+1], action[TokenBytes+1], line[LineBytes];
            Detail::Token(kind, truncated ? "trace" : data.type);
            Detail::Token(action, truncated ? "truncated" : data.event);
            const int length = std::snprintf(line, sizeof line,
                "NR_RESOURCE_TRACE monotonic_ns=%" PRIu64 " seq=%" PRIu64
                " tid=%ld type=%s event=%s id=%" PRIu64 " handle=0x%016" PRIx64
                " bda=0x%016" PRIx64 " bytes=%" PRIu64 " retire_tick=%" PRIu64
                " observed_tick=%" PRIu64 " scheduler=0x%016" PRIx64
                " completion=NOT_INFERRED unknown_tick=0 scope=linked_image_cpu_hooks"
                " cap=%" PRIu64 " truncated=%u\n",
                uint64_t(ns), seq, ::syscall(SYS_gettid), kind, action,
                truncated ? 0 : data.id, truncated ? 0 : data.handle, truncated ? 0 : data.bda,
                truncated ? 0 : data.bytes, truncated ? 0 : data.retire_tick,
                truncated ? 0 : data.observed_tick, truncated ? 0 : data.scheduler, EventCap, unsigned(truncated));
            if (length > 0 && size_t(length) < sizeof line) {
                // One stdio operation keeps our complete record independent from
                // concurrent stderr users; the mutex orders trace seq/write/flush.
                accepted = std::fwrite(line, size_t(length), 1, stderr) == 1 && !truncated;
                std::fflush(stderr);
            }
        }
    } catch (...) { /* Logging failure must not change the resource operation. */ }
    errno = saved_errno;
    return accepted;
}
} // namespace BbResourceTrace
