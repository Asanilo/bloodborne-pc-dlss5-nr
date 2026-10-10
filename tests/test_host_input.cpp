#include "gpu/shim/host_input.h"
#include <cassert>

int main() {
    HostInput input;
    BbHostInput state{};
    input.Key(26, true);
    input.Button(1, true);
    input.Motion(4, -3);
    input.Read(&state);
    assert(state.keys[26] && state.mouse_buttons == 1 && state.mouse_x == 4 && state.mouse_y == -3);
    input.Read(&state);
    assert(state.keys[26] && state.mouse_x == 0);
    input.Block(true);
    input.Key(26, true);
    input.Motion(100, 100);
    input.Read(&state);
    assert(state.blocked && !state.keys[26] && state.mouse_buttons == 0 && state.mouse_x == 0);
    input.Block(false);
    input.Read(&state);
    assert(!state.blocked && !state.keys[26] && state.mouse_x == 0);
    input.Key(26, true);
    input.Key(26, false);
    input.Read(&state);
    assert(!state.keys[26]);
}
