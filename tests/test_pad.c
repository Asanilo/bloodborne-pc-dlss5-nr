#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include "../src/runtime_pad.c"

static int capture;
static Uint16 rumble_low, rumble_high;
static bool SDLCALL test_rumble(void *unused, Uint16 low, Uint16 high) {
    (void)unused; rumble_low=low; rumble_high=high; return true;
}
static BbHostInput host;
static int mouse_route=BB_MOUSE_STICK, mouse_routes, mouse_cancels, strict_epoch;
static uint64_t routed_epoch, stub_epoch=1;
static float routed_pitch, routed_yaw;
void bbgpu_read_host_input(BbHostInput *out) { *out=host; host.mouse_x=host.mouse_y=0; host.wheel_up=host.wheel_down=0; }
int bbgpu_route_mouse(uint64_t epoch,float pitch,float yaw) {
    ++mouse_routes; routed_epoch=epoch; routed_pitch=pitch; routed_yaw=yaw;
    if (strict_epoch && epoch!=stub_epoch) return BB_MOUSE_DROPPED;
    return mouse_route;
}
int bbgpu_route_mouse_stick(uint64_t epoch) { return epoch==stub_epoch; }
void bbgpu_cancel_mouse_input(void) { ++mouse_cancels; ++stub_epoch; }
uint64_t bbgpu_mouse_epoch(void) { return stub_epoch; }
int bbgpu_overlay_captures_input(void) { return capture; }
void bbgpu_mouse_take(double *dx, double *dy, int *up, int *down) { *dx=*dy=0; *up=*down=0; }
int bbgpu_mouse_captured(void) { return 0; }
void bbgpu_mouse_capture(void) {}
void bbgpu_mouse_look_enable(int enabled) { (void)enabled; }
static int free_camera;
int bbgpu_free_camera(void) { return free_camera; }
int bbgpu_pointer_wanted(void) { return 0; }
void bbgpu_filter_pad(uint32_t *buttons, const uint8_t *sticks) { (void)buttons; (void)sticks; }
void runtime_re_watch_tick(const void *pad_buffer, uint64_t reads) { (void)pad_buffer; (void)reads; }
uintptr_t runtime_libc_base;
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    (void)table; (void)count; (void)name;
    return 0;
}

static void inject(const char *path, const char *tokens) {
    FILE *f=fopen(path,"w");
    assert(f);
    fputs(tokens,f);
    fclose(f);
    usleep(25000);
}

static void load_config(const char *path, const char *text) {
    FILE *f=fopen(path,"w");
    assert(f);
    assert(fputs(text,f)>=0);
    fclose(f);
    load_bindings();
}

/* Composite keys: parsed from the real load_bindings/key_down in src/runtime_pad.c, not a copy. */
static void test_chords(const char *config) {
    uint8_t keys[512]={0};
    const unsigned all_mods=MOD_LCTRL|MOD_RCTRL|MOD_LSHIFT|MOD_RSHIFT|
                            MOD_LALT|MOD_RALT|MOD_LGUI|MOD_RGUI;

    /* Upstream 0.6 defaults, all plain. */
    bind_defaults();
    assert(bindings[IN_L3].keys[0]==SDL_SCANCODE_LCTRL && bindings[IN_L3].mods[0]==0);
    assert(bindings[IN_OPTIONS].keys[0]==SDL_SCANCODE_ESCAPE);
    assert(bindings[IN_R2].keys[0]==MOUSE_KEY+SDL_BUTTON_LEFT &&
           bindings[IN_R2].mods[0]==MOD_LSHIFT);
    assert(bindings[IN_R1].keys[0]==MOUSE_KEY+SDL_BUTTON_LEFT && bindings[IN_R1].mods[0]==0);

    /* A plain old config and one single-modifier chord. C alone is still R3, Ctrl+C L3. */
    load_config(config,"key.cross=X, Space\nkey.l3=Left Ctrl+C\n");
    assert(bindings[IN_CROSS].key_count==2);
    assert(bindings[IN_CROSS].keys[0]==SDL_SCANCODE_X && bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE);
    assert(bindings[IN_CROSS].mods[0]==0 && bindings[IN_CROSS].mods[1]==0);
    assert(bindings[IN_L3].key_count==1 && bindings[IN_L3].keys[0]==SDL_SCANCODE_C &&
           bindings[IN_L3].mods[0]==MOD_LCTRL);
    keys[SDL_SCANCODE_C]=1;
    assert(key_down(keys,IN_R3) && !key_down(keys,IN_L3));
    keys[SDL_SCANCODE_LCTRL]=1;
    assert(key_down(keys,IN_L3) && !key_down(keys,IN_R3));
    /* The modifiers keep their own independent actions. */
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_LSHIFT]=1;
    assert(key_down(keys,IN_SPRINT) && !key_down(keys,IN_L3));
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_LCTRL]=1;
    assert(!key_down(keys,IN_L3)); /* the chord still needs C */

    /* A bare modifier name without '+' remains the plain key binding it always was. */
    load_config(config,"key.circle=Left Shift\n");
    assert(bindings[IN_CIRCLE].key_count==1 && bindings[IN_CIRCLE].keys[0]==SDL_SCANCODE_LSHIFT &&
           bindings[IN_CIRCLE].mods[0]==0);

    /* Two modifiers are more specific than one on the same primary key. */
    load_config(config,"key.l3=Left Ctrl+C\nkey.l1=Left Ctrl + Left Shift + C\n");
    assert(bindings[IN_L3].mods[0]==MOD_LCTRL);
    assert(bindings[IN_L1].mods[0]==(MOD_LCTRL|MOD_LSHIFT));
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_C]=1;
    assert(key_down(keys,IN_R3) && !key_down(keys,IN_L3) && !key_down(keys,IN_L1));
    keys[SDL_SCANCODE_LCTRL]=1;
    assert(key_down(keys,IN_L3) && !key_down(keys,IN_R3) && !key_down(keys,IN_L1));
    keys[SDL_SCANCODE_LSHIFT]=1;
    assert(key_down(keys,IN_L1) && !key_down(keys,IN_L3) && !key_down(keys,IN_R3));
    assert(key_down(keys,IN_SPRINT) && key_down(keys,IN_L3)==0);

    /* Mouse chords: the old compact name and SDL's spaced name, any case, plus suppression. */
    load_config(config,"key.square=Left Shift+MouseLeft\nkey.circle=  left shift + Mouse Left  \n");
    assert(bindings[IN_SQUARE].key_count==1 &&
           bindings[IN_SQUARE].keys[0]==MOUSE_KEY+SDL_BUTTON_LEFT &&
           bindings[IN_SQUARE].mods[0]==MOD_LSHIFT);
    assert(bindings[IN_CIRCLE].key_count==1 &&
           bindings[IN_CIRCLE].keys[0]==MOUSE_KEY+SDL_BUTTON_LEFT &&
           bindings[IN_CIRCLE].mods[0]==MOD_LSHIFT);
    memset(keys,0,sizeof keys);
    mouse_buttons=SDL_BUTTON_LMASK;
    assert(!key_down(keys,IN_SQUARE) && !key_down(keys,IN_CIRCLE) && key_down(keys,IN_R1));
    keys[SDL_SCANCODE_LSHIFT]=1;
    assert(key_down(keys,IN_SQUARE) && key_down(keys,IN_CIRCLE) && !key_down(keys,IN_R1));
    mouse_buttons=0; memset(keys,0,sizeof keys);
    assert(!key_down(keys,IN_SQUARE) && !key_down(keys,IN_R1));

    /* Keypad + stays one primary key, also as the primary of a chord. */
    load_config(config,"key.l2=Keypad +\nkey.r2=Left Ctrl+Keypad +\n");
    assert(bindings[IN_L2].keys[0]==SDL_SCANCODE_KP_PLUS && bindings[IN_L2].mods[0]==0);
    assert(bindings[IN_R2].keys[0]==SDL_SCANCODE_KP_PLUS && bindings[IN_R2].mods[0]==MOD_LCTRL);
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_KP_PLUS]=1;
    assert(key_down(keys,IN_L2) && !key_down(keys,IN_R2));
    keys[SDL_SCANCODE_LCTRL]=1;
    assert(key_down(keys,IN_R2) && !key_down(keys,IN_L2));

    /* Full-name case and surrounding spaces; all eight standard modifiers fit in the mask. */
    load_config(config,"key.touchpad=  lEfT cTrL + c  \n");
    assert(bindings[IN_TOUCHPAD].key_count==1 && bindings[IN_TOUCHPAD].keys[0]==SDL_SCANCODE_C &&
           bindings[IN_TOUCHPAD].mods[0]==MOD_LCTRL);
    load_config(config,"key.options=Left Ctrl+Right Ctrl+Left Shift+Right Shift+"
                       "Left Alt+Right Alt+Left GUI+Right GUI+K\n");
    assert(bindings[IN_OPTIONS].key_count==1 && bindings[IN_OPTIONS].keys[0]==SDL_SCANCODE_K &&
           bindings[IN_OPTIONS].mods[0]==all_mods);
    load_config(config,"key.options=Left Ctrl+Right Ctrl+Left Shift+Right Shift+Left Alt+"
                       "Right Alt+Left GUI+Right GUI+Left Ctrl+K\n");
    assert(bindings[IN_OPTIONS].key_count==0);

    /* Invalid and missing primaries, illegal modifiers and duplicate modifiers are rejected. */
    load_config(config,
        "key.sprint=Left Ctrl+\n"
        "key.l1=+C\n"
        "key.r2=Space+C\n"
        "key.square=F1+C\n"
        "key.triangle=Left Ctrl+Space+C\n"
        "key.circle=Left Ctrl+NoSuchKey\n"
        "key.options=Left Ctrl+Left Shift+\n"
        "key.touchpad_right=Left Ctrl+Left Ctrl+K\n");
    assert(bindings[IN_SPRINT].key_count==0 && bindings[IN_L1].key_count==0 &&
           bindings[IN_R2].key_count==0 && bindings[IN_SQUARE].key_count==0 &&
           bindings[IN_TRIANGLE].key_count==0 && bindings[IN_CIRCLE].key_count==0 &&
           bindings[IN_OPTIONS].key_count==0 && bindings[IN_TOUCHPAD_RIGHT].key_count==0);
    load_config(config,"key.square=Space+C, K\n");
    assert(bindings[IN_SQUARE].key_count==1 && bindings[IN_SQUARE].keys[0]==SDL_SCANCODE_K &&
           bindings[IN_SQUARE].mods[0]==0);
    load_config(config,"key.touchpad=A,B,C,D,E\n");
    assert(bindings[IN_TOUCHPAD].key_count==MAX_BIND);
    assert(bindings[IN_TOUCHPAD].keys[0]==SDL_SCANCODE_A && bindings[IN_TOUCHPAD].keys[1]==SDL_SCANCODE_B &&
           bindings[IN_TOUCHPAD].keys[2]==SDL_SCANCODE_C && bindings[IN_TOUCHPAD].keys[3]==SDL_SCANCODE_D);

    assert(key_from_name("MouseLeft")==MOUSE_KEY+SDL_BUTTON_LEFT);
    assert(key_from_name("Mouse Left")==MOUSE_KEY+SDL_BUTTON_LEFT);
    assert(key_from_name("mouse x2")==MOUSE_KEY+SDL_BUTTON_X2);
    assert(key_from_name("Keypad +")==SDL_SCANCODE_KP_PLUS &&
           key_from_name("keypad +")==SDL_SCANCODE_KP_PLUS);
}

int main(void) {
    char path[]="/tmp/bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    setenv("BB_PAD_FILE",path,1);
    char config[]="/tmp/bbport-pad-config-XXXXXX";
    int config_fd=mkstemp(config);
    assert(config_fd>=0);
    const char controls[]="upscaler=fsr3\npad.cross=b\npad.circle=a\npad.r2=rightshoulder\n"
                          "pad.r1=righttrigger\nkey.cross=X, Space\n"
                          "key.l3=Left Ctrl+C, Keypad +\npad.bogus=a\n";
    assert(write(config_fd,controls,sizeof(controls)-1)==(ssize_t)(sizeof(controls)-1));
    close(config_fd);
    setenv("BB_CONFIG",config,1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    test_chords(config);
    /* Slow-walk is usable without an old INI override, alongside upstream Ctrl=L3.
     * Test sustained input and release, not merely the binding table. */
    load_config(config,"");
    uint8_t walk_keys[512]={0};
    walk_keys[SDL_SCANCODE_W]=walk_keys[SDL_SCANCODE_LALT]=1;
    assert(key_down(walk_keys,IN_WALK) && !key_down(walk_keys,IN_L3));
    move_stick=(KeyStick){0}; look_stick=(KeyStick){0};
    PadData walking={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    for (unsigned i=1;i<=10;++i) { walking.timestamp=100000ull*i; apply_keyboard(&walking,walk_keys); }
    assert(walking.left_y>=76 && walking.left_y<=78 && walking.left_x==128);
    walk_keys[SDL_SCANCODE_LALT]=0;
    walking.timestamp+=100000; apply_keyboard(&walking,walk_keys);
    assert(walking.left_y<16); /* releasing Alt restores full keyboard movement */
    walk_keys[SDL_SCANCODE_LCTRL]=1;
    assert(!key_down(walk_keys,IN_WALK) && key_down(walk_keys,IN_L3));
    walking.timestamp+=100000; apply_keyboard(&walking,walk_keys);
    assert(walking.left_y<16 && (walking.buttons&BTN_L3));
    /* The modifier must not reduce an independent gamepad stick without WASD. */
    memset(walk_keys,0,sizeof walk_keys); walk_keys[SDL_SCANCODE_LALT]=1;
    move_stick=(KeyStick){0};
    walking=(PadData){.timestamp=2000000,.left_x=255,.left_y=0,.right_x=128,.right_y=128};
    apply_keyboard(&walking,walk_keys);
    assert(walking.left_x==255 && walking.left_y==0);
    /* Existing player profiles keep explicit slow-walk and keyboard camera speed. */
    load_config(config,"key.walk=V\nkey.look_right=Right\nkeyboard_look_speed=0.2\n");
    assert(bindings[IN_WALK].key_count==1 && keyboard_look_speed==0.2f);
    uint8_t legacy_keys[512]={0};
    legacy_keys[SDL_SCANCODE_W]=legacy_keys[SDL_SCANCODE_V]=legacy_keys[SDL_SCANCODE_RIGHT]=1;
    move_stick=(KeyStick){0}; look_stick=(KeyStick){0};
    PadData legacy={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    for (unsigned i=1;i<=10;++i) { legacy.timestamp=100000ull*i; apply_keyboard(&legacy,legacy_keys); }
    assert(legacy.left_y>=76 && legacy.left_y<128 && legacy.right_x>128 && legacy.right_x<=154);
    load_config(config,"keyboard_look_speed=nan\n"); assert(keyboard_look_speed==1.0f);
    load_config(config,"keyboard_look_speed=0.2invalid\n"); assert(keyboard_look_speed==1.0f);
    load_config(config,controls);
    move_stick=(KeyStick){0}; look_stick=(KeyStick){0};

    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471 && data.touches[0].id==1);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);

    /* HostInput keyboard chords through the real sampler. */
    host=(BbHostInput){0};
    host.keys[SDL_SCANCODE_ESCAPE]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_OPTIONS) && !(data.buttons & BTN_CIRCLE));
    host=(BbHostInput){0};
    host.keys[SDL_SCANCODE_LSHIFT]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_CIRCLE));
    host=(BbHostInput){0};
    host.keys[SDL_SCANCODE_C]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_R3) && !(data.buttons & BTN_L3));
    host.keys[SDL_SCANCODE_LCTRL]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_L3) && !(data.buttons & BTN_R3));
    host=(BbHostInput){0};

    /* Keyboard stick ramp: W must reach the left stick after a few samples. */
    PadData hybrid={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    uint8_t keyboard[512]={0};
    keyboard[SDL_SCANCODE_W]=1;
    move_stick=(KeyStick){0};
    for (int i=1;i<=10;++i) { hybrid.timestamp=1000000ull*i; apply_keyboard(&hybrid,keyboard); }
    assert(hybrid.left_y<128 && hybrid.left_x==128);

    /* Capture is the pad's authority, not blocked: F1/pointer/menu release must stop motion and
     * buttons, and the first warp after capture is not queued. */
    look_x=look_y=0; look_last_us=0;
    host.mouse_captured=0;
    host.mouse_x=5; host.mouse_buttons=SDL_BUTTON_LMASK;
    assert(pad_read_state(1,&data)==0 && data.right_x==128 && !(data.buttons & BTN_R1));
    host=(BbHostInput){0};
    host.mouse_captured=1;
    look_x=look_y=0; look_last_us=0;
    host.mouse_x=5;
    assert(pad_read_state(1,&data)==0 && data.right_x>128);
    look_x=look_y=0;
    host.mouse_x=0;
    assert(pad_read_state(1,&data)==0 && data.right_x==128);

    /* Wheel goes through the one HostInput snapshot: one press, then release. */
    host=(BbHostInput){0};
    host.mouse_captured=1;
    host.wheel_up=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_UP));
    host.wheel_up=0;
    usleep(70000);
    assert(pad_read_state(1,&data)==0 && !(data.buttons & BTN_UP));
    assert(pad_read_state(1,&data)==0 && !(data.buttons & BTN_UP));
    /* Queued notches must not survive a focus/menu loss or F1 capture release. */
    host.wheel_up=3;
    assert(pad_read_state(1,&data)==0);
    host.blocked=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0);
    assert(!wheel_pending[0] && !wheel_phase[0]);
    host.blocked=0; host.mouse_captured=1;
    host.wheel_up=2;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_UP));
    host.mouse_captured=0;
    assert(pad_read_state(1,&data)==0 && !(data.buttons & BTN_UP));
    assert(!wheel_pending[0] && !wheel_phase[0]);
    host=(BbHostInput){0};

    SDL_VirtualJoystickTouchpadDesc touch={.nfingers=2};
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.Rumble=test_rumble;
    desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.name="bbport test controller";
    desc.vendor_id=0x1d50;
    desc.product_id=0x6189;
    desc.ntouchpads=1;
    desc.touchpads=&touch;
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_Joystick *joystick=SDL_OpenJoystick(id);
    assert(joystick);
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,true,0.75f,0.5f,1.0f));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,true,0.25f,1.0f,1.0f));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,true));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(gamepad && data.touch_count==2 && (data.buttons & BTN_TOUCHPAD));
    assert(data.touches[0].x==1439 && data.touches[0].y==471 && data.touches[0].id==2);
    assert(data.touches[1].x==480 && data.touches[1].y==942 && data.touches[1].id==3);
    const uint8_t vibration[2]={128,255};
    assert(pad_vibration(1,vibration)==0 && rumble_low==128*257 && rumble_high==65535);
    capture=1;
    host.keys[SDL_SCANCODE_LCTRL]=1;
    host.keys[SDL_SCANCODE_C]=1;
    host.mouse_buttons=SDL_BUTTON_LMASK;
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
    host=(BbHostInput){0};
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,0,false,0,0,0));
    assert(SDL_SetJoystickVirtualTouchpad(joystick,0,1,false,0,0,0));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==480);
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_TOUCHPAD,false));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_EAST,true));
    assert(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));
    assert(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767));
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0);
    assert(data.buttons==(BTN_CROSS|BTN_R2|BTN_R1) && data.r2==255);
    assert(bindings[IN_CROSS].key_count==2 && bindings[IN_CROSS].keys[0]==SDL_SCANCODE_X &&
           bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE);
    host.keys[SDL_SCANCODE_W]=1;
    host.keys[SDL_SCANCODE_LCTRL]=1;
    host.mouse_captured=1;
    host.mouse_buttons=SDL_BUTTON_LMASK | SDL_BUTTON_MMASK;
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_R1|BTN_R3))==(BTN_R1|BTN_R3));
    host.keys[SDL_SCANCODE_C]=1;
    host.blocked=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.left_y==128);
    host=(BbHostInput){0};

    /* Mouse look: speed to deflection at two frame rates (one stable epoch for the stick
       accumulator; a changing epoch deliberately clears it). */
    mouse_route=BB_MOUSE_STICK;
    strict_epoch=0;
    for (int rate=60;rate<=240;rate*=4) {
        PadData m; memset(&m,0,sizeof m);
        look_x=look_y=0; look_last_us=1;
        const uint64_t step=1000000/rate;
        for (int i=1;i<=rate;++i) {
            m.right_x=m.right_y=128;
            apply_mouse_look(&m,500.0/rate,0,1+i*step,MOUSE_MODE_NATIVE,100);
        }
        assert(m.right_x>=128+16+50 && m.right_x<=128+16+62 && m.right_y==128);
        m.right_x=128;
        apply_mouse_look(&m,0,0,1+(rate+60)*step,MOUSE_MODE_NATIVE,100);
        for (int i=0;i<20;++i) apply_mouse_look(&m,0,0,1+(rate+61+i)*step,MOUSE_MODE_NATIVE,100);
        assert(m.right_x==128);
    }

    /* NativeQueued/Dropped never also drive the stick, and clear the old 60 ms tail. */
    for (int route=BB_MOUSE_DROPPED;route<=BB_MOUSE_NATIVE_QUEUED;++route) {
        mouse_route=route;
        PadData r={.right_x=128,.right_y=128};
        look_x=80.0f; look_y=-40.0f; look_last_us=1;
        const int before=mouse_routes;
        apply_mouse_look(&r,4,-2,1000000,MOUSE_MODE_NATIVE,73);
        assert(mouse_routes==before+1 && routed_epoch==73);
        assert(fabsf(routed_pitch+2*mouse_sensitivity*0.0015f)<1e-6f);
        assert(fabsf(routed_yaw-4*mouse_sensitivity*0.0015f)<1e-6f);
        assert(r.right_x==128 && r.right_y==128 && look_x==0 && look_y==0);
    }
    /* The explicit Stick route is the only one that feeds the legacy axis. */
    mouse_route=BB_MOUSE_STICK;
    {
        PadData r={.right_x=128,.right_y=128};
        look_x=look_y=0; look_last_us=0;
        const int before=mouse_routes;
        apply_mouse_look(&r,4,-2,1000000,MOUSE_MODE_NATIVE,74);
        assert(mouse_routes==before+1 && r.right_x>128 && r.right_y<128);
    }
    /* Free camera: captured but stick-only; a mode-transition batch has the cancelled epoch
       and drops, then a new generation is allowed to drive the stick without native submit. */
    {
        stub_epoch=75;
        PadData r={.right_x=128,.right_y=128};
        look_x=look_y=0; look_last_us=0;
        const int before=mouse_routes, cancels=mouse_cancels;
        apply_mouse_look(&r,4,-2,1000000,MOUSE_MODE_STICK,75);
        assert(mouse_routes==before && mouse_cancels==cancels+1);
        assert(r.right_x==128 && r.right_y==128);
        stub_epoch=76;
        apply_mouse_look(&r,4,-2,1000000,MOUSE_MODE_STICK,76);
        assert(mouse_routes==before && r.right_x>128 && r.right_y<128);
    }
    /* Uncaptured/menu: dropped, no stick, old pending is cancelled. */
    {
        PadData r={.right_x=128,.right_y=128};
        look_x=80.0f; look_y=-40.0f;
        const int before=mouse_routes, cancels=mouse_cancels;
        apply_mouse_look(&r,4,-2,1000000,MOUSE_MODE_DROPPED,0);
        assert(mouse_routes==before && mouse_cancels==cancels+1);
        assert(r.right_x==128 && r.right_y==128 && look_x==0 && look_y==0);
    }

    /* Actual sample_host: free camera is captured but stick-only, never native. The first
       sample after the mode switch drops; a fresh epoch then drives the stick. */
    mouse_route=BB_MOUSE_NATIVE_QUEUED;
    strict_epoch=1;
    stub_epoch=80;
    free_camera=1;
    host=(BbHostInput){.mouse_x=5,.mouse_captured=1,.mouse_epoch=80};
    {
        const int before=mouse_routes;
        assert(pad_read_state(1,&data)==0 && data.right_x==128 && mouse_routes==before);
    }
    stub_epoch=81;
    host=(BbHostInput){.mouse_x=5,.mouse_captured=1,.mouse_epoch=81};
    {
        const int before=mouse_routes;
        assert(pad_read_state(1,&data)==0 && data.right_x>128 && mouse_routes==before);
    }
    free_camera=0;
    stub_epoch=82;
    host=(BbHostInput){.mouse_x=5,.mouse_captured=1,.mouse_epoch=81};
    {
        const int before=mouse_routes;
        assert(pad_read_state(1,&data)==0 && data.right_x==128 && mouse_routes==before+1);
    }
    host=(BbHostInput){.mouse_x=5,.mouse_captured=1,.mouse_epoch=82};
    {
        const int before=mouse_routes;
        assert(pad_read_state(1,&data)==0 && data.right_x==128 && mouse_routes==before+1);
    }
    /* Touchpad/sideways aim consumes the delta; native sees only a zero heartbeat. */
    setenv("BB_PAD_MOTION","1",1);
    motion_enabled=1;
    motion.sideways=1;
    mouse_route=BB_MOUSE_NATIVE_QUEUED;
    stub_epoch=83;
    host=(BbHostInput){.mouse_x=5,.mouse_captured=1,.mouse_epoch=82};
    {
        const int before=mouse_routes;
        assert(pad_read_state(1,&data)==0 && data.right_x==128 && mouse_routes==before+1);
        assert(routed_pitch==0.0f && routed_yaw==0.0f);
    }
    motion.sideways=0;
    motion_enabled=0;
    strict_epoch=0;
    unsetenv("BB_PAD_MOTION");
    host=(BbHostInput){0};

    /* Same NATIVE mode, capture off/on: the new epoch's fallback must not inherit the old
       ~60 ms stick tail; the fresh delta still moves the stick. */
    {
        strict_epoch=0;
        mouse_route=BB_MOUSE_STICK;
        PadData r={.right_x=128,.right_y=128};
        look_x=look_y=0; look_last_us=0;
        apply_mouse_look(&r,4,0,1000000,MOUSE_MODE_NATIVE,90);
        const float old_tail=look_x;
        assert(old_tail>0);
        apply_mouse_look(&r,1,0,1001000,MOUSE_MODE_NATIVE,91);
        assert(look_x>0 && look_x<old_tail*0.5f);
    }

    SDL_CloseJoystick(joystick);
    if (gamepad) SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    puts("PASS: pad ABI, chords, SDL touch coordinates, overlay capture, controls, mouse look");
}
