// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <string>
namespace BbNR {
inline constexpr uint32_t ApiVersion=2;
enum class Reprojection:uint32_t {Nearest=0,ClampedCubic=1};
struct Config {
 Reprojection reprojection=Reprojection::Nearest;
 bool display_bypass=false;
 float display_strength=1.f;
 uint32_t ttl_frames=12;
 float ttl_ms=200.f,depth_tolerance=.02f,colour_tolerance=.5f,min_confidence=.5f;
 uint32_t model_style=0;
 float model_intensity=1.f;
 bool hud=true;
 bool operator==(const Config&) const=default;
};
struct Actions {bool reset=false,capture=false,apply_model=false;};
struct Request {uint32_t version=ApiVersion;uint64_t revision=0,reset_seq=0,capture_seq=0,model_apply_seq=0;Config config{},model_config{};};
enum class Status {Unavailable,Pending,Applied,Failed,Held};
struct Snapshot {
 uint32_t version=ApiVersion;
 uint64_t requested_revision=0,applied_revision=0,model_applied_version=0;
 uint64_t reset_applied_seq=0,capture_applied_seq=0;
 Config actual_config{};
 Status status=Status::Unavailable;
 std::string reason="Renderer has not published runtime status";
 uint64_t current_source=0,candidate_origin=0,candidate_age=0,last_job=0;
 bool candidate_admitted=false,candidate_obsolete=false;
 bool capture_busy=false;
 // Set by renderer/owner when this Helper cannot safely retry in place.
 bool model_restart_required=false;
 // Independent completed Native GPU dispatch sample, never Present CPU timing.
 bool gpu_timing_available=false;
 std::array<double,6> history_pass_ms{}; // Clear, Capture, Advance, Adopt, Compose, Stats
 uint64_t gpu_timing_source=0,gpu_timing_revision=0,gpu_timing_monotonic_ns=0;
 bool model_refresh_available=false;
 double model_refresh_ms=0; // actual Native candidate-capture interval, not Helper estimate
 uint64_t model_refresh_source=0;
 bool gpu_sample_available=false;
 uint64_t gpu_sample_source=0,gpu_sample_monotonic_ns=0,gpu_sample_revision=0;
 float history_valid_rate=0,changed_fraction=0,actual_age_frames=0,actual_age_ms=0;
 uint64_t present_samples=0;
 double fps=0,p95_ms=0,p99_ms=0;
 bool timing_available=false;
};
// UI/config worker only. Explicit initialization loads a versioned private file;
// absent file restores the exact defaults. Renderer must never call this for I/O.
bool InitializeConfig(const std::string& path={},std::string* error=nullptr);
void ShutdownConfig();
bool Validate(const Config&,std::string* error=nullptr);
// Thread-safe data mailbox, no Vulkan/FD/fence operations or synchronous file I/O.
// Model sliders only stage values; apply_model increments only on an explicit action.
bool Submit(const Config&,Actions actions={},std::string* error=nullptr);
Request ReadRequest();
Snapshot ReadSnapshot();
// Renderer owner reports real applied state, never an optimistic UI acknowledgment.
bool PublishSnapshot(const Snapshot&,std::string* error=nullptr);
// Actual unique nonreuse successful swapchain Present only; CPU monotonic ns.
// UI EWMA and reused frames must never call this.
void NotifyPresent(uint64_t source,uint64_t present_number,uint64_t monotonic_ns);
// Defined here so renderer and tests share reset rules; never changes NR origin.
bool RequiresVisualReset(const Config& before,const Config& after);
const char* StatusName(Status);
}
