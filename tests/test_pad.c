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
                          "pad.r1=righttrigger\nkey.cross=X, Space\npad.bogus=a\n";
    assert(write(config_fd,controls,sizeof(controls)-1)==(ssize_t)(sizeof(controls)-1));
    close(config_fd);
    setenv("BB_CONFIG",config,1);
    setenv("SDL_VIDEODRIVER","dummy",1);
    /* Only the virtual test controller is a gamepad, whatever is plugged in. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0x1d50/0x6189");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
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
    assert(pad_read_state(1,&data)==0 && data.touch_count==0 && data.buttons==0);
    capture=0;
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
    host.mouse_buttons=SDL_BUTTON_LMASK | SDL_BUTTON_MMASK;
    host.mouse_x=5;
    assert(pad_read_state(1,&data)==0);
    assert(data.left_y>64 && data.left_y<128 && data.right_x>128);
    assert((data.buttons & (BTN_R1|BTN_R3))==(BTN_R1|BTN_R3));
    assert(pad_read_state(1,&data)==0 && data.right_x==128);
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
    puts("PASS: pad ABI, debug camera chord, left/right clicks, SDL touch coordinates, overlay capture, controls");
}
