// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstring>

namespace Vulkan {
// Lifecycle/ownership proofs remain unconditional. Only per-frame text is gated.
inline bool VerbosePlayerFrames() {
    static const bool enabled=[] {
        const char* mode=std::getenv("BB_DEVELOPER_MODE");
        const char* profile=std::getenv("BB_FG_PROFILE");
        return !mode || std::strcmp(mode,"0")!=0 || (profile && std::strcmp(profile,"1")==0);
    }();
    return enabled;
}
}
