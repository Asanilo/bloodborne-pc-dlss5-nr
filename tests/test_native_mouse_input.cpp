// SPDX-License-Identifier: GPL-2.0-or-later
// The real global router and HostInput must reject a snapshot taken before cancellation,
// even while native installation is disabled and ordinary fresh motion falls back to sticks.
#include "gpu/shim/bbport_mouse_look.h"
#include "gpu/shim/host_input.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>

int main() {
    unsetenv("BB_NATIVE_MOUSE");
    BbMouseLook::PatchImage(nullptr, 0, nullptr, 0);
    assert(!BbMouseLook::Available());
    HostInput input{BbMouseLook::Epoch, BbMouseLook::SetCapture, BbMouseLook::Cancel};
    input.Capture(true);
    input.Motion(4, 2);
    BbHostInput old{};
    input.Read(&old);
    assert(old.mouse_x == 4 && old.mouse_y == 2);
    BbMouseLook::Cancel();
    assert(BbMouseLook::Submit(old.mouse_epoch, 0.003f, 0.006f) == BbMouseLook::Route::Dropped);
    input.Motion(5, 3);
    BbHostInput fresh{};
    input.Read(&fresh);
    assert(fresh.mouse_epoch != old.mouse_epoch);
    assert(BbMouseLook::Submit(fresh.mouse_epoch, 0.0045f, 0.0075f) == BbMouseLook::Route::Stick);
    assert(BbMouseLook::Submit(fresh.mouse_epoch, 30.f, -30.f) == BbMouseLook::Route::Stick);
    assert(BbMouseLook::Submit(fresh.mouse_epoch, std::numeric_limits<float>::quiet_NaN(), 0) ==
           BbMouseLook::Route::Dropped);
    input.Motion(100, 100);
    input.Capture(false);
    input.Read(&fresh);
    assert(!fresh.mouse_x && !fresh.mouse_y);
    assert(BbMouseLook::Submit(BbMouseLook::Epoch(), 0.1f, 0) == BbMouseLook::Route::Dropped);
    input.Capture(true);
    input.Motion(3, 2);
    input.Block(true);
    input.Read(&fresh);
    assert(fresh.blocked && !fresh.mouse_x && !fresh.mouse_y);
    assert(BbMouseLook::Submit(BbMouseLook::Epoch(), 0.1f, 0) == BbMouseLook::Route::Dropped);
    puts("PASS: actual HostInput snapshot -> epoch cancellation -> real fallback router");
}
