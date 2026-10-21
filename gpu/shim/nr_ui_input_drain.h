// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

namespace BbNRUiInput {
enum Key : uint32_t {
    Enter=1u<<0, KeypadEnter=1u<<1, Space=1u<<2, Escape=1u<<3,
    Delete=1u<<4, MouseLeft=1u<<5, South=1u<<6, L3=1u<<7, R3=1u<<8,
    Gamepad=South|L3|R3, All=Enter|KeypadEnter|Space|Escape|Delete|MouseLeft|Gamepad
};
// Event and close operations share the overlay mutex. Only Active() is read by
// game polling threads. held_ records SDL receipt, never ImGui's trickled frame.
class Drain {
public:
    bool Event(uint32_t key,bool down) noexcept {
        const bool consumed_release=!down&&(draining_.load()&key)!=0;
        if(down) {
            held_|=key;
            if(draining_.load()!=0)draining_.fetch_or(key);
        } else {
            held_&=~key;
            draining_.fetch_and(~key);
        }
        return consumed_release;
    }
    void Arm() noexcept {draining_.store(held_);}
    void Clear(uint32_t keys=All) noexcept {held_&=~keys;draining_.fetch_and(~keys);}
    bool Active() const noexcept {return draining_.load()!=0;}
private:
    uint32_t held_=0;
    std::atomic<uint32_t> draining_{0};
};
}
