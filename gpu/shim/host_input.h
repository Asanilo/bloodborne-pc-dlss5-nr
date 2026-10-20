#pragma once
#include <algorithm>
#include <mutex>
#include "../bbgpu.h"

// All SDL event access stays on the window thread. Pad reads can run on game threads.
class HostInput {
    std::mutex mutex;
    BbHostInput state{};
public:
    void Block(bool blocked) {
        std::scoped_lock guard{mutex};
        if (blocked != bool(state.blocked)) {
            state = {};
            state.blocked = blocked;
            state.mouse_captured = 0;
        }
    }
    // Capture changes clear mouse buttons, motion and wheel; keyboard stays.
    void Capture(bool captured) {
        std::scoped_lock guard{mutex};
        const int value = captured && !state.blocked ? 1 : 0;
        if (state.mouse_captured == value) return;
        state.mouse_captured = value;
        state.mouse_buttons = 0;
        state.mouse_x = state.mouse_y = 0;
        state.wheel_up = state.wheel_down = 0;
    }
    void Key(unsigned key, bool down) {
        std::scoped_lock guard{mutex};
        if (!state.blocked && key < 512) state.keys[key] = down;
    }
    void Button(unsigned button, bool down) {
        std::scoped_lock guard{mutex};
        if (state.blocked || !state.mouse_captured || button < 1 || button > 5) return;
        const auto mask = 1u << (button - 1);
        if (down) state.mouse_buttons |= mask;
        else state.mouse_buttons &= ~mask;
    }
    void Motion(float x, float y) {
        std::scoped_lock guard{mutex};
        if (!state.blocked && state.mouse_captured) {
            state.mouse_x = std::clamp(state.mouse_x + x, -4096.0f, 4096.0f);
            state.mouse_y = std::clamp(state.mouse_y + y, -4096.0f, 4096.0f);
        }
    }
    void Wheel(int up, int down) {
        std::scoped_lock guard{mutex};
        if (state.blocked || !state.mouse_captured) return;
        state.wheel_up = std::min(state.wheel_up + up, 16);
        state.wheel_down = std::min(state.wheel_down + down, 16);
    }
    // The pad's single snapshot: relative motion and wheel are consumed by this read.
    void Read(BbHostInput* out) {
        std::scoped_lock guard{mutex};
        *out = state;
        state.mouse_x = state.mouse_y = 0;
        state.wheel_up = state.wheel_down = 0;
    }
};
