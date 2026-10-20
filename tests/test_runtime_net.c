#define _GNU_SOURCE
#include "../src/runtime_net.c"
#include <assert.h>
static unsigned callbacks;
const char *runtime_symbol(const char *name) { return !strcmp(name,"local") ? "sceNpTrophyInit" : "sceNetInit"; }
int32_t runtime_thread_spawn(void **thread, void *(ABI *entry)(void *), void *arg, uint64_t stack, const char *name) {
    (void)thread; (void)entry; (void)arg; (void)stack; (void)name; ++callbacks; abort();
}
int32_t runtime_thread_join_spawned(void *thread, void **result) { (void)thread; (void)result; ++callbacks; abort(); }
void runtime_thread_exit(void *value) { (void)value; ++callbacks; abort(); }
int runtime_memory_is_mapped(uintptr_t address,uint64_t size) { (void)address; (void)size; ++callbacks; abort(); }
int main(int argc,char **argv) {
    assert(argc==3);
    assert(!setenv("BB_ONLINE","1",1) && !setenv("BB_NET_MODULE",argv[1],1));
    int success=!strcmp(argv[2],"ok");
    if (!success) assert(!setenv("BB_TEST_INIT_FAIL","1",1));
    void *fixture=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(fixture);
    const unsigned *(*get_calls)(void)=(const unsigned *(*)(void))dlsym(fixture,"bbnet_test_calls"); assert(get_calls);
    runtime_net_configure("/tmp/unused-cpu","CUSA00900","CPU fixture","01.09");
    assert(!runtime_net_resolve("local"));
    assert((runtime_net_resolve("net")!=0)==success);
    assert((runtime_net_resolve("net")!=0)==success);
    runtime_net_image_mapped(NULL,0); runtime_net_image_ready(0,0); runtime_net_start();
    const unsigned *calls=get_calls();
    assert(calls[0]<=1);
    for(unsigned i=1;i<4;++i) assert(calls[i]==(unsigned)success);
    assert(!callbacks);
    puts("PASS: module published only after successful init; failed/missing exports get no callbacks");
}
