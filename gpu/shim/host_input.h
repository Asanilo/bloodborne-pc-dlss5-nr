#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>
#include "../bbgpu.h"

// All SDL event access stays on the window thread. Pad reads can run on game threads.
class HostInput {
    std::mutex mutex;
    BbHostInput state{};
    using EpochFn = std::uint64_t (*)();
    using CaptureFn = void (*)(bool);
    using CancelFn = void (*)();
    EpochFn epoch_fn;
    CaptureFn capture_fn;
    CancelFn cancel_fn;
    bool cinematic_active{};
    bool capture_known{};
public:
    explicit HostInput(EpochFn epoch = nullptr, CaptureFn capture = nullptr,
                       CancelFn cancel = nullptr)
        : epoch_fn(epoch), capture_fn(capture), cancel_fn(cancel) {}
    // Independent keyboard/mouse owner: blocked remains focus/menu only, so the
    // physical controller can keep playing during the cinematic camera.
    void CinematicActive(bool active) {
        std::scoped_lock guard{mutex};
        if (cinematic_active == active) return;
        cinematic_active = active;
        const int blocked = state.blocked;
        state = {};
        state.blocked = blocked;
        capture_known = true;
        if (capture_fn) capture_fn(false);
        if (cancel_fn) cancel_fn();
    }
    bool CinematicActive() {
        std::scoped_lock guard{mutex};
        return cinematic_active;
    }
    bool Captured() {
        std::scoped_lock guard{mutex};
        return state.mouse_captured != 0;
    }
    void Block(bool blocked) {
        std::scoped_lock guard{mutex};
        if (blocked != bool(state.blocked)) {
            state = {};
            state.blocked = blocked;
            state.mouse_captured = 0;
            if (capture_fn) capture_fn(false);
        }
    }
    // Capture changes clear mouse buttons, motion and wheel; keyboard stays. The first
    // Capture(false) must reach the mailbox too: it invalidates a generation that may still
    // hold native work from a previous capture.
    void Capture(bool captured) {
        std::scoped_lock guard{mutex};
        const int value = captured && !state.blocked && !cinematic_active ? 1 : 0;
        if (capture_known && state.mouse_captured == value) return;
        capture_known = true;
        state.mouse_captured = value;
        state.mouse_buttons = 0;
        state.mouse_x = state.mouse_y = 0;
        state.wheel_up = state.wheel_down = 0;
        if (capture_fn) capture_fn(value != 0);
    }
    // Motion and its epoch share this lock. EpochFn reads an acquire-published epoch without
    // taking the mailbox gate. Reset lock order is HostInput -> mailbox; the camera and pad
    // router never acquire HostInput while holding the mailbox.
    void CancelMotion() {
        std::scoped_lock guard{mutex};
        state.mouse_x = state.mouse_y = 0;
        state.mouse_epoch = 0;
        if (cancel_fn) cancel_fn();
    }
    void Key(unsigned key, bool down) {
        std::scoped_lock guard{mutex};
        if (!state.blocked && !cinematic_active && key < 512) state.keys[key] = down;
    }
    void Button(unsigned button, bool down) {
        std::scoped_lock guard{mutex};
        if (state.blocked || cinematic_active || !state.mouse_captured || button < 1 || button > 5) return;
        const auto mask = 1u << (button - 1);
        if (down) state.mouse_buttons |= mask;
        else state.mouse_buttons &= ~mask;
    }
    void Motion(float x, float y) {
        std::scoped_lock guard{mutex};
        if (!state.blocked && !cinematic_active && state.mouse_captured) {
            const auto epoch = epoch_fn ? epoch_fn() : 0;
            if (state.mouse_epoch != epoch) state.mouse_x = state.mouse_y = 0;
            state.mouse_epoch = epoch;
            state.mouse_x = std::clamp(state.mouse_x + x, -4096.0f, 4096.0f);
            state.mouse_y = std::clamp(state.mouse_y + y, -4096.0f, 4096.0f);
        }
    }
    void Wheel(int up, int down) {
        std::scoped_lock guard{mutex};
        if (state.blocked || cinematic_active || !state.mouse_captured) return;
        state.wheel_up = std::min(state.wheel_up + up, 16);
        state.wheel_down = std::min(state.wheel_down + down, 16);
    }
    // The pad's single snapshot: relative motion and wheel are consumed by this read.
    void Read(BbHostInput* out) {
        std::scoped_lock guard{mutex};
        if (epoch_fn && state.mouse_epoch != epoch_fn())
            state.mouse_x = state.mouse_y = 0;
        *out = state;
        state.mouse_x = state.mouse_y = 0;
        state.wheel_up = state.wheel_down = 0;
    }
};
