// CPU replay of the production cinematic input code and HostInput/mailbox ownership.
// Run with: python3 tests/test_cinematic_input.py
#include "gpu/shim/host_input.h"
#include "gpu/shim/native_mouse_mailbox.h"
#ifdef BBPORT_OFFLINE_CINEMATIC
#include "gpu/shim/bbport_cinematic_input.h"
#define Bbsc BbPortCinematic
#else
#include "core/bbsc_cinematic.h"
#endif
#include <SDL3/SDL.h>
#include <cassert>
#include <cstring>

static BbNativeMouse::Mailbox mailbox;
static std::uint64_t Epoch() { return mailbox.Epoch(); }
static void Capture(bool active) { mailbox.SetCapture(active); }
static void Cancel() { mailbox.Cancel(); }
static HostInput host{Epoch, Capture, Cancel};
static int notifications;
extern "C" void bbgpu_cinematic_input_active(int active) {
    ++notifications;
    host.CinematicActive(active != 0);
}

#ifdef BBNET_PORT_VERSION
static void neutral() {
    BbHostInput state{};
    host.Read(&state);
    // runtime_pad uses blocked to suppress the physical controller as well.
    assert(!state.blocked);
    for (const auto key : state.keys) assert(!key);
    assert(!state.mouse_captured && !state.mouse_buttons);
    assert(!state.mouse_x && !state.mouse_y && !state.wheel_up && !state.wheel_down);
}
#endif
static bool key(SDL_Keycode code, bool down = true, bool repeat = false) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = code;
    event.key.repeat = repeat;
    return Core::Bbsc::CinematicHandleEvent(event, nullptr);
}
#ifdef BBNET_PORT_VERSION
static void queued() {
    host.Capture(true);
    assert(mailbox.TryConsume(1000).status == BbNativeMouse::ConsumeStatus::Empty);
    host.Key(SDL_SCANCODE_W, true);
    host.Button(1, true);
    host.Motion(8, -4);
    host.Wheel(1, 0);
    assert(mailbox.Submit(mailbox.Epoch(), 1, 2, 1000) == BbNativeMouse::SubmitResult::Native);
}
static void late_input() {
    host.Block(true);
    host.Block(false); // an unrelated menu/focus update must not steal ownership
    host.Capture(true);
    host.Key(SDL_SCANCODE_W, true);
    host.Button(1, true);
    host.Motion(50, 60);
    host.Wheel(1, 1);
    neutral();
    assert(!mailbox.Captured());
}
#endif
int main() {
    using namespace Core::Bbsc;
#ifdef BBNET_PORT_VERSION
    constexpr auto toggle = SDLK_F6;
    assert(std::strcmp(CinematicToggleKeyName(), "F6") == 0);
    mailbox.SetInstalled(true);
    queued();
    const auto old_epoch = mailbox.Epoch();
    const auto sequence = mailbox.state.sequence;
    CinematicForceOn(); // no SDL event / window poll / controller poll required
    assert(TakeCinematicInput().enabled && host.CinematicActive());
    assert(mailbox.Epoch() > old_epoch && !mailbox.state.has_pending);
    assert(mailbox.state.cancelled_seq >= sequence);
    assert(mailbox.Submit(old_epoch, 1, 2, 1000) == BbNativeMouse::SubmitResult::Discard);
    assert(mailbox.TryConsume(1000).status == BbNativeMouse::ConsumeStatus::Empty);
    neutral();
    late_input();
    const int count = notifications;
    CinematicForceOn();
    assert(notifications == count);
    assert(!key(SDLK_F1)); // bbport owns F1 release
    assert(key(SDLK_W));
    assert(TakeCinematicInput().forward);
    CinematicForceOff(); // revive/failure, again no SDL events
    assert(!TakeCinematicInput().enabled && !host.CinematicActive());
    neutral(); // old key/motion may not revive on exit
    host.Motion(100, 100);
    neutral();
    queued(); // fresh capture/keypress restores native input
    BbHostInput fresh{};
    host.Read(&fresh);
    assert(fresh.keys[SDL_SCANCODE_W] && fresh.mouse_x == 8 && fresh.mouse_y == -4);
    const auto second_epoch = mailbox.Epoch();
    assert(key(toggle));
    assert(host.CinematicActive() && mailbox.Epoch() > second_epoch);
    neutral();
    late_input();
    assert(key(toggle, true, true)); // repeated keydown does not toggle
    assert(TakeCinematicInput().enabled);
    assert(key(toggle, false));
    assert(TakeCinematicInput().enabled);
    assert(key(toggle));
    assert(!TakeCinematicInput().enabled && !host.CinematicActive());
    neutral();
#else
    constexpr auto toggle = SDLK_F1;
    assert(std::strcmp(CinematicToggleKeyName(), "F1") == 0);
    assert(key(toggle));
    assert(TakeCinematicInput().enabled);
    CinematicForceOff();
    assert(!TakeCinematicInput().enabled);
    CinematicForceOn();
    assert(TakeCinematicInput().enabled);
    assert(key(toggle));
    assert(!TakeCinematicInput().enabled);
    assert(notifications == 0); // upstream/mod have no dependency on the bbport API
#endif
}
