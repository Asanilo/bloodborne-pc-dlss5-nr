// SPDX-License-Identifier: GPL-2.0-or-later
//
// CPU-only tests for the production native-mouse installer/generator/stub.  A synthetic guest
// image is patched and executed; no game image, GPU, Vulkan, Wine or save data is used.

#include "../gpu/shim/bbport_mouse_look.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace {

using BbMouseLook::Segment;
using BbMouseLook::TestSeam::Artifact;
using BbMouseLook::TestSeam::Fault;
using BbMouseLook::TestSeam::Profile;

void fail(const char* file, int line, const char* expression) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    std::exit(1);
}

#define CHECK(expression) \
    do { \
        if (!(expression)) fail(__FILE__, __LINE__, #expression); \
    } while (false)

struct FixtureOutput {
    std::uint64_t gpr[15];
    std::uint64_t flags;
    std::uint32_t mxcsr;
    std::uint32_t marker_before;
    std::uint32_t marker_after;
    std::uint32_t cam158;
    std::uint32_t cam154;
    unsigned char cam15c;
    unsigned char cam_pad[3];
    std::uint64_t rsp_at_done;
    unsigned char xmm0[16];
    unsigned char xmm1[16];
    unsigned char xmm2[16];
    unsigned char xmm3[16];
    unsigned char xmm4[16];
    unsigned char xmm5[16];
    unsigned char xmm10[16];
    unsigned char redzone[128];
};
static_assert(sizeof(FixtureOutput) == 400);
static_assert(offsetof(FixtureOutput, gpr) == 0);
static_assert(offsetof(FixtureOutput, flags) == 120);
static_assert(offsetof(FixtureOutput, mxcsr) == 128);
static_assert(offsetof(FixtureOutput, marker_before) == 132);
static_assert(offsetof(FixtureOutput, marker_after) == 136);
static_assert(offsetof(FixtureOutput, cam158) == 140);
static_assert(offsetof(FixtureOutput, cam154) == 144);
static_assert(offsetof(FixtureOutput, cam15c) == 148);
static_assert(offsetof(FixtureOutput, rsp_at_done) == 152);
static_assert(offsetof(FixtureOutput, xmm0) == 160);
static_assert(offsetof(FixtureOutput, xmm1) == 176);
static_assert(offsetof(FixtureOutput, xmm2) == 192);
static_assert(offsetof(FixtureOutput, xmm10) == 256);
static_assert(offsetof(FixtureOutput, redzone) == 272);

extern "C" {
FixtureOutput bb_fixture_output;
alignas(16) unsigned char bb_fixture_frame[0x500];
alignas(16) unsigned char bb_fixture_camera[0x200];
alignas(16) unsigned char bb_fixture_stack[0x1000];
void* bb_fixture_site = nullptr;
std::uint64_t bb_fixture_saved_rsp = 0;
std::uint64_t bb_fixture_test_rsp = 0;
std::uint64_t bb_fixture_flags_in = 0;
std::uint32_t bb_fixture_mxcsr_in = 0x1fbf;
void bb_native_mouse_fixture_run();
}

constexpr std::size_t kImageSize = 0x10000;
constexpr std::uint64_t kCameraStart = 0x1000;
constexpr std::uint64_t kCameraEnd = 0x9000;
constexpr std::uint64_t kConstantsStart = 0xa000;
constexpr std::uint64_t kConstantsEnd = 0xa040;
constexpr std::uint64_t kSiteStart = 0x2000;
constexpr std::uint64_t kSegmentEnd = 0xc000;
constexpr std::uint32_t kInitialXmm1[4] = {0x3f800000u, 0x40000000u, 0x7fc00001u, 0x80000000u};
constexpr std::uint32_t kOne[4] = {0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u};
constexpr std::uint32_t kXmm0[4] = {0xeeeeeeeeu, 0xeeeeeeeeu, 0xeeeeeeeeu, 0xeeeeeeeeu};
constexpr std::uint32_t kXmm3[4] = {0xbbbbbbbbu, 0xbbbbbbbbu, 0xbbbbbbbbu, 0xbbbbbbbbu};
constexpr std::uint32_t kXmm4[4] = {0xccccccccu, 0xccccccccu, 0xccccccccu, 0xccccccccu};
constexpr std::uint32_t kXmm5[4] = {0xdddddddd, 0xdddddddd, 0xdddddddd, 0xdddddddd};
std::array<unsigned char, 16> g_site_signature = [] {
    std::array<unsigned char, 16> value{};
    value.fill(0x90);
    return value;
}();

std::uint64_t now_us() {
    struct timespec ts {};
    CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec) / 1000u;
}

std::uint64_t fnv1a64(const unsigned char* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

unsigned char* map_image() {
    void* mapping = mmap(nullptr, kImageSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    return static_cast<unsigned char*>(mapping);
}

void unmap_image(unsigned char* image) {
    if (image != nullptr) CHECK(munmap(image, kImageSize) == 0);
}

void fill_image(unsigned char* image, Profile& profile) {
    std::memset(image, 0, kImageSize);
    for (std::size_t i = 0; i < kCameraEnd - kCameraStart; ++i)
        image[kCameraStart + i] = static_cast<unsigned char>((i * 37u + 11u) & 0xffu);
    for (std::size_t i = 0; i < kConstantsEnd - kConstantsStart; ++i)
        image[kConstantsStart + i] = static_cast<unsigned char>((i * 53u + 7u) & 0xffu);
    std::memcpy(image + kSiteStart, g_site_signature.data(), g_site_signature.size());
    image[kSiteStart + 16] = 0xc3; // continuation: ret back to the fixture harness

    profile = Profile{};
    profile.camera_start = kCameraStart;
    profile.camera_end = kCameraEnd;
    profile.constants_start = kConstantsStart;
    profile.constants_end = kConstantsEnd;
    profile.site_start = kSiteStart;
    profile.camera_fnv1a64 = fnv1a64(image + kCameraStart,
                                     static_cast<std::size_t>(kCameraEnd - kCameraStart));
    profile.constants_fnv1a64 =
        fnv1a64(image + kConstantsStart, static_cast<std::size_t>(kConstantsEnd - kConstantsStart));
    profile.site_bytes = g_site_signature.data();
    profile.site_size = 16;
}

Segment synthetic_segment() {
    return Segment{0, kSegmentEnd, 5}; // PF_R | PF_X
}

bool mapping_is_rx(const void* address) {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (maps == nullptr) return false;
    char line[512];
    bool result = false;
    const auto target = reinterpret_cast<std::uintptr_t>(address);
    while (std::fgets(line, sizeof line, maps) != nullptr) {
        unsigned long long start = 0, end = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &start, &end, perms) != 3) continue;
        if (target < start || target >= end) continue;
        result = perms[0] == 'r' && perms[1] == '-' && perms[2] == 'x';
        break;
    }
    std::fclose(maps);
    return result;
}

void check_bytes(const unsigned char* actual, const std::uint32_t expected[4]) {
    unsigned char want[16];
    std::memcpy(want, expected, 16);
    if (std::memcmp(actual, want, 16) != 0) {
        std::fprintf(stderr, "xmm actual ");
        for (int i = 0; i < 16; ++i) std::fprintf(stderr, "%02x", actual[i]);
        std::fprintf(stderr, " want ");
        for (int i = 0; i < 16; ++i) std::fprintf(stderr, "%02x", want[i]);
        std::fprintf(stderr, "\n");
        fail(__FILE__, __LINE__, "xmm bytes");
    }
}

void check_preserved(const FixtureOutput& out, const std::uint32_t expected_xmm1[4],
                     bool flags_written) {
    const std::uint64_t sentinels[15] = {
        0xaaaaaaaaaaaaaaaaULL, 0x1111111111111111ULL, 0x2222222222222222ULL,
        0x3333333333333333ULL, 0x4444444444444444ULL, 0x5555555555555555ULL,
        0, 0x8888888888888888ULL, 0x9999999999999999ULL, 0x1010101010101010ULL,
        0x1111222233334444ULL, 0x5555666677778888ULL, 0,
        0xaaaabbbbccccddddULL, 0xeeeeffff00001111ULL,
    };
    for (int i = 0; i < 15; ++i) {
        if (i == 6 || i == 12) continue;
        CHECK(out.gpr[i] == sentinels[i]);
    }
    CHECK(out.gpr[6] == reinterpret_cast<std::uintptr_t>(bb_fixture_frame) + 0x400);
    CHECK(out.gpr[12] == reinterpret_cast<std::uintptr_t>(bb_fixture_camera));
    CHECK(out.flags == bb_fixture_flags_in);
    CHECK(out.mxcsr == bb_fixture_mxcsr_in);
    CHECK(out.rsp_at_done == reinterpret_cast<std::uintptr_t>(bb_fixture_stack) + 0x1000);
    for (unsigned char byte : out.redzone) CHECK(byte == 0xa5);

    check_bytes(out.xmm0, kXmm0);
    check_bytes(out.xmm1, expected_xmm1);
    check_bytes(out.xmm2, kOne);
    check_bytes(out.xmm3, kXmm3);
    check_bytes(out.xmm4, kXmm4);
    check_bytes(out.xmm5, kXmm5);
    check_bytes(out.xmm10, kOne);

    CHECK(out.marker_before == 0xdeadbeef);
    CHECK(out.cam158 == 0x22222222);
    if (flags_written) {
        CHECK(out.marker_after == 1);
        CHECK(out.cam154 == 0);
        CHECK(out.cam15c == 1);
    } else {
        CHECK(out.marker_after == 0xdeadbeef);
        CHECK(out.cam154 == 0x11111111);
        CHECK(out.cam15c == 0x33);
    }
}

std::uint32_t float_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

void run_fixture() {
    std::memset(&bb_fixture_output, 0, sizeof bb_fixture_output);
    bb_native_mouse_fixture_run();
}

std::uint32_t prime(BbNativeMouse::Mailbox& mailbox, std::uint64_t now) {
    mailbox.Reset();
    CHECK(mailbox.TryConsume(now).status == BbNativeMouse::ConsumeStatus::Empty);
    return static_cast<std::uint32_t>(mailbox.Epoch());
}

std::size_t syscall_immediate_offset(const unsigned char* stub, std::size_t size) {
    for (std::size_t i = 0; i + 5 <= size; ++i) {
        if (stub[i] == 0xb8 && stub[i + 1] == 0xe4 && stub[i + 2] == 0 &&
            stub[i + 3] == 0 && stub[i + 4] == 0)
            return i + 1;
    }
    fail(__FILE__, __LINE__, "mov eax, SYS_clock_gettime not found");
    return 0;
}

void test_installer_and_stub() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    Segment segment = synthetic_segment();
    CHECK(mprotect(image, kImageSize, PROT_READ | PROT_EXEC) == 0);
    // Installer needs the image writable for the final site publication.
    CHECK(mprotect(image, kImageSize, PROT_READ | PROT_WRITE) == 0);

    BbNativeMouse::Mailbox mailbox;
    Artifact artifact = BbMouseLook::TestSeam::InstallForTest(
        image, kImageSize, &segment, 1, profile, &mailbox);
    CHECK(artifact.ok);
    CHECK(artifact.stub != nullptr);
    CHECK(artifact.site == image + kSiteStart);
    CHECK(mailbox.Installed());
    CHECK(mapping_is_rx(artifact.stub));

    const unsigned char* site = image + kSiteStart;
    CHECK(site[0] == 0xe9);
    std::int32_t displacement = 0;
    std::memcpy(&displacement, site + 1, 4);
    CHECK(reinterpret_cast<std::uintptr_t>(site + 5) +
              static_cast<std::intptr_t>(displacement) ==
          reinterpret_cast<std::uintptr_t>(artifact.stub));
    for (int i = 5; i < 16; ++i) CHECK(site[i] == 0x90);
    CHECK(site[16] == 0xc3);

    CHECK(mprotect(image, kImageSize, PROT_READ | PROT_EXEC) == 0);
    bb_fixture_site = image + kSiteStart;
    CHECK(bb_fixture_site == image + kSiteStart);
    bb_fixture_mxcsr_in = 0x1fbf;

    // Non-zero native batch: add both axes and write exactly the three camera flags.
    std::uint64_t now = now_us();
    std::uint32_t epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    run_fixture();
    const std::uint32_t expected_both[4] = {
        float_bits(1.0f + 0.5f), float_bits(2.0f - 0.25f), 0x7fc00001u, 0x80000000u};
    check_preserved(bb_fixture_output, expected_both, true);
    {
        const auto state = [&] {
            BbNativeMouse::TestSeam::TryAcquire(mailbox);
            BbNativeMouse::State copy = mailbox.state;
            BbNativeMouse::TestSeam::Release(mailbox);
            return copy;
        }();
        CHECK(state.has_pending == 0);
        CHECK(state.last_consumed_seq == 1);
        CHECK(state.heartbeat_seq == 2);
        CHECK(state.ready == 1);
    }

    // A second consume must not replay; no pending is a zero heartbeat.
    run_fixture();
    check_preserved(bb_fixture_output, kInitialXmm1, false);

    // Per-axis zero delta must preserve the other lane bit-for-bit.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, 0.0f, now) == BbNativeMouse::SubmitResult::Native);
    run_fixture();
    const std::uint32_t expected_pitch[4] = {
        float_bits(1.0f + 0.5f), kInitialXmm1[1], kInitialXmm1[2], kInitialXmm1[3]};
    check_preserved(bb_fixture_output, expected_pitch, true);

    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.0f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    run_fixture();
    const std::uint32_t expected_yaw[4] = {
        kInitialXmm1[0], float_bits(2.0f - 0.25f), kInitialXmm1[2], kInitialXmm1[3]};
    check_preserved(bb_fixture_output, expected_yaw, true);

    // Net-zero batch retires but writes no camera flags.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 1.0f, -2.0f, now) == BbNativeMouse::SubmitResult::Native);
    CHECK(mailbox.Submit(epoch, -1.0f, 2.0f, now) == BbNativeMouse::SubmitResult::Native);
    run_fixture();
    check_preserved(bb_fixture_output, kInitialXmm1, false);

    // Gate contention: one try, no state change, original path still replayed.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(mailbox));
    run_fixture();
    CHECK(bb_fixture_output.marker_after == 0xdeadbeef);
    CHECK(bb_fixture_output.cam154 == 0x11111111);
    BbNativeMouse::TestSeam::Release(mailbox);
    check_preserved(bb_fixture_output, kInitialXmm1, false);
    run_fixture(); // pending survived and is consumed now
    check_preserved(bb_fixture_output, expected_both, true);

    // Unmasked MXCSR blocks consumption without touching mailbox or XMM1.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    bb_fixture_mxcsr_in = 0x1f00;
    run_fixture();
    check_preserved(bb_fixture_output, kInitialXmm1, false);
    bb_fixture_mxcsr_in = 0x1fbf;
    run_fixture();
    check_preserved(bb_fixture_output, expected_both, true);

    // Stale pending is cancelled by the stub itself, with no motion and no replay.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(mailbox));
    mailbox.state.pending_since_us = now - 150000;
    const std::uint32_t cancelled_epoch = mailbox.state.epoch;
    const std::uint64_t cancelled_sequence = mailbox.state.pending_last_seq;
    BbNativeMouse::TestSeam::Release(mailbox);
    run_fixture();
    check_preserved(bb_fixture_output, kInitialXmm1, false);
    {
        CHECK(mailbox.Epoch() == static_cast<std::uint64_t>(cancelled_epoch + 1));
        BbNativeMouse::TestSeam::TryAcquire(mailbox);
        const BbNativeMouse::State state = mailbox.state;
        BbNativeMouse::TestSeam::Release(mailbox);
        CHECK(state.has_pending == 0);
        CHECK(state.cancelled_seq == cancelled_sequence);
        CHECK(state.ready == 0);
        CHECK(state.fallback_watermark == state.heartbeat_seq);
    }
    run_fixture(); // zero heartbeat recovers, advances epoch again
    CHECK(bb_fixture_output.marker_after == 0xdeadbeef);

    // A pending timestamp in the future is also cancelled, never consumed.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(mailbox));
    mailbox.state.pending_since_us = now + 1000000;
    const std::uint64_t future_sequence = mailbox.state.pending_last_seq;
    const std::uint32_t future_epoch = mailbox.state.epoch;
    BbNativeMouse::TestSeam::Release(mailbox);
    run_fixture();
    check_preserved(bb_fixture_output, kInitialXmm1, false);
    CHECK(mailbox.Epoch() == static_cast<std::uint64_t>(future_epoch + 1));
    {
        BbNativeMouse::TestSeam::TryAcquire(mailbox);
        const BbNativeMouse::State state = mailbox.state;
        BbNativeMouse::TestSeam::Release(mailbox);
        CHECK(state.has_pending == 0);
        CHECK(state.cancelled_seq == future_sequence);
        CHECK(state.ready == 0);
    }
    run_fixture(); // recover

    // Clock syscall failure: no state change, no motion, no flags.
    now = now_us();
    epoch = prime(mailbox, now);
    CHECK(mailbox.Submit(epoch, 0.5f, -0.25f, now) == BbNativeMouse::SubmitResult::Native);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(mailbox));
    const BbNativeMouse::State before_clock_fault = mailbox.state;
    BbNativeMouse::TestSeam::Release(mailbox);
    unsigned char* stub = static_cast<unsigned char*>(artifact.stub);
    CHECK(mprotect(artifact.stub, artifact.page_size, PROT_READ | PROT_WRITE) == 0);
    const std::size_t syscall_offset = syscall_immediate_offset(stub, artifact.stub_size);
    unsigned char saved_syscall[4];
    std::memcpy(saved_syscall, stub + syscall_offset, 4);
    const std::uint32_t invalid_syscall = 9999;
    std::memcpy(stub + syscall_offset, &invalid_syscall, 4);
    CHECK(mprotect(artifact.stub, artifact.page_size, PROT_READ | PROT_EXEC) == 0);
    run_fixture();
    check_preserved(bb_fixture_output, kInitialXmm1, false);
    CHECK(mprotect(artifact.stub, artifact.page_size, PROT_READ | PROT_WRITE) == 0);
    std::memcpy(stub + syscall_offset, saved_syscall, 4);
    CHECK(mprotect(artifact.stub, artifact.page_size, PROT_READ | PROT_EXEC) == 0);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(mailbox));
    const BbNativeMouse::State after_clock_fault = mailbox.state;
    BbNativeMouse::TestSeam::Release(mailbox);
    CHECK(std::memcmp(&before_clock_fault, &after_clock_fault, sizeof(BbNativeMouse::State)) == 0);
    run_fixture();
    check_preserved(bb_fixture_output, expected_both, true);

    BbMouseLook::TestSeam::UninstallForTest(artifact);
    unmap_image(image);
}

void test_installer_failures() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    Segment segment = synthetic_segment();
    auto expect_failure = [&](auto&& call, std::size_t site_offset, std::size_t site_size,
                              const char* what) {
        unsigned char before[16] = {};
        if (image != nullptr && site_offset + 16 <= site_size)
            std::memcpy(before, image + site_offset, 16);
        Artifact artifact = call();
        if (artifact.ok) {
            std::fprintf(stderr, "unexpected success: %s\n", what);
            std::exit(1);
        }
        if (artifact.stub != nullptr) {
            std::fprintf(stderr, "failed install leaked stub: %s\n", what);
            std::exit(1);
        }
        if (image != nullptr && site_offset + 16 <= site_size)
            CHECK(std::memcmp(before, image + site_offset, 16) == 0);
    };

    BbNativeMouse::Mailbox mailbox;
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(nullptr, kImageSize, &segment, 1, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "null image");
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, nullptr, 0, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "null segments");
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kConstantsEnd - 1, &segment, 1,
                                                     profile, &mailbox);
    }, kSiteStart, kImageSize, "short image");
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, profile,
                                                     nullptr);
    }, kSiteStart, kImageSize, "null mailbox");

    Segment hole[2] = {{0, 0x1800, 5}, {0x8000, 0x4000, 5}};
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, hole, 2, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "segment hole");
    Segment read_only = {0, kSegmentEnd, 4};
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &read_only, 1, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "camera not executable");
    Segment exec_only[2] = {{0, kSegmentEnd, 1}, {kConstantsStart, 0x40, 1}};
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, exec_only, 2, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "constants not readable");

    Profile bad = profile;
    bad.camera_fnv1a64 ^= 1;
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, bad,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "camera FNV");
    bad = profile;
    bad.constants_fnv1a64 ^= 1;
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, bad,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "constants FNV");
    bad = profile;
    bad.site_start = kCameraStart - 1;
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, bad,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "invalid site profile");

    for (std::size_t i = 0; i < 16; ++i) {
        image[kSiteStart + i] ^= 0xff;
        expect_failure([&] {
            return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1,
                                                         profile, &mailbox);
        }, kSiteStart, kImageSize, "site byte mutation");
        image[kSiteStart + i] ^= 0xff;
    }
    image[kCameraStart + 17] ^= 0xff;
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "camera context mutation");
    image[kCameraStart + 17] ^= 0xff;
    image[kConstantsStart + 3] ^= 0xff;
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, profile,
                                                     &mailbox);
    }, kSiteStart, kImageSize, "constants context mutation");
    image[kConstantsStart + 3] ^= 0xff;

    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, profile,
                                                     &mailbox, Fault::MmapFailure);
    }, kSiteStart, kImageSize, "mmap failure");
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, profile,
                                                     &mailbox, Fault::MprotectFailure);
    }, kSiteStart, kImageSize, "mprotect failure");
    expect_failure([&] {
        return BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1, profile,
                                                     &mailbox, Fault::Rel32Failure);
    }, kSiteStart, kImageSize, "rel32 failure");

    Artifact first = BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1,
                                                           profile, &mailbox);
    CHECK(first.ok);
    const unsigned char* site = image + kSiteStart;
    unsigned char patched[16];
    std::memcpy(patched, site, 16);
    Artifact second = BbMouseLook::TestSeam::InstallForTest(image, kImageSize, &segment, 1,
                                                            profile, &mailbox);
    CHECK(!second.ok);
    CHECK(std::memcmp(patched, site, 16) == 0);
    BbMouseLook::TestSeam::UninstallForTest(first);
    unmap_image(image);
}


void test_rel32_boundaries() {
    constexpr std::uintptr_t base = 0x100000000ULL;
    std::int32_t value = 0;
    CHECK(BbMouseLook::TestSeam::Rel32ForTest(base, base, value) && value == 0);
    CHECK(BbMouseLook::TestSeam::Rel32ForTest(base, base + 0x7fffffffULL, value) &&
          value == std::numeric_limits<std::int32_t>::max());
    CHECK(!BbMouseLook::TestSeam::Rel32ForTest(base, base + 0x80000000ULL, value));
    CHECK(BbMouseLook::TestSeam::Rel32ForTest(base, base - 0x80000000ULL, value) &&
          value == std::numeric_limits<std::int32_t>::min());
    CHECK(!BbMouseLook::TestSeam::Rel32ForTest(base, base - 0x80000001ULL, value));
    CHECK(BbMouseLook::TestSeam::Rel32ForTest(0xfffffffffffff000ULL, 0x7fffffffULL, value) ==
          false);
}

void test_segment_page_ambiguity() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    const Segment base_segments[2] = {
        synthetic_segment(),
        Segment{kConstantsStart, 0x1000, 4},
    };
    auto expect_failure = [&](const Segment* segments, std::size_t count, const char* what) {
        unsigned char before[16];
        std::memcpy(before, image + kSiteStart, 16);
        BbNativeMouse::Mailbox mailbox;
        Artifact artifact = BbMouseLook::TestSeam::InstallForTest(
            image, kImageSize, segments, count, profile, &mailbox);
        if (artifact.ok) {
            std::fprintf(stderr, "unexpected success: %s\n", what);
            std::exit(1);
        }
        CHECK(artifact.stub == nullptr);
        CHECK(std::memcmp(before, image + kSiteStart, 16) == 0);
    };

    Segment camera_overlap[3] = {base_segments[0], base_segments[1],
                                 Segment{kSiteStart, 0x1000, 4}};
    expect_failure(camera_overlap, 3, "camera page read-only overlap");
    Segment last_page[3] = {base_segments[0], base_segments[1],
                            Segment{0x8000, 0x1000, 4}};
    expect_failure(last_page, 3, "camera last page read-only overlap");
    Segment constants_overlap[3] = {base_segments[0], base_segments[1],
                                    Segment{kConstantsStart, 0x100, 1}};
    expect_failure(constants_overlap, 3, "constants page exec-only overlap");
    Segment page_hole[2] = {Segment{0, 0x8fff, 5}, base_segments[1]};
    expect_failure(page_hole, 2, "camera last page hole");
    unmap_image(image);
}

void test_install_final_gate_failures() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    Segment segment = synthetic_segment();
    unsigned char before[16];
    std::memcpy(before, image + kSiteStart, 16);

    BbNativeMouse::Mailbox fatal;
    fatal.SetInstalled(true);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(fatal));
    fatal.state.fatal = 1;
    BbNativeMouse::TestSeam::Release(fatal);
    Artifact fatal_artifact = BbMouseLook::TestSeam::InstallForTest(
        image, kImageSize, &segment, 1, profile, &fatal);
    CHECK(!fatal_artifact.ok);
    CHECK(fatal_artifact.stub == nullptr);
    CHECK(std::memcmp(before, image + kSiteStart, 16) == 0);

    BbNativeMouse::Mailbox exhausted;
    exhausted.SetInstalled(true);
    CHECK(BbNativeMouse::TestSeam::TryAcquire(exhausted));
    exhausted.state.epoch = UINT32_MAX;
    __atomic_store_n(&exhausted.state.published_epoch, UINT32_MAX, __ATOMIC_RELEASE);
    BbNativeMouse::TestSeam::Release(exhausted);
    Artifact exhausted_artifact = BbMouseLook::TestSeam::InstallForTest(
        image, kImageSize, &segment, 1, profile, &exhausted);
    CHECK(!exhausted_artifact.ok);
    CHECK(exhausted_artifact.stub == nullptr);
    CHECK(std::memcmp(before, image + kSiteStart, 16) == 0);
    CHECK(exhausted.Epoch() == 0);
    {
        CHECK(BbNativeMouse::TestSeam::TryAcquire(exhausted));
        CHECK(exhausted.state.fatal == 1);
        BbNativeMouse::TestSeam::Release(exhausted);
    }
    unmap_image(image);
}

void test_capability_rejections() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    Segment segment = synthetic_segment();
    const BbMouseLook::TestSeam::Capability rejected[] = {
        BbMouseLook::TestSeam::Capability::NoSse41,
        BbMouseLook::TestSeam::Capability::NoAvx,
        BbMouseLook::TestSeam::Capability::NoOsxsave,
        BbMouseLook::TestSeam::Capability::NoXcr0,
        BbMouseLook::TestSeam::Capability::NoClock,
        BbMouseLook::TestSeam::Capability::UnsupportedPlatform,
    };
    unsigned char before[16];
    std::memcpy(before, image + kSiteStart, 16);
    for (auto capability : rejected) {
        BbNativeMouse::Mailbox mailbox;
        Artifact artifact = BbMouseLook::TestSeam::InstallForTest(
            image, kImageSize, &segment, 1, profile, &mailbox, Fault::None, capability);
        CHECK(!artifact.ok);
        CHECK(artifact.stub == nullptr);
        CHECK(std::memcmp(before, image + kSiteStart, 16) == 0);
        CHECK(!mailbox.Installed());
    }
    unmap_image(image);
}

void test_capture_and_module_repeat() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    Segment segment = synthetic_segment();
    BbNativeMouse::Mailbox mailbox;
    mailbox.SetCapture(false);
    Artifact artifact = BbMouseLook::TestSeam::InstallForTest(
        image, kImageSize, &segment, 1, profile, &mailbox);
    CHECK(artifact.ok);
    CHECK(mailbox.Installed());
    CHECK(!mailbox.Captured());
    const std::uint64_t epoch = mailbox.Epoch();
    CHECK(mailbox.Submit(static_cast<std::uint32_t>(epoch), 0.5f, 0.5f, now_us()) ==
          BbNativeMouse::SubmitResult::Discard);
    mailbox.SetCapture(true);
    CHECK(mailbox.Captured());
    BbMouseLook::TestSeam::UninstallForTest(artifact);
    unmap_image(image);

    // Repeated production PatchImage must not replace an owned installation or revoke
    // Available.  There is no real production image in this CPU test, so force the state bit.
    BbMouseLook::TestSeam::ForceModuleInstalledForTest();
    CHECK(BbMouseLook::Available());
    const std::uint64_t module_epoch = BbMouseLook::Epoch();
    const char* module_status = BbMouseLook::Status();
    setenv("BB_NATIVE_MOUSE", "1", 1);
    unsigned char* different = map_image();
    std::memset(different, 0xcc, kImageSize);
    BbMouseLook::PatchImage(different, kImageSize, nullptr, 0);
    unmap_image(different);
    CHECK(BbMouseLook::Available());
    CHECK(BbMouseLook::Epoch() == module_epoch);
    CHECK(BbMouseLook::Status() == module_status);
    setenv("BB_NATIVE_MOUSE", "0", 1);
    BbMouseLook::PatchImage(nullptr, 0, nullptr, 0);
    CHECK(BbMouseLook::Available());
    CHECK(BbMouseLook::Status() == module_status);
    unsetenv("BB_NATIVE_MOUSE");
    BbMouseLook::TestSeam::ResetModuleStateForTest();
}


volatile sig_atomic_t g_signal_count = 0;

void sigusr1_handler(int) {
    g_signal_count = g_signal_count + 1;
}

void test_signal_return_during_stub() {
    unsigned char* image = map_image();
    Profile profile{};
    fill_image(image, profile);
    Segment segment = synthetic_segment();
    BbNativeMouse::Mailbox mailbox;
    Artifact artifact = BbMouseLook::TestSeam::InstallForTest(
        image, kImageSize, &segment, 1, profile, &mailbox);
    CHECK(artifact.ok);
    CHECK(mprotect(image, kImageSize, PROT_READ | PROT_EXEC) == 0);
    bb_fixture_site = image + kSiteStart;
    bb_fixture_mxcsr_in = 0x1fbf;

    const std::uint64_t now = now_us();
    const std::uint32_t epoch = prime(mailbox, now);
    CHECK(epoch != 0);

    alignas(16) static unsigned char signal_stack[0x8000];
    stack_t alternate{};
    alternate.ss_sp = signal_stack;
    alternate.ss_size = sizeof signal_stack;
    alternate.ss_flags = 0;
    stack_t old_alternate{};
    CHECK(sigaltstack(&alternate, &old_alternate) == 0);

    struct sigaction action {};
    struct sigaction old_action {};
    action.sa_handler = sigusr1_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_ONSTACK | SA_RESTART;
    CHECK(sigaction(SIGUSR1, &action, &old_action) == 0);

    std::atomic<bool> stop{false};
    const pthread_t main_thread = pthread_self();
    std::thread signal_thread([&] {
        while (!stop.load(std::memory_order_acquire)) {
            pthread_kill(main_thread, SIGUSR1);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    constexpr int kRuns = 2000;
    for (int i = 0; i < kRuns; ++i) {
        const std::uint64_t at = now_us();
        const std::uint32_t current = static_cast<std::uint32_t>(mailbox.Epoch());
        const auto routed = mailbox.Submit(current, 0.001f, -0.001f, at);
        if (routed == BbNativeMouse::SubmitResult::Native ||
            routed == BbNativeMouse::SubmitResult::Fallback)
            run_fixture();
    }
    stop.store(true, std::memory_order_release);
    signal_thread.join();

    CHECK(g_signal_count > 0);
    CHECK(mailbox.Epoch() != 0);

    // The stub still consumes normally after the signal storm.
    mailbox.Reset();
    CHECK(mailbox.TryConsume(now_us()).status == BbNativeMouse::ConsumeStatus::Empty);
    const std::uint32_t active = static_cast<std::uint32_t>(mailbox.Epoch());
    const std::uint64_t at = now_us();
    CHECK(mailbox.Submit(active, 0.5f, -0.25f, at) == BbNativeMouse::SubmitResult::Native);
    for (int attempt = 0; attempt < 4; ++attempt) {
        BbNativeMouse::TestSeam::TryAcquire(mailbox);
        const bool pending = mailbox.state.has_pending != 0;
        BbNativeMouse::TestSeam::Release(mailbox);
        if (!pending) break;
        run_fixture();
    }
    const std::uint32_t expected[4] = {
        float_bits(1.0f + 0.5f), float_bits(2.0f - 0.25f), 0x7fc00001u, 0x80000000u};
    check_preserved(bb_fixture_output, expected, true);

    CHECK(sigaction(SIGUSR1, &old_action, nullptr) == 0);
    CHECK(sigaltstack(&old_alternate, nullptr) == 0);
    BbMouseLook::TestSeam::UninstallForTest(artifact);
    unmap_image(image);
}

void test_production_env_gate() {
    const std::uint64_t epoch = BbMouseLook::Epoch();
    setenv("BB_NATIVE_MOUSE", "0", 1);
    BbMouseLook::PatchImage(nullptr, 0, nullptr, 0);
    CHECK(std::strstr(BbMouseLook::Status(), "disabled") != nullptr);
    CHECK(!BbMouseLook::Available());
    BbMouseLook::SetCapture(true);
    CHECK(BbMouseLook::Submit(epoch, 0.25f, 0.5f) == BbMouseLook::Route::Stick);
    const std::uint64_t live_epoch = BbMouseLook::Epoch();
    CHECK(BbMouseLook::Submit(live_epoch, 30.0f, 0.0f) == BbMouseLook::Route::Stick);
    CHECK(BbMouseLook::Submit(live_epoch, std::numeric_limits<float>::max(), 0.0f) ==
          BbMouseLook::Route::Stick);
    CHECK(BbMouseLook::Submit(live_epoch, std::numeric_limits<float>::quiet_NaN(), 0.0f) ==
          BbMouseLook::Route::Dropped);
    BbMouseLook::SetCapture(false);
    CHECK(BbMouseLook::Submit(epoch, 0.25f, 0.5f) == BbMouseLook::Route::Dropped);
    CHECK(BbMouseLook::Submit(UINT64_MAX, 0.25f, 0.5f) == BbMouseLook::Route::Dropped);
    BbMouseLook::SetCapture(true);
    setenv("BB_NATIVE_MOUSE", "1", 1);
    BbMouseLook::PatchImage(nullptr, 0, nullptr, 0); // state already Disabled: no-op
    CHECK(!BbMouseLook::Available());
    unsetenv("BB_NATIVE_MOUSE");
}

} // namespace


void test_private_context_read_only() {
    FILE* file = std::fopen("out/native-mouse-cpu/private-camera-expanded.bin", "rb");
    if (file == nullptr) {
        std::puts("SKIP: private camera context fixture absent (no private bytes committed)");
        return;
    }
    std::array<unsigned char, 20080> camera{};
    const std::size_t read = std::fread(camera.data(), 1, camera.size(), file);
    std::fclose(file);
    CHECK(read == camera.size());
    CHECK(BbMouseLook::TestSeam::ValidateProductionCameraForTest(camera.data(), camera.size()));
}

int main() {
    test_rel32_boundaries();
    test_installer_failures();
    test_segment_page_ambiguity();
    test_install_final_gate_failures();
    test_capability_rejections();
    test_capture_and_module_repeat();
    test_installer_and_stub();
    test_signal_return_during_stub();
    test_private_context_read_only();
    test_production_env_gate();
    std::puts("PASS: native mouse stub/installer CPU acceptance");
    return 0;
}
