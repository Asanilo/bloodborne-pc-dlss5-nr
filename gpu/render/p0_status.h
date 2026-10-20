// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "nr_runtime_status.h"
#include <cstdint>
#include <atomic>
#include <optional>
#include <string>

// UI requests contain no Vulkan or process ownership. The GPU/render controller alone
// publishes applied state after satisfying its current-frame/last-use proof.
namespace BbP0 {
// Compatibility names for opt-in legacy controllers and their existing CPU tests.
using BbNRRuntime::ExitState;
using BbNRRuntime::ExitProof;
using BbNRRuntime::NormalExitProof;
using BbNRRuntime::ExitHandshake;
using BbNRRuntime::RequestExit;
using BbNRRuntime::ReadExitState;
using BbNRRuntime::NoteExitDraining;
using BbNRRuntime::PublishExitProof;
using BbNRRuntime::WindowExitWaitMs;
using BbNRRuntime::SrTiming;
using BbNRRuntime::PublishSrTiming;
using BbNRRuntime::ReadSrTiming;
enum class State { Disabled, Starting, Ready, Active, Idle, Draining, Quarantined, Failed };

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
} // namespace BbP0
