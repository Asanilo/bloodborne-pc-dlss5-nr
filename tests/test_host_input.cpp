#include "gpu/shim/host_input.h"
#include <cassert>

static std::uint64_t epoch = 1;
static unsigned capture_changes, cancellations;
static bool active;
static std::uint64_t CurrentEpoch() { return epoch; }
static void Capture(bool value) {
    if (active != value) { active = value; ++epoch; ++capture_changes; }
}
static void Cancel() { ++epoch; ++cancellations; }

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

    // Publish initial capture=false too (for example an initial SDL capture failure).
    // The mailbox initially permits legacy fallback, so its initial state can differ.
    active = true;
    HostInput initially_off{CurrentEpoch, Capture, Cancel};
    initially_off.Capture(false);
    assert(!active);
    HostInput tagged{CurrentEpoch, Capture, Cancel};
    tagged.Capture(true);
    tagged.Motion(3, 4);
    tagged.Read(&state);
    const auto old = state.mouse_epoch;
    assert(old == epoch && state.mouse_x == 3 && state.mouse_y == 4);
    // Consumer-side cancellation can happen without another window event/pad poll.
    // Read must discard queued old motion, never stamp the new epoch on its values.
    tagged.Motion(9, 8);
    ++epoch;
    tagged.Read(&state);
    assert(state.mouse_epoch == old && !state.mouse_x && !state.mouse_y);
    tagged.Motion(1, 2);
    ++epoch;
    tagged.Motion(5, 6);
    tagged.Read(&state);
    assert(state.mouse_epoch == epoch && state.mouse_x == 5 && state.mouse_y == 6);
    tagged.Motion(7, 8);
    tagged.Capture(false);
    tagged.Read(&state);
    assert(!state.mouse_x && !state.mouse_y && !active);
    tagged.Capture(true);
    tagged.Motion(1, 2);
    tagged.Block(true);
    tagged.Read(&state);
    assert(state.blocked && !state.mouse_x && !state.mouse_y && !active);
    tagged.Block(false);
    tagged.Capture(true);
    tagged.Motion(3, 4);
    tagged.CancelMotion();
    tagged.Read(&state);
    assert(!state.mouse_x && !state.mouse_y && cancellations == 1 && capture_changes >= 4);

    // Cinematic ownership is independent of focus/menu blocking. It also works
    // when a game-thread force toggle arrives without any new SDL events.
    tagged.Key(26, true);
    tagged.Button(1, true);
    tagged.Motion(10, 20);
    tagged.Wheel(1, 1);
    const auto before_cine = epoch;
    tagged.CinematicActive(true);
    assert(tagged.CinematicActive() && !active && epoch > before_cine);
    const auto after_cine = epoch;
    tagged.CinematicActive(true);
    assert(epoch == after_cine); // a repeated setter is not another mode change
    tagged.Block(true);
    tagged.Block(false);
    tagged.Capture(true);
    tagged.Key(26, true);
    tagged.Button(1, true);
    tagged.Motion(100, 100);
    tagged.Wheel(1, 1);
    tagged.Read(&state);
    assert(!state.blocked); // physical controller remains available
    assert(!state.keys[26] && !state.mouse_captured && !state.mouse_buttons);
    assert(!state.mouse_x && !state.mouse_y && !state.wheel_up && !state.wheel_down);
    tagged.CinematicActive(false);
    tagged.Read(&state);
    assert(!state.keys[26] && !state.mouse_captured && !state.mouse_x);
    tagged.Capture(true);
    tagged.Key(26, true);
    tagged.Motion(2, 3);
    tagged.Read(&state);
    assert(state.keys[26] && state.mouse_captured && state.mouse_x == 2 && state.mouse_y == 3);
    tagged.Block(true);
    tagged.CinematicActive(true);
    tagged.CinematicActive(false);
    tagged.Read(&state);
    assert(state.blocked); // cinematic release cannot remove a real focus/menu block

}
