// SPDX-License-Identifier: GPL-2.0-or-later
//
// Candidate native mouse look: production installer and host-side routing.  This file never
// executes guest code itself; it validates the exact image context, publishes a leaf stub near
// the camera site and leaves all runtime consumption to that stub.

#include "bbport_mouse_look.h"

#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(__linux__) && defined(__x86_64__) && defined(__LP64__)
#include <cpuid.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace BbMouseLook {
namespace {

constexpr std::uint64_t kPfX = 1; // ELF PF_X
constexpr std::uint64_t kPfR = 4; // ELF PF_R

constexpr std::uint64_t kCameraStart = 0x143ac60;
constexpr std::uint64_t kCameraEnd = 0x143fad0;
constexpr std::uint64_t kConstantsStart = 0x48f25f0;
constexpr std::uint64_t kConstantsEnd = 0x48f2630;
constexpr std::uint64_t kSiteStart = 0x143cde0;
constexpr std::uint64_t kCameraFnv1a64 = 0xec5d03c6452b92a4ULL;
constexpr std::uint64_t kConstantsFnv1a64 = 0x7c77e08196c17e8dULL;
constexpr unsigned char kSiteBytes[16] = {
    0xc5, 0xa8, 0x59, 0xc9,
    0xc5, 0xf8, 0x28, 0x95, 0x10, 0xfc, 0xff, 0xff,
    0xc5, 0xe8, 0x59, 0xc9,
};

// Bytes generated from gpu/shim/native_mouse_stub_template.S.  The final 16 bytes are the two
// 64-bit patch slots (mailbox pointer, continuation pointer).
constexpr std::size_t kStubMailboxSlot = 920;
constexpr std::size_t kStubContinuationSlot = 928;
constexpr unsigned char kStubTemplate[] = {
    0xc5, 0xa8, 0x59, 0xc9, 0xc5, 0xf8, 0x28, 0x95, 0x10, 0xfc, 0xff, 0xff, 0xc5, 0xe8, 0x59, 0xc9,
    0x48, 0x8d, 0xa4, 0x24, 0x00, 0xfe, 0xff, 0xff, 0x9c, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55,
    0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xec, 0x40, 0xf3, 0x0f, 0x7f, 0x14, 0x24, 0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x10, 0x0f,
    0xae, 0x5c, 0x24, 0x20, 0x8b, 0x44, 0x24, 0x20, 0x25, 0x80, 0x1f, 0x00, 0x00, 0x3d, 0x80, 0x1f,
    0x00, 0x00, 0x0f, 0x85, 0x05, 0x03, 0x00, 0x00, 0x48, 0x8b, 0x1d, 0x39, 0x03, 0x00, 0x00, 0x31,
    0xc0, 0xba, 0x01, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0xb1, 0x13, 0x0f, 0x85, 0xed, 0x02, 0x00, 0x00,
    0x83, 0x7b, 0x1c, 0x00, 0x0f, 0x85, 0xdb, 0x02, 0x00, 0x00, 0x83, 0x7b, 0x04, 0x00, 0x0f, 0x84,
    0xd1, 0x02, 0x00, 0x00, 0x83, 0x7b, 0x08, 0x00, 0x0f, 0x84, 0xc7, 0x02, 0x00, 0x00, 0xb8, 0xe4,
    0x00, 0x00, 0x00, 0xbf, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x74, 0x24, 0x28, 0x0f, 0x05, 0x48,
    0x85, 0xc0, 0x0f, 0x85, 0xad, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x44, 0x24, 0x28, 0x48, 0x85, 0xc0,
    0x0f, 0x88, 0x9f, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x54, 0x24, 0x30, 0x48, 0x85, 0xd2, 0x0f, 0x88,
    0x91, 0x02, 0x00, 0x00, 0x48, 0x81, 0xfa, 0x00, 0xca, 0x9a, 0x3b, 0x0f, 0x83, 0x84, 0x02, 0x00,
    0x00, 0x48, 0xc7, 0xc1, 0x40, 0x42, 0x0f, 0x00, 0x48, 0xf7, 0xe1, 0x48, 0x85, 0xd2, 0x0f, 0x85,
    0x71, 0x02, 0x00, 0x00, 0x49, 0x89, 0xc0, 0x48, 0x8b, 0x44, 0x24, 0x30, 0x31, 0xd2, 0xb9, 0xe8,
    0x03, 0x00, 0x00, 0x48, 0xf7, 0xf1, 0x49, 0x01, 0xc0, 0x0f, 0x82, 0x56, 0x02, 0x00, 0x00, 0x48,
    0x8b, 0x43, 0x50, 0x49, 0x39, 0xc0, 0x0f, 0x82, 0x6d, 0x01, 0x00, 0x00, 0x4c, 0x89, 0x43, 0x50,
    0x83, 0x7b, 0x14, 0x00, 0x0f, 0x84, 0xd7, 0x00, 0x00, 0x00, 0x83, 0x7b, 0x0c, 0x00, 0x0f, 0x84,
    0x4d, 0x01, 0x00, 0x00, 0x48, 0x8b, 0x43, 0x48, 0x49, 0x39, 0xc0, 0x0f, 0x82, 0x40, 0x01, 0x00,
    0x00, 0x4d, 0x89, 0xc1, 0x49, 0x29, 0xc1, 0x49, 0x81, 0xf9, 0xa0, 0x86, 0x01, 0x00, 0x0f, 0x83,
    0x2d, 0x01, 0x00, 0x00, 0x48, 0x8b, 0x43, 0x60, 0x48, 0x83, 0xf8, 0xff, 0x0f, 0x84, 0xa3, 0x01,
    0x00, 0x00, 0x8b, 0x43, 0x58, 0x8b, 0x53, 0x5c, 0x41, 0x89, 0xc2, 0x41, 0x81, 0xe2, 0xff, 0xff,
    0xff, 0x7f, 0x41, 0x89, 0xd3, 0x41, 0x81, 0xe3, 0xff, 0xff, 0xff, 0x7f, 0x44, 0x89, 0xd1, 0x44,
    0x09, 0xd9, 0x45, 0x85, 0xd2, 0x74, 0x08, 0x66, 0x0f, 0x6e, 0xd0, 0xf3, 0x0f, 0x58, 0xca, 0x45,
    0x85, 0xdb, 0x74, 0x15, 0x66, 0x0f, 0x6e, 0xd2, 0x0f, 0x28, 0xd9, 0x0f, 0xc6, 0xdb, 0x55, 0xf3,
    0x0f, 0x58, 0xda, 0x66, 0x0f, 0x3a, 0x21, 0xcb, 0x10, 0x85, 0xc9, 0x74, 0x1d, 0xc7, 0x85, 0x3c,
    0xfc, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x41, 0xc6, 0x85, 0x5c, 0x01, 0x00, 0x00, 0x01, 0x41,
    0xc7, 0x85, 0x54, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x43, 0x30, 0x48, 0x89,
    0x43, 0x38, 0xc7, 0x43, 0x14, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x28, 0x00, 0x00, 0x00,
    0x00, 0x48, 0xc7, 0x43, 0x30, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x48, 0x00, 0x00, 0x00,
    0x00, 0xc7, 0x43, 0x58, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x43, 0x5c, 0x00, 0x00, 0x00, 0x00, 0xeb,
    0x5f, 0x48, 0x8b, 0x43, 0x60, 0x48, 0x83, 0xf8, 0xff, 0x0f, 0x84, 0xf6, 0x00, 0x00, 0x00, 0x48,
    0xff, 0xc0, 0x48, 0x89, 0x43, 0x60, 0x48, 0x8b, 0x53, 0x68, 0x48, 0x39, 0xd0, 0x0f, 0x86, 0x42,
    0x01, 0x00, 0x00, 0x48, 0x8b, 0x53, 0x70, 0x48, 0x39, 0xd0, 0x0f, 0x86, 0x35, 0x01, 0x00, 0x00,
    0x83, 0x7b, 0x0c, 0x00, 0x75, 0x14, 0x8b, 0x43, 0x10, 0x83, 0xf8, 0xff, 0x0f, 0x84, 0xc3, 0x00,
    0x00, 0x00, 0xff, 0xc0, 0x89, 0x43, 0x10, 0x89, 0x43, 0x18, 0x4c, 0x89, 0x43, 0x40, 0xc7, 0x43,
    0x0c, 0x01, 0x00, 0x00, 0x00, 0xc7, 0x03, 0x00, 0x00, 0x00, 0x00, 0xe9, 0x0d, 0x01, 0x00, 0x00,
    0x48, 0x8b, 0x43, 0x60, 0x48, 0xff, 0xc0, 0x48, 0x89, 0x43, 0x60, 0x4c, 0x89, 0x43, 0x40, 0xc7,
    0x43, 0x0c, 0x01, 0x00, 0x00, 0x00, 0xc7, 0x03, 0x00, 0x00, 0x00, 0x00, 0xe9, 0xec, 0x00, 0x00,
    0x00, 0x41, 0xb9, 0x01, 0x00, 0x00, 0x00, 0xeb, 0x07, 0x45, 0x31, 0xc9, 0x4c, 0x89, 0x43, 0x50,
    0x83, 0x7b, 0x14, 0x00, 0x74, 0x08, 0x48, 0x8b, 0x43, 0x30, 0x48, 0x89, 0x43, 0x78, 0xc7, 0x43,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x28, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x48, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x43, 0x58,
    0x00, 0x00, 0x00, 0x00, 0xc7, 0x43, 0x5c, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x43, 0x10, 0x83, 0xf8,
    0xff, 0x74, 0x32, 0xff, 0xc0, 0x89, 0x43, 0x10, 0x89, 0x43, 0x18, 0x48, 0x8b, 0x43, 0x60, 0x45,
    0x85, 0xc9, 0x75, 0x06, 0x48, 0x89, 0x43, 0x68, 0xeb, 0x04, 0x48, 0x89, 0x43, 0x70, 0xc7, 0x43,
    0x0c, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x40, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x03, 0x00,
    0x00, 0x00, 0x00, 0xeb, 0x68, 0xc7, 0x43, 0x1c, 0x01, 0x00, 0x00, 0x00, 0x83, 0x7b, 0x14, 0x00,
    0x74, 0x08, 0x48, 0x8b, 0x43, 0x30, 0x48, 0x89, 0x43, 0x78, 0xc7, 0x43, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x48, 0xc7, 0x43, 0x28, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x30, 0x00, 0x00, 0x00,
    0x00, 0x48, 0xc7, 0x43, 0x48, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x43, 0x58, 0x00, 0x00, 0x00, 0x00,
    0xc7, 0x43, 0x5c, 0x00, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x43, 0x40, 0x00, 0x00, 0x00, 0x00, 0xc7,
    0x43, 0x0c, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x43, 0x18, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x03, 0x00,
    0x00, 0x00, 0x00, 0xeb, 0x08, 0xc7, 0x03, 0x00, 0x00, 0x00, 0x00, 0xeb, 0x00, 0x0f, 0xae, 0x54,
    0x24, 0x20, 0xf3, 0x0f, 0x6f, 0x14, 0x24, 0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x10, 0x48, 0x83, 0xc4,
    0x40, 0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41,
    0x58, 0x5d, 0x5f, 0x5e, 0x5a, 0x59, 0x5b, 0x58, 0x9d, 0x48, 0x8d, 0xa4, 0x24, 0x00, 0x02, 0x00,
    0x00, 0xff, 0x25, 0x09, 0x00, 0x00, 0x00, 0x90, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};
static_assert(sizeof(kStubTemplate) == kStubContinuationSlot + sizeof(void*));

struct InstallProfile {
    std::uint64_t camera_start;
    std::uint64_t camera_end;
    std::uint64_t constants_start;
    std::uint64_t constants_end;
    std::uint64_t site_start;
    std::uint64_t camera_fnv1a64;
    std::uint64_t constants_fnv1a64;
    const unsigned char* site_bytes;
    std::size_t site_size;
};

const InstallProfile kProductionProfile = {
    kCameraStart, kCameraEnd, kConstantsStart, kConstantsEnd, kSiteStart,
    kCameraFnv1a64, kConstantsFnv1a64, kSiteBytes, sizeof(kSiteBytes),
};

enum class ModuleState : int {
    Idle = 0,
    Disabled = 1,
    Installing = 2,
    Installed = 3,
    Failed = 4,
    Unsupported = 5,
};

std::atomic<ModuleState> g_state{ModuleState::Idle};
std::atomic<const char*> g_status{"disabled"};
BbNativeMouse::Mailbox g_mailbox;

void set_status(const char* status) noexcept {
    g_status.store(status, std::memory_order_release);
}

bool env_enabled() noexcept {
#if defined(__linux__) && defined(__x86_64__) && defined(__LP64__)
    const char* value = std::getenv("BB_NATIVE_MOUSE");
    return value != nullptr && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}

#if defined(__linux__) && defined(__x86_64__) && defined(__LP64__)
static_assert(SYS_clock_gettime == 228, "stub hard-codes Linux x86-64 clock_gettime");

const char* cpu_os_supported_impl() noexcept {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return "unsupported: no CPUID";
    if ((ecx & bit_SSE4_1) == 0) return "unsupported: SSE4.1";
    if ((ecx & bit_AVX) == 0) return "unsupported: AVX";
    if ((ecx & bit_OSXSAVE) == 0) return "unsupported: OSXSAVE";
    unsigned xcr0_lo = 0, xcr0_hi = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0));
    (void)xcr0_hi;
    if ((xcr0_lo & 0x6u) != 0x6u) return "unsupported: XMM/YMM state";
    struct timespec ts {};
    if (syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts) != 0)
        return "unsupported: SYS_clock_gettime";
    return nullptr;
}
#else
const char* cpu_os_supported_impl() noexcept {
    return "unsupported: Linux x86-64 LP64 only";
}
#endif

const char* cached_cpu_support() noexcept {
    static const char* result = cpu_os_supported_impl();
    return result;
}

std::uint64_t fnv1a64(const unsigned char* data, std::size_t size) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool range_covered(const Segment* segments, std::size_t count, std::uint64_t image_size,
                   std::uint64_t start, std::uint64_t length, std::uint64_t flags) noexcept {
    if (segments == nullptr || length == 0) return false;
    for (std::size_t i = 0; i < count; ++i) {
        const Segment& segment = segments[i];
        if ((segment.flags & flags) != flags) continue;
        if (segment.size < length) continue;
        if (segment.address > start) continue;
        if (start - segment.address > segment.size - length) continue;
        if (segment.address > image_size) continue;
        if (segment.size > image_size - segment.address) continue;
        return true;
    }
    return false;
}

bool valid_profile(const InstallProfile& profile) noexcept {
    if (profile.camera_start >= profile.camera_end) return false;
    if (profile.constants_start >= profile.constants_end) return false;
    if (profile.site_bytes == nullptr || profile.site_size != 16) return false;
    if (profile.site_start < profile.camera_start) return false;
    if (profile.site_start > profile.camera_end) return false;
    if (profile.camera_end - profile.site_start < 16) return false;
    return true;
}

constexpr std::uint64_t kGuestPage = 0x1000; // loader page granularity

std::uint64_t page_down(std::uint64_t value) noexcept {
    return value & ~(kGuestPage - 1);
}

bool page_up(std::uint64_t value, std::uint64_t& result) noexcept {
    if (value > UINT64_MAX - (kGuestPage - 1)) return false;
    result = (value + kGuestPage - 1) & ~(kGuestPage - 1);
    return true;
}

bool overlaps(std::uint64_t a_start, std::uint64_t a_end, std::uint64_t b_start,
              std::uint64_t b_end) noexcept {
    return a_start < b_end && b_start < a_end;
}

const char* validate_image(unsigned char* image, std::size_t size, const Segment* segments,
                           std::size_t count, const InstallProfile& profile) noexcept {
    if (image == nullptr || size == 0) return "failed: null or empty image";
    if (segments == nullptr || count == 0) return "failed: no segments";
    if (!valid_profile(profile)) return "failed: invalid profile";
    const std::uintptr_t image_address = reinterpret_cast<std::uintptr_t>(image);
    if (size > UINTPTR_MAX - image_address) return "failed: image address overflow";
    if (profile.camera_end > size) return "failed: image too small for camera";
    if (profile.constants_end > size) return "failed: image too small for constants";

    // Any out-of-bounds segment can still be protected by the loader; reject it.
    for (std::size_t i = 0; i < count; ++i) {
        const Segment& segment = segments[i];
        if (segment.size == 0) continue;
        if (segment.address > size || segment.size > size - segment.address)
            return "failed: segment out of bounds";
    }

    std::uint64_t camera_page_start = page_down(profile.camera_start);
    std::uint64_t camera_page_end = 0;
    std::uint64_t constants_page_start = page_down(profile.constants_start);
    std::uint64_t constants_page_end = 0;
    if (!page_up(profile.camera_end, camera_page_end) ||
        !page_up(profile.constants_end, constants_page_end))
        return "failed: page range overflow";

    // Loader protects whole pages in listed order.  Require the entire camera/constant page
    // ranges to be covered and reject any overlapping segment that would revoke the required
    // permission on those pages (including a partial last-page overlap).
    if (!range_covered(segments, count, size, camera_page_start,
                       camera_page_end - camera_page_start, kPfX))
        return "failed: camera pages not executable";
    if (!range_covered(segments, count, size, constants_page_start,
                       constants_page_end - constants_page_start, kPfR))
        return "failed: constants pages not readable";
    for (std::size_t i = 0; i < count; ++i) {
        const Segment& segment = segments[i];
        if (segment.size == 0) continue;
        const std::uint64_t segment_end = segment.address + segment.size;
        if (overlaps(segment.address, segment_end, camera_page_start, camera_page_end) &&
            (segment.flags & kPfX) == 0)
            return "failed: ambiguous camera page overlap";
        if (overlaps(segment.address, segment_end, constants_page_start, constants_page_end) &&
            (segment.flags & kPfR) == 0)
            return "failed: ambiguous constants page overlap";
    }
    if (!range_covered(segments, count, size, profile.site_start, 16, kPfX))
        return "failed: site not executable";

    const unsigned char* camera = image + profile.camera_start;
    const unsigned char* constants = image + profile.constants_start;
    if (fnv1a64(camera, static_cast<std::size_t>(profile.camera_end - profile.camera_start)) !=
        profile.camera_fnv1a64)
        return "failed: camera FNV mismatch";
    if (fnv1a64(constants,
                static_cast<std::size_t>(profile.constants_end - profile.constants_start)) !=
        profile.constants_fnv1a64)
        return "failed: constants FNV mismatch";
    if (std::memcmp(image + profile.site_start, profile.site_bytes, 16) != 0)
        return "failed: site signature mismatch";
    return nullptr;
}

bool rel32_to(const void* from_after_instruction, const void* target,
              std::int32_t& displacement) noexcept {
    const std::uintptr_t from = reinterpret_cast<std::uintptr_t>(from_after_instruction);
    const std::uintptr_t to = reinterpret_cast<std::uintptr_t>(target);
    if (to >= from) {
        const std::uint64_t delta = static_cast<std::uint64_t>(to - from);
        if (delta > 0x7fffffffuLL) return false;
        displacement = static_cast<std::int32_t>(delta);
    } else {
        const std::uint64_t delta = static_cast<std::uint64_t>(from - to);
        if (delta > 0x80000000uLL) return false;
        displacement = delta == 0x80000000uLL
                           ? std::numeric_limits<std::int32_t>::min()
                           : -static_cast<std::int32_t>(delta);
    }
    return true;
}

const char* capability_error(TestSeam::Capability capability) noexcept {
    switch (capability) {
    case TestSeam::Capability::Supported: return nullptr;
    case TestSeam::Capability::NoSse41: return "failed: injected SSE4.1 unsupported";
    case TestSeam::Capability::NoAvx: return "failed: injected AVX unsupported";
    case TestSeam::Capability::NoOsxsave: return "failed: injected OSXSAVE unsupported";
    case TestSeam::Capability::NoXcr0: return "failed: injected XCR0 unsupported";
    case TestSeam::Capability::NoClock: return "failed: injected clock_gettime unavailable";
    case TestSeam::Capability::UnsupportedPlatform: return "failed: injected unsupported platform";
    }
    return "failed: injected capability";
}

TestSeam::Artifact install_impl(unsigned char* image, std::size_t size, const Segment* segments,
                                std::size_t count, const InstallProfile& profile,
                                BbNativeMouse::Mailbox* mailbox,
                                TestSeam::Fault fault = TestSeam::Fault::None,
                                TestSeam::Capability capability =
                                    TestSeam::Capability::Supported) noexcept {
    TestSeam::Artifact artifact{};
    if (mailbox == nullptr) {
        artifact.status = "failed: null mailbox";
        return artifact;
    }
    const char* invalid = validate_image(image, size, segments, count, profile);
    if (invalid != nullptr) {
        artifact.status = invalid;
        return artifact;
    }
    const char* capability_failure = capability_error(capability);
    if (capability_failure != nullptr) {
        artifact.status = capability_failure;
        return artifact;
    }
    const char* support = cached_cpu_support();
    if (support != nullptr) {
        artifact.status = support;
        return artifact;
    }

#if defined(__linux__) && defined(__x86_64__) && defined(__LP64__)
    if (fault == TestSeam::Fault::Rel32Failure) {
        artifact.status = "failed: injected rel32 failure";
        return artifact;
    }
    const long page_long = sysconf(_SC_PAGESIZE);
    const std::size_t page = page_long > 0 ? static_cast<std::size_t>(page_long) : 4096;
    const std::size_t mapping_size = sizeof(kStubTemplate) > page ? sizeof(kStubTemplate) : page;
    unsigned char* site = image + profile.site_start;
    const std::uintptr_t site_address = reinterpret_cast<std::uintptr_t>(site);

    const std::intptr_t hints[] = {
        0x1000000LL, -0x1000000LL, 0x4000000LL, -0x4000000LL, 0x10000000LL,
        -0x10000000LL, 0x40000000LL, -0x40000000LL,
    };
    void* mapping = nullptr;
    if (fault == TestSeam::Fault::MmapFailure) {
        artifact.status = "failed: injected mmap failure";
        return artifact;
    }
    for (std::intptr_t offset : hints) {
        std::uintptr_t hint = site_address;
        if (offset >= 0) {
            const auto delta = static_cast<std::uintptr_t>(offset);
            if (hint > UINTPTR_MAX - delta) continue;
            hint += delta;
        } else {
            const auto delta = static_cast<std::uintptr_t>(-offset);
            if (hint < delta) continue;
            hint -= delta;
        }
        hint &= ~static_cast<std::uintptr_t>(page - 1);
        void* candidate = mmap(reinterpret_cast<void*>(hint), mapping_size,
                               PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (candidate == MAP_FAILED) continue;
        std::int32_t probe = 0;
        if (!rel32_to(site + 5, candidate, probe)) {
            munmap(candidate, mapping_size);
            continue;
        }
        mapping = candidate;
        break;
    }
    if (mapping == nullptr) {
        artifact.status = "failed: no near stub mapping";
        return artifact;
    }

    std::memcpy(mapping, kStubTemplate, sizeof(kStubTemplate));
    void* mailbox_state = &mailbox->state;
    void* continuation = site + 16;
    std::memcpy(static_cast<unsigned char*>(mapping) + kStubMailboxSlot, &mailbox_state,
                sizeof(mailbox_state));
    std::memcpy(static_cast<unsigned char*>(mapping) + kStubContinuationSlot, &continuation,
                sizeof(continuation));
    __builtin___clear_cache(static_cast<char*>(mapping),
                            static_cast<char*>(mapping) + sizeof(kStubTemplate));

    std::int32_t site_disp = 0;
    if (!rel32_to(site + 5, mapping, site_disp)) {
        munmap(mapping, mapping_size);
        artifact.status = "failed: stub rel32 out of range";
        return artifact;
    }
    if (fault == TestSeam::Fault::MprotectFailure ||
        mprotect(mapping, mapping_size, PROT_READ | PROT_EXEC) != 0) {
        munmap(mapping, mapping_size);
        artifact.status = fault == TestSeam::Fault::MprotectFailure
                              ? "failed: injected mprotect failure"
                              : "failed: stub mprotect RX";
        return artifact;
    }

    // All validation, mapping and RX publication succeeded.  Perform the final mailbox epoch
    // transition under its gate BEFORE publishing the site; if that gate rejects (fatal/epoch
    // exhausted), the site remains completely unchanged and the stub mapping is released.
    if (std::memcmp(site, profile.site_bytes, 16) != 0) {
        munmap(mapping, mapping_size);
        artifact.status = "failed: site changed during install";
        return artifact;
    }
    if (!mailbox->InstallForCamera()) {
        munmap(mapping, mapping_size);
        artifact.status = "failed: mailbox cannot install/advance epoch";
        return artifact;
    }

    unsigned char patch[16] = {};
    patch[0] = 0xe9;
    std::memcpy(patch + 1, &site_disp, sizeof(site_disp));
    std::memset(patch + 5, 0x90, 11);
    std::memcpy(site, patch, sizeof(patch));
    __builtin___clear_cache(reinterpret_cast<char*>(site), reinterpret_cast<char*>(site) + 16);

    artifact.ok = true;
    artifact.status = "installed";
    artifact.stub = mapping;
    artifact.stub_size = sizeof(kStubTemplate);
    artifact.page_size = mapping_size;
    artifact.site = site;
    std::memcpy(artifact.original_site, profile.site_bytes, 16);
    return artifact;
#else
    (void)image;
    (void)size;
    (void)segments;
    (void)count;
    artifact.status = "unsupported: Linux x86-64 LP64 only";
    return artifact;
#endif
}

bool monotonic_us(void*, std::uint64_t* out) noexcept {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return false;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) return false;
    const std::uint64_t seconds = static_cast<std::uint64_t>(ts.tv_sec);
    const std::uint64_t micros = static_cast<std::uint64_t>(ts.tv_nsec) / 1000u;
    if (seconds > (UINT64_MAX - micros) / 1000000ULL) return false;
    *out = seconds * 1000000ULL + micros;
    return true;
}

} // namespace

void PatchImage(unsigned char* image, std::size_t size, const Segment* segments,
                std::size_t count) {
    ModuleState expected = ModuleState::Idle;
    if (!g_state.compare_exchange_strong(expected, ModuleState::Installing,
                                         std::memory_order_acq_rel)) {
        return; // already installed/disabled/failed/installing: never clobber it
    }
    if (!env_enabled()) {
        g_state.store(ModuleState::Disabled, std::memory_order_release);
        set_status("disabled: BB_NATIVE_MOUSE must be exactly 1");
        return;
    }
    const char* supported = cached_cpu_support();
    if (supported != nullptr) {
        g_state.store(ModuleState::Unsupported, std::memory_order_release);
        set_status(supported);
        return;
    }
    TestSeam::Artifact artifact =
        install_impl(image, size, segments, count, kProductionProfile, &g_mailbox,
                     TestSeam::Fault::None, TestSeam::Capability::Supported);
    if (!artifact.ok) {
        g_state.store(ModuleState::Failed, std::memory_order_release);
        set_status(artifact.status);
        return;
    }
    g_state.store(ModuleState::Installed, std::memory_order_release);
    set_status("installed");
}

std::uint64_t Epoch() {
    return g_mailbox.Epoch();
}

bool StickOnly(std::uint64_t epoch) {
    if (epoch > UINT32_MAX) return false;
    return g_mailbox.ValidateStick(static_cast<std::uint32_t>(epoch));
}

void SetCapture(bool active) {
    g_mailbox.SetCapture(active);
}

void Cancel() {
    g_mailbox.Cancel();
}

Route Submit(std::uint64_t epoch, float pitch, float yaw) {
#if defined(__linux__) && defined(__x86_64__) && defined(__LP64__)
    if (epoch > UINT32_MAX) return Route::Dropped;
    switch (g_mailbox.SubmitWithClock(static_cast<std::uint32_t>(epoch), pitch, yaw,
                                      monotonic_us, nullptr)) {
    case BbNativeMouse::SubmitResult::Native: return Route::NativeQueued;
    case BbNativeMouse::SubmitResult::Fallback: return Route::Stick;
    case BbNativeMouse::SubmitResult::Discard: return Route::Dropped;
    }
    return Route::Dropped;
#else
    (void)epoch;
    (void)pitch;
    (void)yaw;
    return Route::Dropped;
#endif
}

bool Available() {
    if (g_state.load(std::memory_order_acquire) != ModuleState::Installed) return false;
    return Epoch() != 0;
}

const char* Status() {
    const char* status = g_status.load(std::memory_order_acquire);
    return status != nullptr ? status : "disabled";
}

namespace TestSeam {

Artifact InstallForTest(unsigned char* image, std::size_t size, const Segment* segments,
                        std::size_t count, const Profile& profile,
                        BbNativeMouse::Mailbox* mailbox, Fault fault, Capability capability) {
    const InstallProfile converted = {
        profile.camera_start, profile.camera_end, profile.constants_start, profile.constants_end,
        profile.site_start, profile.camera_fnv1a64, profile.constants_fnv1a64, profile.site_bytes,
        profile.site_size,
    };
    return install_impl(image, size, segments, count, converted, mailbox, fault, capability);
}

void UninstallForTest(Artifact& artifact) {
#if defined(__linux__) && defined(__x86_64__) && defined(__LP64__)
    if (artifact.stub != nullptr && artifact.page_size != 0) {
        mprotect(artifact.stub, artifact.page_size, PROT_READ | PROT_WRITE);
        munmap(artifact.stub, artifact.page_size);
    }
#endif
    artifact.stub = nullptr;
    artifact.stub_size = 0;
    artifact.page_size = 0;
    artifact.ok = false;
    artifact.status = "uninstalled";
}

bool PatchImageForTest(unsigned char* image, std::size_t size, const Segment* segments,
                       std::size_t count) {
    PatchImage(image, size, segments, count);
    return Available();
}

bool Rel32ForTest(std::uintptr_t from_after_instruction, std::uintptr_t target,
                  std::int32_t& displacement) {
    return rel32_to(reinterpret_cast<const void*>(from_after_instruction),
                    reinterpret_cast<const void*>(target), displacement);
}

bool ValidateProductionCameraForTest(const unsigned char* camera, std::size_t size) {
    constexpr std::size_t camera_size =
        static_cast<std::size_t>(kCameraEnd - kCameraStart);
    constexpr std::size_t site_offset = static_cast<std::size_t>(kSiteStart - kCameraStart);
    if (camera == nullptr || size < camera_size) return false;
    return fnv1a64(camera, camera_size) == kCameraFnv1a64 &&
           std::memcmp(camera + site_offset, kSiteBytes, sizeof(kSiteBytes)) == 0;
}

void ForceModuleInstalledForTest() {
    g_state.store(ModuleState::Installed, std::memory_order_release);
    set_status("installed");
}

void ResetModuleStateForTest() {
    g_state.store(ModuleState::Idle, std::memory_order_release);
    set_status("disabled");
}

} // namespace TestSeam

} // namespace BbMouseLook
