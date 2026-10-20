// SPDX-License-Identifier: GPL-2.0-or-later
#include "nr_runtime_status.h"
#include <mutex>
#include <utility>
namespace BbNRRuntime {
namespace {
std::mutex snapshot_mutex;
SrTiming sr_timing;
ExitHandshake exit_handshake;
}
void RequestExit() noexcept {exit_handshake.Request();}
ExitState ReadExitState() noexcept {return exit_handshake.Read();}
void NoteExitDraining() noexcept {exit_handshake.Draining();}
bool PublishExitProof(const ExitProof& p) noexcept {return exit_handshake.PublishProof(p);}
void PublishSrTiming(SrTiming value) {
    std::scoped_lock lock{snapshot_mutex};
    if (value.source_game_frame>=sr_timing.source_game_frame) sr_timing=std::move(value);
}
SrTiming ReadSrTiming() {
    std::scoped_lock lock{snapshot_mutex};
    return sr_timing;
}
} // namespace BbNRRuntime
