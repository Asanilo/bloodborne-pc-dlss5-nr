// SPDX-License-Identifier: GPL-2.0-or-later
/* Recorded assignment names must round-trip through the actual game's binding parser. */
#include "../src/runtime_pad.c"
#include "../tools/input_capture.h"
#include <assert.h>
#include <unistd.h>

void bbgpu_read_host_input(BbHostInput *out) { memset(out,0,sizeof(*out)); }
int bbgpu_overlay_captures_input(void) { return 0; }
void bbgpu_mouse_look_enable(int enabled) { (void)enabled; }
void bbgpu_mouse_capture(void) {}
int bbgpu_route_mouse(uint64_t epoch, float pitch, float yaw) {
    (void)epoch; (void)pitch; (void)yaw; return BB_MOUSE_STICK;
}
int bbgpu_route_mouse_stick(uint64_t epoch) { (void)epoch; return 1; }
void bbgpu_cancel_mouse_input(void) {}
uint64_t bbgpu_mouse_epoch(void) { return 1; }
int bbgpu_free_camera(void) { return 0; }
int bbgpu_pointer_wanted(void) { return 0; }
void bbgpu_filter_pad(uint32_t *buttons, const uint8_t *sticks) { (void)buttons; (void)sticks; }
void runtime_re_watch_tick(const void *pad_buffer, uint64_t reads) { (void)pad_buffer; (void)reads; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    (void)table; (void)count; (void)name; return 0;
}
static void round_trip(const char *path,const char *name,const uint8_t *keys,unsigned buttons) {
    FILE *f=fopen(path,"w"); assert(f);
    // Remove independently bound modifier actions for this selected configuration.
    assert(fprintf(f,"key.r2=%s\nkey.circle=\nkey.walk=\n",name)>0);
    assert(!fclose(f));
    load_bindings();
    assert(bindings[IN_R2].key_count==1);
    mouse_buttons=buttons;
    PadData data={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    apply_keyboard(&data,keys);
    assert((data.buttons&BTN_R2) && !(data.buttons&BTN_R1) && data.r2==255);
}
int main(void) {
    char path[]="/tmp/bbport-chord-capture-XXXXXX", name[256];
    int fd=mkstemp(path); assert(fd>=0); close(fd);
    setenv("BB_CONFIG",path,1);
    BbInputCapture capture={0}; uint8_t keys[512]={0};
    SDL_Event event;
    const SDL_Scancode sequence[]={SDL_SCANCODE_LCTRL,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_K};
    for (int i=0;i<3;++i) {
        SDL_Event event; memset(&event,0,sizeof(event));
        event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=sequence[i];
        assert(bb_capture_event(&capture,&event,name,sizeof(name))==(i==2));
        keys[sequence[i]]=1;
    }
    round_trip(path,name,keys,0);
    const SDL_Scancode commas[]={SDL_SCANCODE_COMMA,SDL_SCANCODE_KP_COMMA};
    for (int i=0;i<2;++i) for (int modified=0;modified<2;++modified) {
        capture=(BbInputCapture){0};
        memset(keys,0,sizeof(keys)); memset(&event,0,sizeof(event));
        if (modified) {
            event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_LCTRL;
            assert(!bb_capture_event(&capture,&event,name,sizeof(name)));
            keys[SDL_SCANCODE_LCTRL]=1;
        }
        event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=commas[i];
        assert(bb_capture_event(&capture,&event,name,sizeof(name))==1);
        keys[commas[i]]=1;
        round_trip(path,name,keys,0);
    }
    assert(bb_capture_name("Mouse Left",SDL_KMOD_CTRL|SDL_KMOD_SHIFT|SDL_KMOD_ALT|SDL_KMOD_GUI,
                           name,sizeof(name)));
    memset(keys,0,sizeof(keys));
    const SDL_Scancode held[]={SDL_SCANCODE_LCTRL,SDL_SCANCODE_RCTRL,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT,
                               SDL_SCANCODE_LALT,SDL_SCANCODE_RALT,SDL_SCANCODE_LGUI,SDL_SCANCODE_RGUI};
    for (size_t i=0;i<sizeof(held)/sizeof(*held);++i) keys[held[i]]=1;
    round_trip(path,name,keys,SDL_BUTTON_LMASK);
    capture=(BbInputCapture){0};
    memset(&event,0,sizeof(event));
    event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_KP_PLUS;
    assert(bb_capture_event(&capture,&event,name,sizeof(name))==1);
    memset(keys,0,sizeof(keys)); keys[SDL_SCANCODE_KP_PLUS]=1;
    round_trip(path,name,keys,0);
    assert(!unlink(path));
    puts("PASS: captured keyboard/mouse/keypad names -> saved INI -> actual pad actions");
}
