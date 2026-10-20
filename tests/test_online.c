#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include "gpu/bbnet/bbnet.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "fixtures/no_network.h"

/* The settings menu of the GPU library restarts through probe.c, which tests do not link. */
void runtime_restart(void) { abort(); }
/* CPU harness: entering the launcher restart flow is outside these tests. */
const char *runtime_restart_block_reason(void) { abort(); }

/* Online play (BB_ONLINE=1): the game's network and PSN imports resolve to the online module
 * (out/gpu/libbbnet.so); trophies stay with the runtime. Without the module (a build without its
 * libraries) there is nothing to check. */
int main(int argc, char **argv) {
    bb_test_deny_network();
    char profile[]="/tmp/bbport-online-cpu-XXXXXX";
    assert(mkdtemp(profile));
    const char *variables[]={"HOME","BB_USER_DIR","BB_DATA_DIR","XDG_CONFIG_HOME","XDG_DATA_HOME","XDG_CACHE_HOME"};
    for (size_t i=0;i<sizeof(variables)/sizeof(*variables);++i) assert(!setenv(variables[i],profile,1));
    assert(!setenv("BB_BBSC","0",1) && !setenv("BB_NET_BACKEND","shadnet",1));
    runtime_net_configure("/nonexistent-bbport-cpu-fixture","CPU_TEST","CPU fixture","01.09");
    static const char *const online[]={
        "Nlev7Lg8k3A#E#F",  /* sceNetInit */
        "A9cVMUtEp4Y#K#L",  /* sceHttpInit */
        "hdpVEUDFW3s#L#M",  /* sceSslInit */
        "2rsFmlGWleQ#R#S",  /* sceNpCheckNpAvailability */
        "10t3e5+JPnU#P#Q",  /* sceNpMatching2Initialize */
        "3KOuC4RmZZU#T#U",  /* sceNpSignalingInitialize */
    };
    static const char *const local="XbkjbobZlCY#l#m"; /* sceNpTrophyCreateContext */
    const char *path=argc>1 ? argv[1] : "out/gpu/libbbnet.so";
    void *module=dlopen(path,RTLD_NOW|RTLD_LOCAL); /* the same handle the runtime gets */
    if (!module) {
        printf("%s: no online module (%s)\n",argc>1 ? "FAIL" : "SKIP",dlerror());
        return argc>1 ? 1 : 0;
    }
    const BbNetResolveFn resolve=(BbNetResolveFn)dlsym(module,"bbnet_resolve");
    assert(resolve);
    setenv("BB_ONLINE","1",1);
    setenv("BB_NET_MODULE",path,1);
    runtime_start(1);
    bbgpu_register_kernel();
    for (size_t i=0;i<sizeof(online)/sizeof(*online);++i) {
        uintptr_t address=runtime_resolve(online[i],0);
        if (!address || address!=resolve(online[i])) {
            printf("FAIL: %s (%s) is not the online version\n",online[i],runtime_import_name(online[i]));
            return 1;
        }
    }
    uintptr_t trophy=runtime_resolve(local,0);
    assert(trophy && trophy!=resolve(local));
    printf("PASS: online imports go to the online module, trophies stay local\n");
    return 0;
}
