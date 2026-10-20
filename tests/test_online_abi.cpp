// CPU-only registration/ABI check: no game image, no session start, no account; network syscalls denied with EPERM.
#include "gpu/bbnet/bbnet.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <dlfcn.h>
#include "fixtures/no_network.h"
extern "C" const char* runtime_restart_block_reason() { std::abort(); }
extern "C" int runtime_extra_direct_map(uint64_t, uint64_t, void**) { std::abort(); }
static int callbacks;
static int32_t spawn(void**, void* (*)(void*), void*, uint64_t, const char*) { ++callbacks; std::abort(); }
static int32_t join(void*, void**) { ++callbacks; std::abort(); }
static void exit_thread(void*) { ++callbacks; std::abort(); }
static int mapped(uintptr_t, uint64_t) { ++callbacks; std::abort(); }
int main(int argc, char** argv) {
    assert(argc==2);
    bb_test_deny_network();
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!module) { std::fprintf(stderr,"dlopen: %s\n",dlerror()); return 1; }
    auto init=reinterpret_cast<BbNetInitFn>(dlsym(module,"bbnet_init"));
    auto resolve=reinterpret_cast<BbNetResolveFn>(dlsym(module,"bbnet_resolve"));
    assert(init && resolve && init(nullptr)!=0);
    BbNetHost host{};
    assert(init(&host)!=0);
    host.abi=BBNET_ABI_VERSION;
    assert(init(&host)!=0);
    host.thread_spawn=spawn; host.thread_join=join;
    host.thread_exit=exit_thread; host.memory_is_mapped=mapped;
    host.app0="/nonexistent-bbport-cpu-fixture"; host.serial="CUSA00900";
    host.title="CPU fixture"; host.app_ver="01.09";
    assert(init(&host)==0);
    const char* required[]={"Nlev7Lg8k3A#E#F","A9cVMUtEp4Y#K#L","hdpVEUDFW3s#L#M",
        "2rsFmlGWleQ#R#S","10t3e5+JPnU#P#Q","3KOuC4RmZZU#T#U"};
    for (const char* nid:required) assert(resolve(nid));
    assert(!resolve("invalid-import") && !resolve("XbkjbobZlCY#l#m"));
    assert(callbacks==0);
    // No dlclose: these modules have no audited stop/join ABI. This process only registered imports.
    std::puts("PASS: module ABI/registration/required imports; zero host callbacks; no session start");
}
