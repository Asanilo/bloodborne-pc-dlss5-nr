// Regression for backend mouse timestamps and capture/focus transitions.
// Requires an X11/Wayland display; creates only a window, no guest or Vulkan device.
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include "sdl_window.h"
#include "bbport_settings.h"
#include "../bbgpu.h"

int main() {
    BbSettings::Get().mouse_capture = true;
    Frontend::WindowSDL window(640, 480, "Bloodborne input regression");
    window.PollEvents();
    const auto id = SDL_GetWindowID(window.GetSDLWindow());
    auto focus = [&](bool gained) {
        SDL_Event e{};
        e.type = gained ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
        e.window.windowID = id;
        assert(SDL_PushEvent(&e));
        window.PollEvents();
    };
    auto motion = [&](Uint64 timestamp, float x, float y) {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = id;
        e.motion.timestamp = timestamp;
        e.motion.xrel = x;
        e.motion.yrel = y;
        assert(SDL_PushEvent(&e));
    };
    auto read = [&]() {
        window.PollEvents();
        BbHostInput state{};
        bbgpu_read_host_input(&state);
        return state;
    };
    focus(true);
    read();
    // A backend timestamp can be much smaller than SDL_GetTicksNS(). It must
    // not suppress newly received motion. The previous implementation fails here.
    motion(1, 32, -16);
    auto state = read();
    assert(!state.blocked && state.mouse_x == 32 && state.mouse_y == -16);
    state = read();
    assert(state.mouse_x == 0 && state.mouse_y == 0);
    focus(false);
    motion(1, 100, 100);
    state = read();
    assert(state.blocked && state.mouse_x == 0 && state.mouse_y == 0);
    // Queued motion before recapture is discarded, regardless of its timestamp.
    motion(SDL_GetTicksNS() + 1000000000, 200, 200);
    focus(true);
    state = read();
    assert(!state.blocked && state.mouse_x == 0 && state.mouse_y == 0);
    motion(1, -12, 8);
    state = read();
    assert(state.mouse_x == -12 && state.mouse_y == 8);
    BbSettings::Get().mouse_capture = false;
    read();
    motion(1, 100, 100);
    state = read();
    assert(state.mouse_x == 0 && state.mouse_y == 0);
    motion(1, 200, 200);
    BbSettings::Get().mouse_capture = true;
    state = read();
    assert(state.mouse_x == 0 && state.mouse_y == 0);
    motion(1, 4, 2);
    state = read();
    assert(state.mouse_x == 4 && state.mouse_y == 2);
    std::puts("PASS: backend timestamps, idle, focus loss/restore, capture off/on");
}
