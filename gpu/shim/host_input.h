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
        }
    }
    void Key(unsigned key, bool down) {
        std::scoped_lock guard{mutex};
        if (!state.blocked && key < 512) state.keys[key] = down;
    }
    void Button(unsigned button, bool down) {
        std::scoped_lock guard{mutex};
        if (state.blocked || button < 1 || button > 5) return;
        const auto mask = 1u << (button - 1);
        if (down) state.mouse_buttons |= mask;
        else state.mouse_buttons &= ~mask;
    }
    void Motion(float x, float y) {
        std::scoped_lock guard{mutex};
        if (!state.blocked) {
            state.mouse_x = std::clamp(state.mouse_x + x, -4096.0f, 4096.0f);
            state.mouse_y = std::clamp(state.mouse_y + y, -4096.0f, 4096.0f);
        }
    }
    void Read(BbHostInput* out) {
        std::scoped_lock guard{mutex};
        *out = state;
        state.mouse_x = state.mouse_y = 0;
    }
};
