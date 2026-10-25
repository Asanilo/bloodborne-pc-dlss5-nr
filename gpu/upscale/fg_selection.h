// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace Vulkan::FgSelection {
inline constexpr std::array<std::string_view,5> Backends{
    "off", "fsr31", "dlssg", "smooth-motion", "fsr316"};
inline int Index(std::string_view name) {
    for (unsigned i=0;i<Backends.size();++i) if (Backends[i]==name) return int(i);
    return 0;
}
inline const char* Token(int index) {
    return Backends[index>=0 && unsigned(index)<Backends.size()?index:0].data();
}
inline bool Multiplier(unsigned value) { return value>=2 && value<=6; }
}
