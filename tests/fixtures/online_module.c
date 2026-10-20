#include "gpu/bbnet/bbnet.h"
#include <stdlib.h>
#include <string.h>
static unsigned calls[4];
const unsigned *bbnet_test_calls(void) { return calls; }
#ifndef NO_INIT
int bbnet_init(const BbNetHost *host) {
    ++calls[0];
    return !host || getenv("BB_TEST_INIT_FAIL") ? -1 : 0;
}
#endif
uintptr_t bbnet_resolve(const char *nid) { return !strcmp(nid,"net") ? (uintptr_t)bbnet_resolve : 0; }
void bbnet_image_mapped(uint8_t *image,uint64_t size) { (void)image; (void)size; ++calls[1]; }
void bbnet_image_ready(uint64_t image,uint64_t size) { (void)image; (void)size; ++calls[2]; }
void bbnet_start(void) { ++calls[3]; }
