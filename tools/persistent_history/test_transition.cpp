// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_visual_transition.h"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using Vulkan::VisualTransition;
    using Vulkan::RequiredTransitionPasses;
    VisualTransition t;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    assert(!t.active && t.Due(1, 0) && t.Weight(1, 0) == 1.f);
    assert(!t.Begin(0, 0) && !t.active);
    assert(!t.Begin(1, nan) && !t.active);
    assert(!t.Begin(1, inf) && !t.active);
    assert(t.Begin(100, 10));
    assert(!t.Due(100, 10) && std::abs(t.Weight(100, 10) - 1.f / 3.f) < 1e-6f);
    assert(!t.Due(101, 26) && std::abs(t.Weight(101, 26) - 2.f / 3.f) < 1e-6f);
    assert(t.Due(102, 26) && t.Weight(102, 26) == 1.f);
    assert(!t.Due(101, 43.339));
    assert(t.Due(101, 43.341));
    assert(t.Weight(101, 40) > 2.f / 3.f);
    // Invalid/retrograde state cannot extend a transition or underflow source.
    assert(t.Due(99, 11) && t.Weight(99, 11) == 1.f);
    assert(t.Due(101, 9) && t.Due(101, nan) && t.Due(101, inf));
    t.ms = nan; assert(t.Due(101, 11));
    // Reset/gap/busy/stop cancellation has no implicit GPU work and no claim
    // that any physical bank lease completed. Caller must clear on these events.
    for (int event = 0; event < 4; ++event) {
        assert(t.Begin(200, 50));
        t.Clear();
        assert(!t.active && t.source == 0 && t.ms == 0);
        assert(t.Due(201, 60));
    }
    // Early replacement: caller records one finalize before Begin(new target).
    assert(t.Begin(300, 100));
    assert(!t.Due(301, 110));
    assert(RequiredTransitionPasses(t.active, 5, true) == 10);
    t.Clear(); assert(t.Begin(301, 110));
    assert(std::abs(t.Weight(301, 110) - 1.f / 3.f) < 1e-6f);
    // Expiry plus replacement uses that SAME finalize slot, not two.
    assert(t.Due(303, 120));
    assert(RequiredTransitionPasses(t.active, 5, true) == 10);
    for (unsigned links = 0; links <= 5; ++links)
        for (bool target : {false, true})
            for (bool stats : {false, true})
                assert(RequiredTransitionPasses(target, links, stats) <= 10);
    assert(RequiredTransitionPasses(false, 0, false) == 2);
    // Cadence mode has a real longer frame AND clock limit. Its source cadence
    // is independent of wall time and is still bounded if a clock stalls.
    t.cadence = true;
    assert(t.Begin(400, 1000));
    assert(std::abs(t.Weight(400, 1000) - 1.f / 7.f) < 1e-6f);
    assert(!t.Due(402, 1034));
    assert(!t.Due(405, 1083));
    assert(t.Due(406, 1083));
    assert(t.Due(401, 1100));
    assert(t.Due(399, 1001));
    t.Clear(); assert(t.cadence); // policy survives reset, ownership does not.
    t.cadence = false;
    assert(t.Begin(UINT64_MAX - 1, 0));
    assert(!t.Due(UINT64_MAX, 1));
    assert(t.Due(0, 2)); // wraparound is invalid, never extended lifetime.
}
