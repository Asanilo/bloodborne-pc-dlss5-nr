// Optional private-image replay: actual BbFrameRate::PatchImage -> proof -> mouse installer.
#include "gpu/shim/bbport_frame_rate.h"
#include "gpu/shim/bbport_mouse_look.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace BbMouseLook;
#ifdef BB_ASAN_REPLAY
constexpr std::uintptr_t kImageBase = 0x400000000ull;
#else
constexpr std::uintptr_t kImageBase = 0x800000000ull;
#endif
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const bool occupied = std::strcmp(argv[1], "occupied") == 0;
    const bool badproof = std::strcmp(argv[1], "badproof") == 0;
    const char* path = argv[2];
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "FAIL: cannot open private image: %s\n", path);
        return 3;
    }
    assert(!std::fseek(f, 0, SEEK_END));
    const long n = std::ftell(f);
    assert(n > 0);
    std::rewind(f);
    const std::size_t bytes = (std::size_t(n) + 4095) & ~std::size_t(4095);
#ifdef BB_ASAN_REPLAY
    auto* image = static_cast<unsigned char*>(
        mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
#else
    auto* image = static_cast<unsigned char*>(
        mmap(reinterpret_cast<void*>(kImageBase), bytes, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0));
#endif
    assert(image != MAP_FAILED);
    assert(std::fread(image, 1, std::size_t(n), f) == std::size_t(n));
    std::fclose(f);
    if (occupied) {
        void* block = mmap(reinterpret_cast<void*>(
                               reinterpret_cast<std::uintptr_t>(image) - (8ull << 20)), 4096,
                           PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        assert(block != MAP_FAILED);
    }
    ::setenv("BB_NATIVE_MOUSE", "1", 1);
    ::setenv("BB_FPS", "uncap", 1);
    ::setenv("BB_FPS_LIMIT", "90", 1);
    ::unsetenv("BB_FRAME_HOOKS");
    const Segment segment{0, std::size_t(n), 5};
    assert(PreAttest(image, std::size_t(n), &segment, 1,
                     BbFrameRate::CameraRedirectSequence()));
    BbFrameRate::PatchImage(image, std::uint64_t(n));
    BbFrameRate::CameraRedirectProof proof{};
    assert(BbFrameRate::CameraRedirectProofs(&proof));
    assert(proof.complete && proof.entry[0].applied && proof.entry[1].applied);
    if (badproof) {
        proof.entry[0].target += 8; // same float values? target identity is wrong
        PatchImageWithProof(image, std::size_t(n), &segment, 1, &proof);
        assert(!Available());
        assert(image[0x143cde0] != 0xe9);
        SetCapture(true);
        assert(Submit(Epoch(), 0.1f, 0.0f) == Route::Stick);
        std::puts("PASS: bad proof leaves site untouched and stick fallback available");
        return 0;
    }
    PatchImageWithProof(image, std::size_t(n), &segment, 1, &proof);
    assert(Available());
    assert(std::strcmp(Status(), "installed") == 0);
    for (int i = 0; i < 2; ++i) {
        const auto& e = proof.entry[i];
        std::printf("%s site=%#llx next=%#llx target=%#llx field=%u orig=%08x written=%08x\n",
                    occupied ? "occupied" : "free", (unsigned long long)e.site,
                    (unsigned long long)e.next, (unsigned long long)e.target, e.field_offset,
                    e.original_disp, e.written_disp);
        assert(image[0x143cde0] == 0xe9); // mouse site is patched only after final validation
    }
    // A later PatchImage entry clears the previous proof, including the disabled early return.
    ::setenv("BB_FPS", "30", 1);
    BbFrameRate::PatchImage(image, std::uint64_t(n));
    BbFrameRate::CameraRedirectProof cleared{};
    assert(!BbFrameRate::CameraRedirectProofs(&cleared));
    std::puts(occupied ? "PASS: occupied near replay" : "PASS: free near replay");
    return 0;
}
