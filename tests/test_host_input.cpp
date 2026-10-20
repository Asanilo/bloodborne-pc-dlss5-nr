#include "gpu/shim/host_input.h"
#include <cassert>

int main() {
    HostInput input;
    BbHostInput state{};
    input.Key(26, true);
    input.Capture(true);
    input.Button(1, true);
    input.Motion(4, -3);
    input.Wheel(1, 0);
    input.Read(&state);
    assert(state.keys[26] && state.mouse_captured);
    assert(state.mouse_buttons == 1 && state.mouse_x == 4 && state.mouse_y == -3);
    assert(state.wheel_up == 1 && state.wheel_down == 0);

    // One snapshot consumes motion and wheel; buttons/capture/keyboard stay.
    input.Read(&state);
    assert(state.keys[26] && state.mouse_captured && state.mouse_buttons == 1);
    assert(state.mouse_x == 0 && state.mouse_y == 0 && state.wheel_up == 0 && state.wheel_down == 0);

    // Capture release clears buttons/motion/wheel but not the keyboard.
    input.Motion(9, 9);
    input.Wheel(0, 1);
    input.Capture(false);
    input.Motion(5, 5);
    input.Button(2, true);
    input.Read(&state);
    assert(!state.mouse_captured && state.keys[26]);
    assert(state.mouse_buttons == 0 && state.mouse_x == 0 && state.mouse_y == 0);
    assert(state.wheel_up == 0 && state.wheel_down == 0);

    // A new capture starts from a clean mouse state (the first warp is not queued here either).
    input.Capture(true);
    input.Motion(2, -2);
    input.Wheel(0, 1);
    input.Read(&state);
    assert(state.mouse_captured && state.mouse_x == 2 && state.mouse_y == -2 && state.wheel_down == 1);

    input.Block(true);
    input.Key(26, true);
    input.Motion(100, 100);
    input.Button(1, true);
    input.Read(&state);
    assert(state.blocked && !state.keys[26] && !state.mouse_captured);
    assert(state.mouse_buttons == 0 && state.mouse_x == 0 && state.mouse_y == 0);

    input.Block(false);
    input.Read(&state);
    assert(!state.blocked && !state.keys[26] && !state.mouse_captured && state.mouse_x == 0);
    input.Motion(7, 7); // not captured yet: ignored
    input.Read(&state);
    assert(state.mouse_x == 0 && state.mouse_y == 0);
    input.Key(26, true);
    input.Capture(true);
    input.Motion(-1, 1);
    input.Read(&state);
    assert(state.keys[26] && state.mouse_captured && state.mouse_x == -1 && state.mouse_y == 1);
    input.Key(26, false);
    input.Read(&state);
    assert(!state.keys[26]);
}
