// SPDX-License-Identifier: GPL-2.0-or-later
//
// Native mouse camera mailbox, candidate stage 2.
//
// Layout is a fixed, 64-byte aligned POD.  `gate` is an integer acquire/release word; every
// mutable field except `published_epoch` is read and written only while that gate is held.
// `published_epoch` is the single deliberate exception: the authoritative epoch is changed
// under the gate, then published with a release store; HostInput and `Epoch()` read it with an
// acquire load without touching the gate.
//
// Host operations may spin/yield.  `TryConsume` implements the guest contract: exactly one
// try-acquire, never waiting.  The production guest stub mirrors this file's field offsets.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>

namespace BbNativeMouse {

inline constexpr std::uint32_t kInitialEpoch = 1;
inline constexpr std::uint64_t kTimeoutUs = 100'000; // elapsed >= 100 ms expires
inline constexpr double kMaxAxisRadians = 16.0;      // fixed per-axis accumulated limit

inline constexpr std::uint32_t kGateFree = 0;
inline constexpr std::uint32_t kGateHeld = 1;

enum class SubmitResult : std::uint32_t {
    Native = 0,
    Fallback = 1,
    Discard = 2,
};

enum class ConsumeStatus : std::uint32_t {
    Empty = 0,       // gate acquired, no pending batch; heartbeat may have progressed
    Batch = 1,       // one native batch returned (possibly with zero net motion)
    Contended = 2,   // gate busy; no field was read or written
    Invalidated = 3, // stale/future pending, backwards time, capture off or fatal state
    Blocked = 4,     // clock or MXCSR capability unavailable; no state change
};

struct ConsumeResult {
    ConsumeStatus status = ConsumeStatus::Empty;
    float pitch = 0.0f;
    float yaw = 0.0f;
    std::uint32_t epoch = 0;
    std::uint64_t sequence = 0;       // last sequence in the returned batch
    std::uint64_t first_sequence = 0; // first sequence in the returned batch
    bool has_motion = false;          // false for a zero-net batch: retire but do not write flags

    [[nodiscard]] bool consumed() const noexcept { return status == ConsumeStatus::Batch; }
};

// Public shared state.  These offsets are the stage-2 assembly contract.
struct alignas(64) State {
    std::uint32_t gate;               // 0
    std::uint32_t installed;          // 4
    std::uint32_t captured;           // 8
    std::uint32_t ready;              // 12
    std::uint32_t epoch;              // 16  authoritative, gate-only
    std::uint32_t has_pending;        // 20
    std::uint32_t published_epoch;    // 24  atomic mirror, gate exception
    std::uint32_t fatal;              // 28  epoch/sequence/heartbeat-sequence exhausted
    std::uint64_t sequence;           // 32
    std::uint64_t pending_first_seq;  // 40
    std::uint64_t pending_last_seq;   // 48
    std::uint64_t last_consumed_seq;  // 56
    std::uint64_t heartbeat_us;       // 64
    std::uint64_t pending_since_us;   // 72
    std::uint64_t last_seen_us;       // 80
    float pitch;                      // 88
    float yaw;                        // 92
    std::uint64_t heartbeat_seq;      // 96
    std::uint64_t reset_watermark;    // 104
    std::uint64_t fallback_watermark; // 112
    std::uint64_t cancelled_seq;      // 120
    std::uint32_t reserved[16];       // 128..191
};

static_assert(std::is_standard_layout_v<State>);
static_assert(std::is_trivial_v<State>);
static_assert(std::is_trivially_copyable_v<State>);
static_assert(alignof(State) == 64);
static_assert(sizeof(State) == 192);
static_assert(offsetof(State, gate) == 0);
static_assert(offsetof(State, installed) == 4);
static_assert(offsetof(State, captured) == 8);
static_assert(offsetof(State, ready) == 12);
static_assert(offsetof(State, epoch) == 16);
static_assert(offsetof(State, has_pending) == 20);
static_assert(offsetof(State, published_epoch) == 24);
static_assert(offsetof(State, fatal) == 28);
static_assert(offsetof(State, sequence) == 32);
static_assert(offsetof(State, pending_first_seq) == 40);
static_assert(offsetof(State, pending_last_seq) == 48);
static_assert(offsetof(State, last_consumed_seq) == 56);
static_assert(offsetof(State, heartbeat_us) == 64);
static_assert(offsetof(State, pending_since_us) == 72);
static_assert(offsetof(State, last_seen_us) == 80);
static_assert(offsetof(State, pitch) == 88);
static_assert(offsetof(State, yaw) == 92);
static_assert(offsetof(State, heartbeat_seq) == 96);
static_assert(offsetof(State, reset_watermark) == 104);
static_assert(offsetof(State, fallback_watermark) == 112);
static_assert(offsetof(State, cancelled_seq) == 120);
static_assert(offsetof(State, reserved) == 128);

namespace detail {

inline bool gate_try_acquire(State& state) noexcept {
    std::uint32_t expected = kGateFree;
    return __atomic_compare_exchange_n(&state.gate, &expected, kGateHeld, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

inline void gate_release(State& state) noexcept {
    __atomic_store_n(&state.gate, kGateFree, __ATOMIC_RELEASE);
}

inline void host_lock(State& state) noexcept {
    unsigned spins = 0;
    while (!gate_try_acquire(state)) {
        if (++spins >= 32) {
            spins = 0;
            std::this_thread::yield();
        }
    }
}

struct HostGuard {
    State& state;
    explicit HostGuard(State& value) noexcept : state(value) { host_lock(state); }
    ~HostGuard() { gate_release(state); }
    HostGuard(const HostGuard&) = delete;
    HostGuard& operator=(const HostGuard&) = delete;
};

struct GuestGuard {
    State& state;
    explicit GuestGuard(State& value) noexcept : state(value) {}
    ~GuestGuard() { gate_release(state); }
    GuestGuard(const GuestGuard&) = delete;
    GuestGuard& operator=(const GuestGuard&) = delete;
};

inline void publish_epoch(State& state) noexcept {
    __atomic_store_n(&state.published_epoch, state.epoch, __ATOMIC_RELEASE);
}

inline void clear_pending(State& state) noexcept {
    state.has_pending = 0;
    state.pending_first_seq = 0;
    state.pending_last_seq = 0;
    state.pending_since_us = 0;
    state.pitch = 0.0f;
    state.yaw = 0.0f;
}

inline void set_fatal(State& state) noexcept {
    state.fatal = 1;
    if (state.has_pending) state.cancelled_seq = state.pending_last_seq;
    clear_pending(state);
    state.ready = 0;
    state.heartbeat_us = 0;
    __atomic_store_n(&state.published_epoch, 0, __ATOMIC_RELEASE);
}

// Epoch overflow must disable the candidate, never wrap and revalidate an old snapshot.
inline bool advance_epoch(State& state) noexcept {
    if (state.fatal) return false;
    if (state.epoch == UINT32_MAX) {
        set_fatal(state);
        return false;
    }
    ++state.epoch;
    publish_epoch(state);
    return true;
}

enum class InvalidateReason { Reset, Fallback };

inline void invalidate(State& state, InvalidateReason reason) noexcept {
    if (state.has_pending) state.cancelled_seq = state.pending_last_seq;
    const bool advanced = advance_epoch(state);
    clear_pending(state);
    state.ready = 0;
    state.heartbeat_us = 0;
    if (!advanced) return;
    if (reason == InvalidateReason::Fallback)
        state.fallback_watermark = state.heartbeat_seq;
    else
        state.reset_watermark = state.heartbeat_seq;
}

inline bool finite_axis(float value) noexcept {
    return std::isfinite(value);
}

// The 16 rad bound is a native-queue invariant, not a legacy/fallback routing rule.
inline bool within_native_cap(float value) noexcept {
    return std::fabs(static_cast<double>(value)) <= kMaxAxisRadians;
}

inline bool add_within_cap(float current, float add, double& out) noexcept {
    const double sum = static_cast<double>(current) + static_cast<double>(add);
    if (!std::isfinite(sum) || std::fabs(sum) > kMaxAxisRadians) return false;
    out = sum;
    return true;
}

// Exactly 100 ms elapsed is already expired.
inline bool expired(std::uint64_t now_us, std::uint64_t since_us) noexcept {
    return now_us < since_us || now_us - since_us >= kTimeoutUs;
}


inline SubmitResult submit_locked(State& state, std::uint32_t epoch, float pitch, float yaw,
                                  std::uint64_t now_us) noexcept {
    if (state.fatal) return SubmitResult::Discard;

    // Reject old snapshots/capture before any clock rollback can reset the new generation.
    if (state.captured == 0 || epoch != state.epoch) return SubmitResult::Discard;

    if (now_us < state.last_seen_us) {
        invalidate(state, InvalidateReason::Reset);
        state.last_seen_us = now_us;
        return SubmitResult::Discard;
    }
    state.last_seen_us = now_us;

    // Non-finite data is never a valid camera input, even for the legacy path.
    if (!finite_axis(pitch) || !finite_axis(yaw)) return SubmitResult::Discard;

    // Default-off / not installed: finite large movement is valid legacy input.
    if (state.installed == 0) {
        if (state.has_pending) invalidate(state, InvalidateReason::Reset);
        return SubmitResult::Fallback;
    }

    const bool has_pending = state.has_pending != 0;
    const bool pending_stale = has_pending && expired(now_us, state.pending_since_us);
    const bool ready = state.ready != 0;
    const bool heartbeat_stale =
        ready && (state.heartbeat_us == 0 || state.heartbeat_us > now_us ||
                  now_us - state.heartbeat_us >= kTimeoutUs);

    // Expiry safety runs before the native 16 rad bound.  The invoking input is discarded, and
    // the old pending is cleared even if the input itself is large.
    if (pending_stale || (!ready && has_pending) || heartbeat_stale) {
        invalidate(state, InvalidateReason::Fallback);
        return SubmitResult::Discard;
    }

    // Installed but without a zero-displacement heartbeat: finite large movement is fallback.
    if (!ready) return SubmitResult::Fallback;

    // Only a candidate Native enqueue is bounded by the fixed 16 rad per-axis/accumulated cap.
    if (!within_native_cap(pitch) || !within_native_cap(yaw))
        return SubmitResult::Discard;

    double next_pitch = 0.0;
    double next_yaw = 0.0;
    if (!add_within_cap(has_pending ? state.pitch : 0.0f, pitch, next_pitch) ||
        !add_within_cap(has_pending ? state.yaw : 0.0f, yaw, next_yaw)) {
        return SubmitResult::Discard;
    }

    if (state.sequence == UINT64_MAX) {
        set_fatal(state);
        return SubmitResult::Discard;
    }

    const std::uint64_t sequence = state.sequence + 1;
    state.sequence = sequence;
    if (!has_pending) {
        state.has_pending = 1;
        state.pending_first_seq = sequence;
        state.pending_since_us = now_us;
    }
    state.pending_last_seq = sequence;
    state.pitch = static_cast<float>(next_pitch);
    state.yaw = static_cast<float>(next_yaw);
    return SubmitResult::Native;
}

} // namespace detail

class alignas(64) Mailbox {
public:
    State state;

    Mailbox() noexcept : state{} {
        state.captured = 1; // uninstalled mailbox stays a transparent legacy path
        state.epoch = kInitialEpoch;
        detail::publish_epoch(state);
    }

    // Trivial layout/value copies; never copy a live mailbox across threads.
    Mailbox(const Mailbox&) = default;
    Mailbox& operator=(const Mailbox&) = default;

    // Gate-free generation read: published_epoch is the only atomic mirror.
    [[nodiscard]] std::uint32_t Epoch() const noexcept {
        return __atomic_load_n(&state.published_epoch, __ATOMIC_ACQUIRE);
    }

    [[nodiscard]] std::uint32_t SnapshotEpoch() const noexcept { return Epoch(); }

    void SetInstalled(bool installed) noexcept {
        detail::HostGuard guard(state);
        const std::uint32_t value = installed ? 1u : 0u;
        if (state.installed == value) return;
        state.installed = value;
        detail::invalidate(state, detail::InvalidateReason::Reset);
    }

    [[nodiscard]] bool Installed() noexcept {
        detail::HostGuard guard(state);
        return state.installed != 0;
    }

    void SetCapture(bool captured) noexcept {
        detail::HostGuard guard(state);
        const std::uint32_t value = captured ? 1u : 0u;
        if (state.captured == value) return;
        state.captured = value;
        detail::invalidate(state, detail::InvalidateReason::Reset);
    }

    [[nodiscard]] bool Captured() noexcept {
        detail::HostGuard guard(state);
        return state.captured != 0;
    }

    [[nodiscard]] bool Enabled() noexcept {
        detail::HostGuard guard(state);
        return state.fatal == 0 && state.installed != 0 && state.captured != 0;
    }

    // Host cancellation/reset: cancels unconsumed work and advances the generation.
    void Reset() noexcept { Cancel(); }

    void Cancel() noexcept {
        detail::HostGuard guard(state);
        detail::invalidate(state, detail::InvalidateReason::Reset);
    }

    // Free-camera/stick-only path: validate the snapshot's original generation and capture
    // under the same gate, without enqueueing native work or changing state.
    [[nodiscard]] bool ValidateStick(std::uint32_t epoch) noexcept {
        detail::HostGuard guard(state);
        return state.fatal == 0 && state.captured != 0 && epoch == state.epoch;
    }

    // Final installer gate: verifies the epoch can still advance, preserves capture, marks the
    // mailbox installed and starts a new generation.  Must be called before publishing the site.
    [[nodiscard]] bool InstallForCamera() noexcept {
        detail::HostGuard guard(state);
        if (state.fatal) return false;
        if (state.epoch == UINT32_MAX) {
            detail::set_fatal(state);
            return false;
        }
        state.installed = 1;
        detail::invalidate(state, detail::InvalidateReason::Reset);
        return state.fatal == 0;
    }

    SubmitResult Submit(std::uint32_t epoch, float pitch, float yaw, std::uint64_t now_us) noexcept {
        detail::HostGuard guard(state);
        return detail::submit_locked(state, epoch, pitch, yaw, now_us);
    }

    using ClockFn = bool (*)(void*, std::uint64_t*);

    // Host producer with the final clock read inside the gate.  Old snapshots, capture-off and
    // fatal state are rejected before the clock is consulted.
    SubmitResult SubmitWithClock(std::uint32_t epoch, float pitch, float yaw, ClockFn clock,
                                 void* context) noexcept {
        detail::HostGuard guard(state);
        if (state.fatal) return SubmitResult::Discard;
        if (state.captured == 0 || epoch != state.epoch) return SubmitResult::Discard;
        std::uint64_t now_us = 0;
        if (clock == nullptr || !clock(context, &now_us)) return SubmitResult::Discard;
        return detail::submit_locked(state, epoch, pitch, yaw, now_us);
    }

    // CPU reference for the guest stub.  `exceptions_masked` is the MXCSR capability check;
    // `clock_valid` models a failed/invalid SYS_clock_gettime.  Both fail without touching state.
    ConsumeResult TryConsume(std::uint64_t now_us, bool exceptions_masked = true,
                             bool clock_valid = true) noexcept {
        ConsumeResult result{};
        if (!exceptions_masked) {
            result.status = ConsumeStatus::Blocked;
            return result;
        }
        if (!detail::gate_try_acquire(state)) {
            result.status = ConsumeStatus::Contended;
            return result;
        }
        detail::GuestGuard guard(state);

        if (state.fatal) {
            result.status = ConsumeStatus::Invalidated;
            result.epoch = state.epoch;
            return result;
        }
        if (state.installed == 0 || state.captured == 0) {
            // Match the stub: no clock read, no last_seen/epoch change, immediate release.
            result.status = ConsumeStatus::Empty;
            result.epoch = state.epoch;
            return result;
        }
        if (!clock_valid) {
            result.status = ConsumeStatus::Blocked;
            return result;
        }

        if (now_us < state.last_seen_us) {
            detail::invalidate(state, detail::InvalidateReason::Reset);
            state.last_seen_us = now_us;
            result.status = ConsumeStatus::Invalidated;
            result.epoch = state.epoch;
            return result;
        }
        state.last_seen_us = now_us;

        if (state.has_pending) {
            if (state.ready == 0 || detail::expired(now_us, state.pending_since_us)) {
                detail::invalidate(state, detail::InvalidateReason::Fallback);
                result.status = ConsumeStatus::Invalidated;
                result.epoch = state.epoch;
                return result;
            }
            if (state.heartbeat_seq == UINT64_MAX) {
                detail::set_fatal(state);
                result.status = ConsumeStatus::Invalidated;
                result.epoch = state.epoch;
                return result;
            }

            result.status = ConsumeStatus::Batch;
            result.pitch = state.pitch;
            result.yaw = state.yaw;
            result.has_motion = state.pitch != 0.0f || state.yaw != 0.0f;
            result.epoch = state.epoch;
            result.first_sequence = state.pending_first_seq;
            result.sequence = state.pending_last_seq;
            state.last_consumed_seq = state.pending_last_seq;
            detail::clear_pending(state);
            ++state.heartbeat_seq;
            state.heartbeat_us = now_us;
            state.ready = 1;
            return result;
        }

        if (state.heartbeat_seq == UINT64_MAX) {
            detail::set_fatal(state);
            result.status = ConsumeStatus::Invalidated;
            result.epoch = state.epoch;
            return result;
        }

        const bool recovering = state.ready == 0;
        const std::uint64_t heartbeat = state.heartbeat_seq + 1;
        state.heartbeat_seq = heartbeat;
        if (heartbeat > state.reset_watermark && heartbeat > state.fallback_watermark) {
            if (recovering && !detail::advance_epoch(state)) {
                result.status = ConsumeStatus::Invalidated;
                result.epoch = state.epoch;
                return result;
            }
            state.heartbeat_us = now_us;
            state.ready = 1;
        }
        result.status = ConsumeStatus::Empty;
        result.epoch = state.epoch;
        return result;
    }

};

static_assert(std::is_standard_layout_v<Mailbox>);
static_assert(std::is_trivially_copyable_v<Mailbox>);
static_assert(alignof(Mailbox) == 64);
static_assert(sizeof(Mailbox) == sizeof(State));
static_assert(offsetof(Mailbox, state) == 0);

// Test-only gate access, not a production fault switch.
struct TestSeam {
    static bool TryAcquire(Mailbox& mailbox) noexcept {
        return detail::gate_try_acquire(mailbox.state);
    }
    static void Release(Mailbox& mailbox) noexcept {
        detail::gate_release(mailbox.state);
    }
};

} // namespace BbNativeMouse
