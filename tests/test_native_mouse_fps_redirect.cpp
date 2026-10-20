// CPU proof for the two BbFrameRate camera RIP-disp rewrites: synthetic image, no game bytes.
#include "gpu/shim/bbport_mouse_look.h"
#include "gpu/shim/bbport_frame_rate.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <vector>

using namespace BbMouseLook;
using BbMouseLook::TestSeam::Profile;
using BbMouseLook::TestSeam::PreAttestForTest;
using BbMouseLook::TestSeam::ValidatePostForTest;
using BbMouseLook::TestSeam::InstallForTest;
using BbFrameRate::CameraRedirectEntry;
using BbFrameRate::CameraRedirectProof;

static constexpr std::uint64_t kCameraStart = 0x143ac60;
static constexpr std::uint64_t kCameraEnd = 0x143fad0;
static constexpr std::uint64_t kSiteStart = 0x143cde0;
static constexpr std::uint64_t kFps10 = 0x143af8c;
static constexpr std::uint64_t kFps30 = 0x143e2b2;
static constexpr std::uint64_t kConst10 = 0x4925e68;
static constexpr std::uint64_t kConst30 = 0x4925e94;
static constexpr std::uint8_t kSite[16] = {
    0xc5,0xa8,0x59,0xc9,0xc5,0xf8,0x28,0x95,0x10,0xfc,0xff,0xff,0xc5,0xe8,0x59,0xc9};
static constexpr std::uint8_t kInsn10[8] = {0xc5,0xf2,0x59,0x0d,0x00,0x10,0x00,0x00};
static constexpr std::uint8_t kInsn30[8] = {0xc5,0xfa,0x10,0x15,0xfc,0x0f,0x00,0x00};

static std::uint64_t fnv(const std::uint8_t* p, std::size_t n) {
    std::uint64_t h=14695981039346656037ull;
    for (std::size_t i=0;i<n;++i) { h^=p[i]; h*=1099511628211ull; }
    return h;
}
static void put32(std::uint8_t* p, std::uint32_t v) { std::memcpy(p,&v,4); }
static std::uint32_t get32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v,p,4); return v; }

struct Fixture {
    std::vector<std::uint8_t> image;
    Profile profile{};
    Segment segment{};
    CameraRedirectProof proof{};
    std::array<std::uint8_t,8> orig10{}, orig30{};
    bool have_orig=false;
    std::uint64_t target10=0, target30=0;
    Fixture() {
        image.assign(0x1440000, 0x5a);
        std::memcpy(image.data()+kSiteStart, kSite, sizeof(kSite));
        std::memcpy(image.data()+kFps10, kInsn10, 8);
        std::memcpy(image.data()+kFps30, kInsn30, 8);
        // constants are small but bounded inside the image; camera is the production range.
        for (int i=0;i<16;++i) image[0x1000+i]=std::uint8_t(i*7+1);
        profile.camera_start=kCameraStart; profile.camera_end=kCameraEnd;
        profile.constants_start=0x1000; profile.constants_end=0x1010;
        profile.site_start=kSiteStart; profile.site_bytes=image.data()+kSiteStart;
        profile.camera_fnv1a64=fnv(image.data()+kCameraStart,kCameraEnd-kCameraStart);
        profile.constants_fnv1a64=fnv(image.data()+0x1000,16);
        segment.address=0; segment.size=image.size(); segment.flags=5;
        target10=reinterpret_cast<std::uintptr_t>(image.data())+kCameraEnd/2+36;
        target30=target10-4;
    }
    void set_targets(std::uint64_t t10,std::uint64_t t30) {
        if (!have_orig) {
            std::memcpy(orig10.data(),image.data()+kFps10,8);
            std::memcpy(orig30.data(),image.data()+kFps30,8);
            have_orig=true;
        }
        target10=t10; target30=t30;
        const std::uintptr_t next10=reinterpret_cast<std::uintptr_t>(image.data())+kFps10+8;
        const std::uintptr_t next30=reinterpret_cast<std::uintptr_t>(image.data())+kFps30+8;
        const std::int32_t d10=static_cast<std::int32_t>(static_cast<std::int64_t>(t10)-next10);
        const std::int32_t d30=static_cast<std::int32_t>(static_cast<std::int64_t>(t30)-next30);
        put32(image.data()+kFps10+4,static_cast<std::uint32_t>(d10));
        put32(image.data()+kFps30+4,static_cast<std::uint32_t>(d30));
        proof={};
        proof.image=reinterpret_cast<std::uint64_t>(image.data()); proof.size=image.size();
        proof.sequence=42; proof.complete=true;
        for (int i=0;i<2;++i) {
            proof.entry[i].site=i==0?kFps10:kFps30;
            proof.entry[i].next=reinterpret_cast<std::uint64_t>(image.data())+
                                (i==0?kFps10:kFps30)+8;
            proof.entry[i].constant=i==0?kConst10:kConst30;
            proof.entry[i].target=i==0?t10:t30;
            proof.entry[i].field_offset=i==0?36:32;
            std::memcpy(proof.entry[i].original,i==0?orig10.data():orig30.data(),8);
            std::memcpy(proof.entry[i].written,image.data()+proof.entry[i].site,8);
            std::memcpy(&proof.entry[i].original_disp,proof.entry[i].original+4,4);
            std::memcpy(&proof.entry[i].written_disp,proof.entry[i].written+4,4);
            proof.entry[i].applied=true;
        }
    }
    const char* post() { return ValidatePostForTest(image.data(),image.size(),&segment,1,profile,&proof); }
};

static unsigned char* rx_image = nullptr;
static int rx_mutation = 0;
static bool rx_armed = false, rx_fired = false;
extern "C" int __real_mprotect(void*, std::size_t, int);
extern "C" int __wrap_mprotect(void* p, std::size_t n, int prot) {
    const int r = __real_mprotect(p, n, prot);
    if (r == 0 && rx_armed && prot == (PROT_READ | PROT_EXEC)) {
        rx_armed = false; rx_fired = true;
        if (rx_mutation == 1) rx_image[kCameraStart + 100] ^= 1;
        if (rx_mutation == 2) rx_image[0x1000] ^= 1;
    }
    return r;
}

static void check_rx_reject(Fixture& f, bool redirect, int mutation) {
    assert(PreAttestForTest(f.image.data(), f.image.size(), &f.segment, 1, f.profile, 41, false));
    if (redirect) {
        f.set_targets(f.target10, f.target30);
        assert(f.post() == nullptr);
    }
    BbNativeMouse::Mailbox mailbox;
    rx_image = f.image.data(); rx_mutation = mutation; rx_armed = true; rx_fired = false;
    const auto artifact = InstallForTest(f.image.data(), f.image.size(), &f.segment, 1,
                                         f.profile, &mailbox, TestSeam::Fault::None,
                                         TestSeam::Capability::Supported, redirect);
    rx_armed = false;
    assert(rx_fired);
    assert(!artifact.ok);
    assert(std::memcmp(f.image.data() + kSiteStart, kSite, sizeof(kSite)) == 0);
    SetCapture(true);
    assert(Submit(Epoch(), 0.1f, 0.0f) == Route::Stick);
}

int main() {
    Fixture f;
    const bool pre=PreAttestForTest(f.image.data(),f.image.size(),&f.segment,1,f.profile,41,false);
    if (!pre) std::fprintf(stderr,"preflight: %s\n",Status());
    assert(pre);
    f.set_targets(f.target10,f.target30);
    assert(f.post()==nullptr);
    // Different near allocation: the disps/targets change but a matching proof still passes.
    f.set_targets(f.target10+0x100000,f.target30+0x100000);
    assert(f.post()==nullptr);
    // Coincidental equal byte: only one byte of a 4-byte disp differs; both writers still applied.
    {
        Fixture g;
        assert(PreAttestForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,41,false));
        g.set_targets(g.target10,g.target30);
        const std::uint32_t d10=get32(g.image.data()+kFps10+4);
        put32(g.image.data()+kFps10+4,d10^1);
        g.proof.entry[0].written_disp=get32(g.image.data()+kFps10+4);
        std::memcpy(g.proof.entry[0].written,g.image.data()+kFps10,8);
        g.proof.entry[0].target=g.proof.entry[0].next+
            static_cast<std::int64_t>(static_cast<std::int32_t>(g.proof.entry[0].written_disp));
        const std::uintptr_t next30=reinterpret_cast<std::uintptr_t>(g.image.data())+kFps30+8;
        g.proof.entry[1].target=g.proof.entry[0].target-4;
        const std::int32_t d30=static_cast<std::int32_t>(
            static_cast<std::int64_t>(g.proof.entry[1].target)-static_cast<std::int64_t>(next30));
        put32(g.image.data()+kFps30+4,static_cast<std::uint32_t>(d30));
        g.proof.entry[1].written_disp=static_cast<std::uint32_t>(d30);
        std::memcpy(g.proof.entry[1].written,g.image.data()+kFps30,8);
        g.target10=g.proof.entry[0].target; g.target30=g.proof.entry[1].target;
        assert(g.post()==nullptr);
    }
    // Missing/partial proof, wrong target, extra byte, opcode/site/constants tamper all reject.
    {
        Fixture g;
        assert(PreAttestForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,41,false));
        g.set_targets(g.target10,g.target30);
        CameraRedirectProof partial=g.proof; partial.entry[1].applied=false; partial.complete=false;
        assert(ValidatePostForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,&partial)!=nullptr);
        CameraRedirectProof bad=g.proof; bad.entry[0].target+=8;
        assert(ValidatePostForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,&bad)!=nullptr);
        CameraRedirectProof old=g.proof; old.sequence=40;
        assert(ValidatePostForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,&old)!=nullptr);
        g.image[kCameraStart+100]^=1;
        assert(g.post()!=nullptr);
    }
    {
        Fixture g;
        assert(PreAttestForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,41,false));
        g.set_targets(g.target10,g.target30);
        g.image[kFps10]^=1;
        assert(g.post()!=nullptr);
    }
    {
        Fixture g;
        assert(PreAttestForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,41,false));
        g.set_targets(g.target10,g.target30);
        g.image[kSiteStart]^=1;
        assert(g.post()!=nullptr);
    }
    {
        Fixture g;
        assert(PreAttestForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,41,false));
        g.set_targets(g.target10,g.target30);
        g.image[0x1000]^=1;
        assert(g.post()!=nullptr);
    }
    // Zero-diff original mode is accepted by the ordinary full-hash path (no proof required).
    {
        Fixture g;
        std::memcpy(g.image.data()+kFps10,kInsn10,8);
        std::memcpy(g.image.data()+kFps30,kInsn30,8);
        g.profile.camera_fnv1a64=fnv(g.image.data()+kCameraStart,kCameraEnd-kCameraStart);
        assert(PreAttestForTest(g.image.data(),g.image.size(),&g.segment,1,g.profile,41,false));
        assert(g.post()==nullptr);
    }
    // The RX-to-publish gap must revalidate both branches; inject at the same point.
    for (bool redirect : {false, true}) {
        for (int mutation : {1, 2}) {
            Fixture g;
            check_rx_reject(g, redirect, mutation);
        }
    }
    std::puts("PASS: staged FPS camera proof (near allocations, coincidence, partial/tamper rejection, RX injection)");
}
