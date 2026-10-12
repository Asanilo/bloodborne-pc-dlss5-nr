// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <atomic>
#include <optional>
#include <string>

// UI requests contain no Vulkan or process ownership. The GPU/render controller alone
// publishes applied state after satisfying its current-frame/last-use proof.
namespace BbP0 {
enum class State { Disabled, Starting, Ready, Active, Idle, Draining, Quarantined, Failed };
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

const char* StateName(State value) noexcept;
struct EnableRequest {
    bool enabled = false;
    uint64_t revision = 0;
};
struct Snapshot {
    State state = State::Disabled;
    bool opt_in = false;
    bool applied_enable = false;
    bool gpu_completion_unknown = false;
    bool helper_exit_observed = false;
    uint64_t generation = 0;
    uint64_t frame = 0;
    uint64_t source_game_frame = 0;
    uint64_t finalized = 0;
    uint64_t accepted = 0;
    uint64_t fallbacks = 0;
    uint64_t timeouts = 0;
    uint64_t applied_request_revision = 0;
    std::optional<double> evaluate_gpu_ms;
    std::optional<double> hook_cpu_ms;
    std::string session;
    std::string reason;
    // Initial P0 controls are fixed. These are declarations, not UI adjustable parameters.
    unsigned style = 0;
    float intensity = 1.0f;
    float nr_mvec_scale = 1.0f;
    const char* parameter_scope = "EXPERIMENTAL_scale1_direct720_phase_none_manualWP1";
};
// Renderer initialization calls this once with its validated private launch opt-in.
// No environment lookup, helper spawn or allocation occurs on RequestEnable.
void InitializeRequest(bool opt_in, bool auto_enable) noexcept;
void RequestEnable(bool enabled) noexcept;
EnableRequest ReadRequest() noexcept;
// Called only by renderer/controller. No UI thread may use this to release resources.
void Publish(Snapshot value);
Snapshot ReadSnapshot();
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
} // namespace BbP0
