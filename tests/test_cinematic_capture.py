#!/usr/bin/env python3
"""Replay actual window capture/update bodies against stub SDL and real input/mailbox."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, marker):
    start = source.index(marker)
    return source[start:source.index('\n}', start) + 2]


def test_cinematic_capture():
    window = (ROOT / 'gpu/shim/window.cpp').read_text()
    source = r'''
#include "gpu/shim/host_input.h"
#include "gpu/shim/native_mouse_mailbox.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
using u64 = std::uint64_t;
constexpr unsigned SDL_EVENT_MOUSE_MOTION = 1, SDL_WINDOW_FULLSCREEN = 2;
static bool sdl_relative, overlay, pointer;
static unsigned mode_calls, flushes, queued_motion;
static u64 now = 1000;
static u64* settle;
static BbNativeMouse::Mailbox mailbox;
static std::uint64_t Epoch() { return mailbox.Epoch(); }
static void Capture(bool captured) {
    if (captured) {
        assert(!queued_motion); // flush must precede publication of host capture
        assert(*settle == now + 100); // as must the settle deadline
    }
    mailbox.SetCapture(captured);
}
static void Cancel() { mailbox.Cancel(); }
static HostInput host_input{Epoch, Capture, Cancel};
static bool SDL_GetWindowRelativeMouseMode(void*) { return sdl_relative; }
static bool SDL_SetWindowRelativeMouseMode(void*, bool capture) {
    ++mode_calls;
    sdl_relative = capture; // deliberately does not flush in same-mode case
    return true;
}
static const char* SDL_GetError() { return "stub"; }
static u64 SDL_GetTicks() { return now; }
static void SDL_FlushEvent(unsigned type) {
    assert(type == SDL_EVENT_MOUSE_MOTION);
    assert(!host_input.Captured());
    ++flushes;
    queued_motion = 0;
}
static unsigned SDL_GetWindowFlags(void*) { return 0; }
static void SDL_HideCursor() {}
static void SDL_ShowCursor() {}
int bbgpu_pointer_wanted() { return pointer; }
namespace BbOverlay {
static bool CapturesInput() { return overlay; }
static bool MenuOpen() { return overlay; }
}
namespace BbSettings {
struct Settings { bool mouse_capture = true; };
static Settings settings;
static Settings& Get() { return settings; }
}
class WindowSDL {
public:
    void* window{};
    std::atomic<bool> mouse_captured{false}, is_open{true};
    bool capture_error{}, relative_mouse{}, focused{true}, text_active{};
    bool mouse_user_released{}, pointer_shown{}, cursor_hidden{};
    std::mutex mouse_mutex;
    double mouse_dx{}, mouse_dy{};
    int wheel_up{}, wheel_down{};
    u64 capture_settle_ms{}, last_mouse_motion_ms{};
    void CaptureMouse(bool capture);
    void UpdateCursor();
};
''' + function(window, 'void WindowSDL::CaptureMouse(bool capture)') + '\n' + function(window, 'void WindowSDL::UpdateCursor()') + r'''
int main() {
    WindowSDL window;
    settle = &window.capture_settle_ms;
    mailbox.SetInstalled(true);
    queued_motion = 3;
    window.CaptureMouse(true);
    assert(host_input.Captured() && sdl_relative && window.mouse_captured);
    assert(flushes == 1 && !queued_motion);
    assert(mailbox.TryConsume(now).status == BbNativeMouse::ConsumeStatus::Empty);
    assert(mailbox.Submit(mailbox.Epoch(), 1, 2, now) == BbNativeMouse::SubmitResult::Native);
    const auto old_epoch = mailbox.Epoch();
    host_input.CinematicActive(true); // automatic ForceOn, no SDL event
    assert(!host_input.Captured() && !mailbox.state.has_pending);
    const auto calls = mode_calls;
    sdl_relative = false;
    window.CaptureMouse(true);
    window.UpdateCursor();
    assert(mode_calls == calls && !sdl_relative); // active never steals cinematic SDL mode
    sdl_relative = true;
    queued_motion = 5;
    window.mouse_dx = 9;
    window.wheel_up = 2;
    host_input.CinematicActive(false); // ForceOff: SDL/window intent remain true
    now += 500;
    window.UpdateCursor();
    assert(host_input.Captured() && sdl_relative && window.mouse_captured);
    assert(flushes == 2 && !queued_motion);
    assert(window.mouse_dx == 0 && window.wheel_up == 0);
    assert(window.capture_settle_ms == now + 100);
    assert(mailbox.Submit(old_epoch, 1, 2, now) == BbNativeMouse::SubmitResult::Discard);
    BbHostInput input{};
    host_input.Read(&input);
    assert(!input.mouse_x && !input.mouse_y && !input.blocked);
    // True same-owner no-op must not restart settle or flush new input each tick.
    window.UpdateCursor();
    assert(flushes == 2);
    host_input.CinematicActive(true);
    window.mouse_user_released = true; // F1 is processed before module filters
    window.CaptureMouse(false);
    assert(!sdl_relative && !window.mouse_captured && !host_input.Captured());
    host_input.CinematicActive(false);
    window.UpdateCursor();
    assert(!sdl_relative && !window.mouse_captured); // exit cannot undo explicit release
    window.mouse_user_released = false;
    window.CaptureMouse(true);
    host_input.CinematicActive(true);
    window.focused = false;
    window.UpdateCursor();
    assert(!sdl_relative && !window.mouse_captured);
    host_input.CinematicActive(false);
    window.UpdateCursor();
    host_input.Read(&input);
    assert(input.blocked && !host_input.Captured());
    window.focused = true;
    window.UpdateCursor();
    assert(!sdl_relative && !window.mouse_captured); // sync preserves released intent
    host_input.Read(&input);
    assert(!input.blocked);
    window.CaptureMouse(true);
    host_input.CinematicActive(true);
    overlay = true;
    window.UpdateCursor();
    host_input.CinematicActive(false);
    window.UpdateCursor();
    assert(!sdl_relative && !window.mouse_captured);
    overlay = false;
    window.CaptureMouse(true);
    host_input.CinematicActive(true);
    window.is_open = false;
    window.CaptureMouse(false); // close lifecycle path always releases SDL
    window.UpdateCursor();
    assert(!sdl_relative && !host_input.Captured());
}
'''
    with tempfile.TemporaryDirectory(prefix='bb-cinematic-capture-') as temporary:
        tmp = Path(temporary)
        cpp = tmp / 'test.cpp'
        cpp.write_text(source)
        binary = tmp / 'test'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                        '-Werror', '-pthread', '-I' + str(ROOT), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    test_cinematic_capture()
    print('PASS: production window capture resync, same-mode cinematic exit, F1/focus/menu/close')
