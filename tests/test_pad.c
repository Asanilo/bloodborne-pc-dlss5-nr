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
void bbgpu_read_host_input(BbHostInput *out) { *out=host; host.mouse_x=host.mouse_y=0; }
int bbgpu_overlay_captures_input(void) { return capture; }
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

    /* Every old default stays a plain binding. */
    bind_defaults();
    for (int input=0;input<IN_COUNT;++input)
        for (int i=0;i<bindings[input].key_count;++i) assert(bindings[input].mods[i]==0);
    assert(bindings[IN_WALK].keys[0]==SDL_SCANCODE_LCTRL);
    assert(bindings[IN_CROSS].keys[0]==SDL_SCANCODE_KP_ENTER &&
           bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE);
    assert(bindings[IN_OPTIONS].keys[0]==SDL_SCANCODE_ESCAPE);

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
    assert(key_down(keys,IN_CIRCLE) && !key_down(keys,IN_L3));
    memset(keys,0,sizeof keys);
    keys[SDL_SCANCODE_LCTRL]=1;
    assert(key_down(keys,IN_WALK) && !key_down(keys,IN_L3));

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
    /* The modifiers still do their own actions while their chord is held. */
    assert(key_down(keys,IN_CIRCLE) && key_down(keys,IN_WALK));
    keys[SDL_SCANCODE_LCTRL]=0;
    assert(!key_down(keys,IN_L1) && !key_down(keys,IN_L3) && key_down(keys,IN_R3));

    /* Mouse chords: the old compact name and SDL's spaced name, any case, plus suppression. */
    load_config(config,"key.square=Left Shift+MouseLeft\nkey.circle=  left shift + Mouse Left  \n");
    assert(bindings[IN_SQUARE].key_count==1 && bindings[IN_SQUARE].keys[0]==MOUSE_KEY_BASE+0 &&
           bindings[IN_SQUARE].mods[0]==MOD_LSHIFT);
    assert(bindings[IN_CIRCLE].key_count==1 && bindings[IN_CIRCLE].keys[0]==MOUSE_KEY_BASE+0 &&
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
    /* A ninth modifier is rejected (there are only eight distinct ones). */
    load_config(config,"key.options=Left Ctrl+Right Ctrl+Left Shift+Right Shift+Left Alt+"
                       "Right Alt+Left GUI+Right GUI+Left Ctrl+K\n");
    assert(bindings[IN_OPTIONS].key_count==0);

    /* Invalid and missing primaries, illegal modifiers and duplicate modifiers are rejected. */
    load_config(config,
        "key.walk=Left Ctrl+\n"
        "key.l1=+C\n"
        "key.r2=Space+C\n"
        "key.square=F1+C\n"
        "key.triangle=Left Ctrl+Space+C\n"
        "key.circle=Left Ctrl+NoSuchKey\n"
        "key.options=Left Ctrl+Left Shift+\n"
        "key.l3=Left Ctrl+Left Ctrl\n"
        "key.touchpad_right=Left Ctrl+Left Ctrl+K\n");
    assert(bindings[IN_WALK].key_count==0 && bindings[IN_L1].key_count==0 &&
           bindings[IN_R2].key_count==0 && bindings[IN_SQUARE].key_count==0 &&
           bindings[IN_TRIANGLE].key_count==0 && bindings[IN_CIRCLE].key_count==0 &&
           bindings[IN_OPTIONS].key_count==0 && bindings[IN_TOUCHPAD_RIGHT].key_count==0 &&
           bindings[IN_L3].key_count==0);
    /* An invalid alternative does not turn into a plain key, and valid ones still load. */
    load_config(config,"key.square=Space+C, K\n");
    assert(bindings[IN_SQUARE].key_count==1 && bindings[IN_SQUARE].keys[0]==SDL_SCANCODE_K &&
           bindings[IN_SQUARE].mods[0]==0);
    /* Four alternatives at most: the fifth is refused, not stored. */
    load_config(config,"key.touchpad=A,B,C,D,E\n");
    assert(bindings[IN_TOUCHPAD].key_count==MAX_BIND);
    assert(bindings[IN_TOUCHPAD].keys[0]==SDL_SCANCODE_A && bindings[IN_TOUCHPAD].keys[1]==SDL_SCANCODE_B &&
           bindings[IN_TOUCHPAD].keys[2]==SDL_SCANCODE_C && bindings[IN_TOUCHPAD].keys[3]==SDL_SCANCODE_D);

    /* Name lookup: both mouse spellings, any case, and the complete SDL Keypad names. */
    assert(key_from_name("MouseLeft")==MOUSE_KEY_BASE && key_from_name("Mouse Left")==MOUSE_KEY_BASE);
    assert(key_from_name("mouse x2")==MOUSE_KEY_BASE+4);
    assert(key_from_name("Keypad +")==SDL_SCANCODE_KP_PLUS && key_from_name("keypad +")==SDL_SCANCODE_KP_PLUS);
}

int main(void) {
    char path[]="/tmp/bbport-pad-test-XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    setenv("BB_PAD_FILE",path,1);
    /* bbport.ini controls: buttons moved, a trigger as a button and a button as a trigger. */
    char config[]="/tmp/bbport-pad-config-XXXXXX";
    int config_fd=mkstemp(config);
    assert(config_fd>=0);
    const char controls[]="upscaler=fsr3\npad.cross=b\npad.circle=a\npad.r2=rightshoulder\n"
                          "pad.r1=righttrigger\nkey.cross=X, Space\nkey.l3=Left Ctrl+C\npad.bogus=a\n";
    assert(write(config_fd,controls,sizeof(controls)-1)==(ssize_t)(sizeof(controls)-1));
    close(config_fd);
    setenv("BB_CONFIG",config,1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    test_chords(config);
    load_config(config,controls); /* restore the main config before sampling */
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==1);
    PadData data;
    inject(path,"cross l3 touchpad_left");
    assert(pad_read_state(1,&data)==0);
    assert((data.buttons & (BTN_CROSS|BTN_L3|BTN_TOUCHPAD))==(BTN_CROSS|BTN_L3|BTN_TOUCHPAD));
    /* A new touch gets a new id (1..127), as from a DualShock 4: the game ignores id 0. */
    assert(data.touch_count==1 && data.touches[0].x==480 && data.touches[0].y==471 && data.touches[0].id==1);
    inject(path,"touchpad_right");
    assert(pad_read_state(1,&data)==0 && data.touch_count==1 && data.touches[0].x==1440);
    inject(path,"");
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.touch_count==0);

    /* Upstream biased-stick calibration must coexist with the local keyboard/mouse path. */
    cal_reset();
    const int16_t biased[4]={16380,-16380,16380,-16380};
    for (int i=0;i<PAD_CAL_QUIET_SAMPLES;++i) cal_sample(biased);
    assert(cal_have_center);
    for (int i=0;i<4;++i) assert(stick_axis(i,biased[i])==128);
    assert(stick_axis(0,0)==0 && stick_axis(0,32760)==255);
    PadData hybrid={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    uint8_t keyboard[512]={0};
    keyboard[SDL_SCANCODE_W]=1;
    apply_keyboard(&hybrid,keyboard);
    assert(hybrid.left_y<10 && hybrid.right_x==128);
    cal_reset();
    const int16_t held[4]={16380,0,0,0};
    for (int i=0;i<PAD_CAL_QUIET_SAMPLES;++i) cal_sample(held);
    assert(cal_have_center && cal_center[0]==0 && stick_axis(0,held[0])>128);

    host.keys[SDL_SCANCODE_ESCAPE]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_OPTIONS) && !(data.buttons & BTN_CIRCLE));
    host=(BbHostInput){0};
    host.keys[SDL_SCANCODE_LSHIFT]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_CIRCLE) && !(data.buttons & BTN_OPTIONS));
    host=(BbHostInput){0};
    /* The chord through the real sampling path: C is R3, Ctrl+C is L3 and not R3. */
    host.keys[SDL_SCANCODE_C]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_R3) && !(data.buttons & BTN_L3));
    host.keys[SDL_SCANCODE_LCTRL]=1;
    assert(pad_read_state(1,&data)==0 && (data.buttons & BTN_L3) && !(data.buttons & BTN_R3));
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
           bindings[IN_CROSS].keys[1]==SDL_SCANCODE_SPACE &&
           bindings[IN_CROSS].mods[0]==0 && bindings[IN_CROSS].mods[1]==0);
    host.keys[SDL_SCANCODE_W]=1;
    host.keys[SDL_SCANCODE_LCTRL]=1;
    host.mouse_buttons=SDL_BUTTON_LMASK | SDL_BUTTON_MMASK;
    host.mouse_x=5;
    assert(pad_read_state(1,&data)==0);
    assert(data.left_y>64 && data.left_y<128 && data.right_x>128);
    assert((data.buttons & (BTN_R1|BTN_R3))==(BTN_R1|BTN_R3));
    assert(pad_read_state(1,&data)==0 && data.right_x==128);
    host.keys[SDL_SCANCODE_C]=1;
    host.blocked=1;
    assert(pad_read_state(1,&data)==0 && data.buttons==0 && data.left_y==128);
    host=(BbHostInput){0};
    assert(mouse_axis(5,0.016f,128)>128 && mouse_axis(-5,0.016f,128)<128);
    assert(mouse_axis(0,0.016f,42)==42);
    assert(input_number("nan",1,0.05f,10)==1);
    assert(key_axis(128,1,1)==128);
    SDL_CloseJoystick(joystick);
    assert(SDL_DetachVirtualJoystick(id));
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && gamepad==NULL && data.buttons==0);
    id=SDL_AttachVirtualJoystick(&desc);
    assert(id!=0);
    SDL_UpdateGamepads();
    assert(pad_read_state(1,&data)==0 && gamepad!=NULL);
    SDL_CloseGamepad(gamepad);
    gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    SDL_Quit();
    unlink(path);
    unlink(config);
    puts("PASS: pad ABI, debug camera chord, left/right clicks, SDL touch coordinates, overlay capture, controls, composite keys");
}
