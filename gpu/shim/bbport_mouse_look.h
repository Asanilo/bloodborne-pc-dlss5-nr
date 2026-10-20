// SPDX-License-Identifier: GPL-2.0-or-later
//
// Candidate native mouse look.  Production entry points are intentionally small: the
// coordinator owns HostInput/window integration.  `Segment::address`/`size` are image-relative
// (offsets into the `image` pointer), with ELF program-header flags (PF_X=1, PF_W=2, PF_R=4).

#pragma once

#include <cstddef>
#include <cstdint>

#include "native_mouse_mailbox.h"

namespace BbFrameRate { struct CameraRedirectProof; }

namespace BbMouseLook {

struct Segment {
    std::uint64_t address;
    std::uint64_t size;
    std::uint64_t flags;
};

void PatchImage(unsigned char* image, std::size_t size, const Segment* segments,
                std::size_t count);
/// Before the DLC/FrameRate image writers: validate and snapshot the original camera range.
bool PreAttest(unsigned char* image, std::size_t size, const Segment* segments,
               std::size_t count, std::uint64_t fps_sequence);
/// Production install after the writers; proof may be null when the camera is unredirected.
void PatchImageWithProof(unsigned char* image, std::size_t size, const Segment* segments,
                         std::size_t count,
                         const BbFrameRate::CameraRedirectProof* proof);

std::uint64_t Epoch();
void SetCapture(bool active);
void Cancel();

enum class Route : std::uint32_t {
    Dropped = 0,
    NativeQueued = 1,
    Stick = 2,
};

Route Submit(std::uint64_t epoch, float pitch, float yaw);
/// Captured and still in this exact epoch, for a stick-only consumer; never enqueues native work.
bool StickOnly(std::uint64_t epoch);

bool Available();
const char* Status();

namespace TestSeam {

struct Profile {
    std::uint64_t camera_start = 0;
    std::uint64_t camera_end = 0;
    std::uint64_t constants_start = 0;
    std::uint64_t constants_end = 0;
    std::uint64_t site_start = 0;
    std::uint64_t camera_fnv1a64 = 0;
    std::uint64_t constants_fnv1a64 = 0;
    const unsigned char* site_bytes = nullptr;
    std::size_t site_size = 16;
};

enum class Fault {
    None = 0,
    MmapFailure,
    MprotectFailure,
    Rel32Failure,
};

enum class Capability {
    Supported = 0,
    NoSse41,
    NoAvx,
    NoOsxsave,
    NoXcr0,
    NoClock,
    UnsupportedPlatform,
};

struct Artifact {
    bool ok = false;
    const char* status = "not attempted";
    void* stub = nullptr;
    std::size_t stub_size = 0;
    std::size_t page_size = 0;
    unsigned char* site = nullptr;
    unsigned char original_site[16] = {};
};

// Builds, maps and publishes a stub for a synthetic image/profile.  This is the shared
// internal installer with an explicit test profile; production `PatchImage` only ever passes
// its hard-coded profile.  UninstallForTest is teardown-only: stop all executions, then destroy
// the whole synthetic fixture.  It does not restore the site and is not a production hot-uninstall.
Artifact InstallForTest(unsigned char* image, std::size_t size, const Segment* segments,
                        std::size_t count, const Profile& profile,
                        BbNativeMouse::Mailbox* mailbox, Fault fault = Fault::None,
                        Capability capability = Capability::Supported,
                        bool post_validated = false);
void UninstallForTest(Artifact& artifact);
bool Rel32ForTest(std::uintptr_t from_after_instruction, std::uintptr_t target,
                  std::int32_t& displacement);
bool ValidateProductionCameraForTest(const unsigned char* camera, std::size_t size);
bool PreAttestForTest(unsigned char* image, std::size_t size, const Segment* segments,
                      std::size_t count, const Profile& profile, std::uint64_t fps_sequence,
                      bool check_fps_sites);
const char* ValidatePostForTest(unsigned char* image, std::size_t size, const Segment* segments,
                                std::size_t count, const Profile& profile,
                                const BbFrameRate::CameraRedirectProof* proof);
void ForceModuleInstalledForTest();
void ResetModuleStateForTest();

// Installs the hard-coded production profile for tests that have a real/synthetic image copy.
// Still subject to the strict BB_NATIVE_MOUSE=1 environment gate.
bool PatchImageForTest(unsigned char* image, std::size_t size, const Segment* segments,
                       std::size_t count);

} // namespace TestSeam

} // namespace BbMouseLook
