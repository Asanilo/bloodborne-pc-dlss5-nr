// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

// Shared renderer proof and telemetry; no experimental controller/transport ownership.
namespace BbNRRuntime {
enum class ExitState { NotRequested, Requested, Draining, NormalComplete, AbnormalHeld };
struct ExitProof {
    bool helper_or_launch_owned = false;
    bool retirement_audited = false;
    bool peer_reaped = false;
    bool current_input_finalized = false;
    bool native_last_use_complete = false;
    bool unknown_gpu_state = true;
};
// Pure proof predicate and local atomic handshake also exercise the real exit decision in
// CPU tests. A time limit or process disappearance alone never proves normal retirement.
inline bool NormalExitProof(const ExitProof& p) noexcept {
    return !p.unknown_gpu_state && p.current_input_finalized && p.native_last_use_complete &&
        (!p.helper_or_launch_owned || (p.retirement_audited && p.peer_reaped));
}
class ExitHandshake {
public:
    void Request() noexcept {
        auto expected=ExitState::NotRequested;
        state.compare_exchange_strong(expected,ExitState::Requested,std::memory_order_acq_rel);
    }
    void Draining() noexcept {
        auto expected=ExitState::Requested;
        state.compare_exchange_strong(expected,ExitState::Draining,std::memory_order_acq_rel);
    }
    bool PublishProof(const ExitProof& p) noexcept {
        if(!NormalExitProof(p)) return false;
        auto prior=state.load(std::memory_order_acquire);
        while(prior==ExitState::Requested || prior==ExitState::Draining)
            if(state.compare_exchange_weak(prior,ExitState::NormalComplete,std::memory_order_acq_rel))return true;
        return false;
    }
    ExitState Read() const noexcept {return state.load(std::memory_order_acquire);}
private:
    std::atomic<ExitState> state{ExitState::NotRequested};
};
// The window/UI thread only requests and reads. Proof publication is renderer-only.
void RequestExit() noexcept;
ExitState ReadExitState() noexcept;
void NoteExitDraining() noexcept;
bool PublishExitProof(const ExitProof&) noexcept;
inline constexpr uint32_t WindowExitWaitMs=120000; // independent bound, not GPU cancellation

// Separate local timing mailbox: controller state publication cannot overwrite it.
// Latest completed source is explicit; it is not necessarily the current UI frame.
struct SrTiming {
    bool enabled=false;
    uint64_t source_game_frame=0, completed_tick=0;
    std::optional<double> commands_gpu_ms;
    std::string reason;
};
void PublishSrTiming(SrTiming);
SrTiming ReadSrTiming();
} // namespace BbNRRuntime
