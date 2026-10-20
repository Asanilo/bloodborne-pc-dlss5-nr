// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the production stub against a real three-instruction reference, CPU only.
#ifdef NDEBUG
#error CPU validation requires assertions and their checked operations
#endif
#include "gpu/shim/bbport_mouse_look.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cpuid.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <ucontext.h>
#include <vector>

struct RunContext {
    void* site;
    unsigned char *input, *output, *host;
    std::uint64_t mask;
    unsigned char *frame, *camera, *stack;
};
static_assert(sizeof(RunContext)==64 && offsetof(RunContext, mask)==32 &&
              offsetof(RunContext, stack)==56);
extern "C" {
RunContext* bb_xstate_context;
void* bb_xstate_site;
std::uint64_t bb_xstate_host_rsp, bb_xstate_gprs[16], bb_xstate_flags;
void bb_mouse_xstate_run();
}
namespace {
using BbMouseLook::TestSeam::Profile;
using BbNativeMouse::SubmitResult;
constexpr unsigned char signature[16]={0xc5,0xa8,0x59,0xc9,0xc5,0xf8,0x28,0x95,
                                      0x10,0xfc,0xff,0xff,0xc5,0xe8,0x59,0xc9};
std::uint64_t hash(const unsigned char* p,std::size_t n) {
    std::uint64_t h=14695981039346656037ULL;
    while(n--) { h^=*p++; h*=1099511628211ULL; }
    return h;
}
std::uint64_t now() {
    timespec ts{}; assert(!clock_gettime(CLOCK_MONOTONIC,&ts));
    return std::uint64_t(ts.tv_sec)*1000000+std::uint64_t(ts.tv_nsec)/1000;
}
template<class T> T get(const unsigned char* p) { T v; std::memcpy(&v,p,sizeof(v)); return v; }
template<class T> void put(unsigned char* p,T v) { std::memcpy(p,&v,sizeof(v)); }
struct Area {
    unsigned char* p{};
    std::size_t size;
    explicit Area(std::size_t n):size(n) {
        void* value=nullptr; assert(!posix_memalign(&value,64,n));
        p=static_cast<unsigned char*>(value); std::memset(p,0,n);
    }
    ~Area() { std::free(p); }
    Area(const Area&)=delete;
};
struct Component { unsigned bit,offset,size; };
std::vector<Component> components;
volatile sig_atomic_t signals_seen;
volatile sig_atomic_t signal_stub_alt_hit, signal_consuming_hit;
std::atomic<std::uintptr_t> stub_start,stub_end,alt_start,alt_end;
std::atomic<bool> running_with_motion;
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
void on_signal(int,siginfo_t*,void* raw) {
    volatile unsigned char local=0;
    const auto address=reinterpret_cast<std::uintptr_t>(&local);
    const auto* context=static_cast<const ucontext_t*>(raw);
    const auto rip=std::uintptr_t(context->uc_mcontext.gregs[REG_RIP]);
    signals_seen=1;
    if(rip>=stub_start.load(std::memory_order_relaxed) && rip<stub_end.load(std::memory_order_relaxed) &&
       address>=alt_start.load(std::memory_order_relaxed) && address<alt_end.load(std::memory_order_relaxed)) {
        signal_stub_alt_hit=1;
        if(running_with_motion.load(std::memory_order_relaxed)) signal_consuming_hit=1;
    }
}
bool frame_preserved(const unsigned char* before,const unsigned char* after,std::size_t size,bool motion) {
    for(std::size_t i=0;i<size;++i) {
        if(motion && i>=0x3c && i<0x40) continue;
        if(before[i]!=after[i]) return false;
    }
    return true;
}
void compare_xstate(const Area& reference,const Area& actual,bool motion) {
    // Compare architectural fields only: no FXSAVE reserved bytes, ST padding,
    // XSAVE header padding or init-component storage.
    const auto* a=reference.p; const auto* b=actual.p;
    assert(get<std::uint64_t>(a+512)==get<std::uint64_t>(b+512));
    assert(!std::memcmp(a,b,5));       // FCW, FSW, abridged tag
    assert(!std::memcmp(a+6,b+6,18));  // FOP, instruction/data pointers
    assert(!std::memcmp(a+24,b+24,4)); // MXCSR, including sticky flags
    for (unsigned i=0;i<8;++i) assert(!std::memcmp(a+32+i*16,b+32+i*16,10));
    for (unsigned i=0;i<16;++i) {
        const unsigned skip=motion && i==1 ? 8:0;
        assert(!std::memcmp(a+160+i*16+skip,b+160+i*16+skip,16-skip));
    }
    if (motion) {
        assert(get<float>(b+160+16)==1.25f);
        assert(get<float>(b+160+20)==1.5f);
    }
    const auto used=get<std::uint64_t>(a+512);
    for (const auto c:components) {
        if (!(used&(1ULL<<c.bit))) continue; // architectural init state, padding unspecified
        assert(c.bit==2 || c.bit==5 || c.bit==6 || c.bit==7 || c.bit==9);
        unsigned bytes=c.size;
        if (c.bit==9) bytes=4; // PKRU's upper dword is reserved
        assert(!std::memcmp(a+c.offset,b+c.offset,bytes));
    }
}
}
int main() {
    unsigned a,b,c,d;
    assert(__get_cpuid_max(0,nullptr)>=0xd);
    assert(__get_cpuid(1,&a,&b,&c,&d) && (c&bit_XSAVE) && (c&bit_OSXSAVE) && (c&bit_AVX));
    unsigned low,high;
    asm volatile("xgetbv":"=a"(low),"=d"(high):"c"(0));
    const std::uint64_t mask=std::uint64_t(low)|(std::uint64_t(high)<<32);
    assert((mask&7)==7);
    __cpuid_count(0xd,0,a,b,c,d);
    const std::uint64_t supported=std::uint64_t(a)|(std::uint64_t(d)<<32);
    assert((mask&~supported)==0);
    const std::size_t size=b;
    assert(size>=576 && size<1024*1024);
    Area input(size), reference(size), actual(size), host(size), frame(0x400), camera(0x400), stack(0x4000);
    asm volatile("xsave64 (%0)"::"r"(input.p),"a"(low),"d"(high):"memory");
    // A valid full x87 stack and known SIMD sentinels. Host state is saved/restored
    // separately by the fixture, so C++ sees its original ABI state after each run.
    std::memset(input.p,0,24);
    put<std::uint16_t>(input.p,0x037f); input.p[4]=0xff;
    for(unsigned i=0;i<8;++i) {
        const long double value=i+1;
        std::memcpy(input.p+32+i*16,&value,10);
    }
    put<std::uint32_t>(input.p+24,0x1fa5);
    for(unsigned i=0;i<16;++i) std::memset(input.p+160+i*16,0x40+i,16);
    const std::uint32_t angles[]={0x3f800000,0x40000000,0x7fc12345,0x80000000};
    std::memcpy(input.p+176,angles,16);
    for(unsigned i=0;i<4;++i) put<float>(input.p+160+10*16+i*4,1.f);
    std::uint64_t used=get<std::uint64_t>(input.p+512)|7;
    for(unsigned bit=2;bit<64;++bit) {
        if (!(mask&(1ULL<<bit))) continue;
        __cpuid_count(0xd,bit,a,b,c,d);
        assert(a>0 && b>=576 && b<=size && a<=size-b && !(c&1));
        components.push_back({bit,b,a});
        if(bit==2 || bit==5 || bit==6 || bit==7) {
            std::memset(input.p+b,0x50+bit,a); used|=1ULL<<bit;
        } else if (bit!=9 && (used&(1ULL<<bit))) {
            std::fprintf(stderr,"unhandled non-init XSTATE component %u\n",bit);
            return 77;
        }
    }
    assert((used&~mask)==0);
    put(input.p+512,used); put<std::uint64_t>(input.p+520,0);
    std::memset(input.p+528,0,48);
    constexpr std::size_t image_size=0x6000, site=0x2000;
    auto* image=static_cast<unsigned char*>(mmap(nullptr,image_size,PROT_READ|PROT_WRITE,
                                               MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(image!=MAP_FAILED);
    std::memset(image,0x90,image_size); std::memcpy(image+site,signature,16); image[site+16]=0xc3;
    Profile profile{};
    profile.camera_start=0x1000; profile.camera_end=0x3000;
    profile.constants_start=0x5000; profile.constants_end=0x5040;
    profile.site_start=site; profile.site_bytes=signature;
    profile.camera_fnv1a64=hash(image+0x1000,0x2000);
    profile.constants_fnv1a64=hash(image+0x5000,0x40);
    BbMouseLook::Segment segment{0,image_size,5};
    RunContext context{image+site,input.p,reference.p,host.p,mask,frame.p,camera.p,stack.p+stack.size-8};
    bb_xstate_context=&context; bb_xstate_site=image+site;
    auto prepare=[&] {
        std::memset(frame.p,0x6a,frame.size); std::memset(camera.p,0x7b,camera.size);
        std::memset(stack.p,0xa5,stack.size);
        for(unsigned i=0;i<4;++i) put<float>(frame.p+0x10+i*4,1.f);
    };
    prepare();
    std::array<unsigned char,0x400> frame_before{};
    std::memcpy(frame_before.data(),frame.p,frame.size);
    frame.p[0x10]^=1;
    assert(!frame_preserved(frame_before.data(),frame.p,frame.size,false));
    prepare();
    assert(!mprotect(image,image_size,PROT_READ|PROT_EXEC));
    bb_mouse_xstate_run();
    const auto flags=bb_xstate_flags;
    std::array<std::uint64_t,16> gprs{};
    std::memcpy(gprs.data(),bb_xstate_gprs,sizeof(bb_xstate_gprs));
    assert(!mprotect(image,image_size,PROT_READ|PROT_WRITE));
    BbNativeMouse::Mailbox mailbox;
    auto artifact=BbMouseLook::TestSeam::InstallForTest(image,image_size,&segment,1,profile,&mailbox);
    assert(artifact.ok);
    assert(!mprotect(image,image_size,PROT_READ|PROT_EXEC));
    context.output=actual.p;
    auto run=[&](bool motion) {
        prepare(); std::memset(actual.p,0,actual.size);
        std::memcpy(frame_before.data(),frame.p,frame.size);
        running_with_motion.store(motion,std::memory_order_relaxed);
        bb_mouse_xstate_run();
        running_with_motion.store(false,std::memory_order_relaxed);
        assert(bb_xstate_flags==flags && !std::memcmp(gprs.data(),bb_xstate_gprs,sizeof(bb_xstate_gprs)));
        compare_xstate(reference,actual,motion);
        const auto* zone=context.stack-128;
        for(unsigned i=0;i<128;++i) assert(zone[i]==0xa5);
        assert(frame_preserved(frame_before.data(),frame.p,frame.size,motion));
        for(unsigned i=0;i<camera.size;++i) {
            if (motion && ((i>=0x154 && i<0x158) || i==0x15c)) continue;
            assert(camera.p[i]==0x7b);
        }
        if (motion) {
            assert(get<std::uint32_t>(frame.p+0x3c)==1);
            assert(get<std::uint32_t>(camera.p+0x154)==0 && camera.p[0x15c]==1);
        }
    };
    run(false); // zero-displacement heartbeat proves readiness
    assert(mailbox.Submit(mailbox.Epoch(),.25f,-.5f,now())==SubmitResult::Native);
    run(true);
    run(false); // no duplicate consumption
    unsigned long fs_before,gs_before,fs_after,gs_after;
    assert(!syscall(SYS_arch_prctl,ARCH_GET_FS,&fs_before));
    assert(!syscall(SYS_arch_prctl,ARCH_GET_GS,&gs_before));
    assert(!syscall(SYS_arch_prctl,ARCH_SET_GS,camera.p));
    Area alt(64*1024);
    stub_start.store(reinterpret_cast<std::uintptr_t>(artifact.stub));
    stub_end.store(reinterpret_cast<std::uintptr_t>(artifact.stub)+artifact.stub_size);
    alt_start.store(reinterpret_cast<std::uintptr_t>(alt.p));
    alt_end.store(reinterpret_cast<std::uintptr_t>(alt.p)+alt.size);
    stack_t old_stack{}, alternate{alt.p,0,alt.size};
    assert(!sigaltstack(&alternate,&old_stack));
    struct sigaction action{}, old_action{};
    action.sa_sigaction=on_signal; action.sa_flags=SA_SIGINFO|SA_ONSTACK;
    sigemptyset(&action.sa_mask); assert(!sigaction(SIGUSR1,&action,&old_action));
    sigset_t signal_set, old_mask;
    sigemptyset(&signal_set); sigaddset(&signal_set,SIGUSR1);
    assert(!pthread_sigmask(SIG_UNBLOCK,&signal_set,&old_mask));
    std::atomic<bool> stop{};
    const auto target=pthread_self();
    std::thread sender([&] {
        while(!stop.load()) { assert(!pthread_kill(target,SIGUSR1)); usleep(100); }
    });
    for(unsigned i=0;i<20000 && (i<200 || !signal_consuming_hit);++i) {
        assert(mailbox.Submit(mailbox.Epoch(),.25f,-.5f,now())==SubmitResult::Native);
        run(true);
    }
    stop.store(true); sender.join();
    assert(signals_seen && signal_stub_alt_hit && signal_consuming_hit);
    assert(!syscall(SYS_arch_prctl,ARCH_GET_FS,&fs_after));
    assert(!syscall(SYS_arch_prctl,ARCH_GET_GS,&gs_after));
    assert(fs_after==fs_before && gs_after==reinterpret_cast<unsigned long>(camera.p));
    assert(!syscall(SYS_arch_prctl,ARCH_SET_GS,gs_before));
    assert(!pthread_sigmask(SIG_BLOCK,&signal_set,nullptr));
    timespec no_wait{};
    while(sigtimedwait(&signal_set,nullptr,&no_wait)>=0) {}
    assert(!sigaction(SIGUSR1,&old_action,nullptr)); assert(!sigaltstack(&old_stack,nullptr));
    assert(!pthread_sigmask(SIG_SETMASK,&old_mask,nullptr));
    BbMouseLook::TestSeam::UninstallForTest(artifact); // no execution after fixture teardown
    assert(!munmap(image,image_size));
    std::printf("PASS: real original instructions vs production stub; XCR0=0x%llx, tested_noninit_BV=0x%llx, XSAVE=%zu; x87/SSE/YMM",
                static_cast<unsigned long long>(mask),static_cast<unsigned long long>(used),size);
    if ((mask&0xe0)==0xe0) std::printf("/opmask/ZMM");
    std::puts("; GPR/RFLAGS/red-zone/allowed writes/FS/GS/observed-stub-consuming-altstack-signal; other components retain init");
}
