#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual edge functions (not a source-string check) for Astra's four findings."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1') if args.sanitize else dict(os.environ)
cxx = os.environ.get('CXX', 'clang++')
flags = ['-std=c++23', '-O1', '-g', '-UNDEBUG', '-pthread', '-mavx', '-msse4.1', '-mxsave', *san,
         '-I' + str(root)]

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    at = brace + 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]

with tempfile.TemporaryDirectory(prefix='bb-mouse-edges-') as tmp:
    tmp = Path(tmp)
    pad = (root / 'src/runtime_pad.c').read_text()
    pad_source = r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "gpu/shim/bbport_mouse_look.h"
#include "gpu/bbgpu.h"
struct PadData { uint8_t right_x=128,right_y=128; };
static float look_x,look_y,mouse_sensitivity=1; static int mouse_invert_y;
static uint64_t look_last_us,look_epoch;
#define LOOK_TAU_US 60000.0f
#define MOUSE_RAD_PER_PIXEL 0.0015f
enum { MOUSE_MODE_DROPPED=0, MOUSE_MODE_STICK=1, MOUSE_MODE_NATIVE=2 };
extern "C" void bbgpu_cancel_mouse_input(){BbMouseLook::Cancel();}
extern "C" int bbgpu_route_mouse_stick(uint64_t e){return BbMouseLook::StickOnly(e)?1:0;}
extern "C" int bbgpu_route_mouse(uint64_t e,float p,float y){
 switch(BbMouseLook::Submit(e,p,y)){
 case BbMouseLook::Route::Stick:return BB_MOUSE_STICK;
 case BbMouseLook::Route::Dropped:return BB_MOUSE_DROPPED;
 case BbMouseLook::Route::NativeQueued:return BB_MOUSE_NATIVE_QUEUED;
 }return BB_MOUSE_DROPPED;
}
''' + function(pad, 'static uint8_t look_axis(') + '\n' + \
        function(pad, 'static void clear_mouse_look_tail(') + '\n' + \
        function(pad, 'static void apply_mouse_look(') + r'''
int main(){
 unsetenv("BB_NATIVE_MOUSE"); BbMouseLook::PatchImage(nullptr,0,nullptr,0);
 BbMouseLook::SetCapture(true);
 PadData d;
 // 1: a free-camera mode transition has the cancelled epoch and drops.
 uint64_t e=BbMouseLook::Epoch();
 apply_mouse_look(&d,4,0,1000000,MOUSE_MODE_STICK,e);
 assert(d.right_x==128 && d.right_y==128);
 e=BbMouseLook::Epoch(); d={};
 apply_mouse_look(&d,4,0,1001000,MOUSE_MODE_STICK,e);
 assert(d.right_x>128);
 // 1: a snapshot read before capture-off cannot drive the stick.
 BbMouseLook::SetCapture(false); e=BbMouseLook::Epoch(); d={};
 apply_mouse_look(&d,4,0,1002000,MOUSE_MODE_STICK,e);
 assert(d.right_x==128);
 // 2: recapture in the same NATIVE mode clears the old 60 ms tail, then uses the new delta.
 BbMouseLook::SetCapture(true); e=BbMouseLook::Epoch(); d={};
 look_x=look_y=0; look_last_us=0;
 apply_mouse_look(&d,4,0,2000000,MOUSE_MODE_NATIVE,e); // mode transition: dropped
 e=BbMouseLook::Epoch(); d={};
 apply_mouse_look(&d,4,0,2000500,MOUSE_MODE_NATIVE,e);
 const float old_tail=look_x;
 assert(old_tail>0);
 BbMouseLook::SetCapture(false); BbMouseLook::SetCapture(true);
 e=BbMouseLook::Epoch(); d={};
 apply_mouse_look(&d,1,0,2001000,MOUSE_MODE_NATIVE,e);
 assert(look_x>0 && look_x<old_tail*0.5f);
 puts("PASS: actual pad stick-only epoch/capture and recapture-tail edges");
}
'''
    (tmp/'pad.cpp').write_text(pad_source)
    subprocess.run([cxx, *flags, str(tmp/'pad.cpp'), str(root/'gpu/shim/bbport_mouse_look.cpp'),
                    '-o', str(tmp/'pad')], check=True, env=env)
    subprocess.run([str(tmp/'pad')], check=True, env=env)

    bridge = (root / 'gpu/shim/bbgpu.cpp').read_text()
    net = (root / 'src/runtime_net.c').read_text()
    bridge_source = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "gpu/bbgpu.h"
#include "gpu/shim/bbport_frame_rate.h"
static unsigned published;
namespace BbFrameRate {
bool CameraRedirectProofs(CameraRedirectProof*){return false;}
std::uint64_t CameraRedirectSequence(){return 0;}
}
namespace BbMouseLook {
struct Segment {uint64_t address,size,flags;};
void PatchImage(unsigned char*,uint64_t,const Segment*,uint64_t){++published;}
void PatchImageWithProof(unsigned char*,uint64_t,const Segment*,uint64_t,
                         const BbFrameRate::CameraRedirectProof*){++published;}
const char* Status(){return "TEST";}
}
''' + function(bridge, 'static bool MouseOverlapRefused()') + '\n' + function(bridge, 'extern "C" void bbgpu_patch_mouse_image(') + '\n' + \
        function(net, 'int runtime_net_enabled(') + r'''
int main(){
 BbGpuSegment seg{}; bbgpu_patch_mouse_image(nullptr,0,&seg,1);
 std::printf("%d %u\n", runtime_net_enabled(), published);
}
'''
    (tmp/'bridge.cpp').write_text(bridge_source)
    subprocess.run([cxx, *flags, str(tmp/'bridge.cpp'), '-o', str(tmp/'bridge')], check=True, env=env)
    def bridge_case(online, camera='', entry=''):
        e=dict(env, BB_NATIVE_MOUSE='1')
        for key,value in (('BB_ONLINE',online),('BB_RE_CAMERA',camera),('BB_RE_ENTRY',entry)):
            if value: e[key]=value
            else: e.pop(key,None)
        out=subprocess.check_output([str(tmp/'bridge')], env=e, text=True).strip().splitlines()[-1].split()
        return int(out[0]), int(out[1])
    for value in ('1','10','1foo'):
        enabled,published=bridge_case(value)
        assert enabled==1 and published==0, (value,enabled,published)
    assert bridge_case('0') == (0,1)
    assert bridge_case('0','10') == (0,0)
    assert bridge_case('0','0') == (0,1)
    assert bridge_case('0','','0x143ac60') == (0,0)
    print("PASS: actual bridge parses 1/10/1foo and camera/entry overlap like the runtime")

    window = (root / 'gpu/shim/window.cpp').read_text()
    start = window.index('} else if (event.type == SDL_EVENT_QUIT')
    end = window.index('} else if (event.type == SDL_EVENT_KEY_DOWN', start)
    close = window[start:end].replace('} else if (event.type == SDL_EVENT_QUIT',
                                      'if (event.type == SDL_EVENT_QUIT', 1)
    tail_start = window.index("    // A request from the game's side")
    tail_end = window.index('    pointer_shown = pointer;', tail_start)
    tail = window[tail_start:tail_end]
    window_source = r'''
#include <atomic>
#include <cstdint>
#include <cassert>
#include <cstdio>
using u64=uint64_t;
struct Event { unsigned type; };
enum { SDL_EVENT_QUIT=1, SDL_EVENT_WINDOW_CLOSE_REQUESTED=2, SDL_EVENT_KEY_DOWN=3 };
constexpr unsigned SDL_WINDOW_INPUT_FOCUS=1, SDL_WINDOW_FULLSCREEN=2;
namespace BbOverlay {bool MenuOpen(){return false;}}
namespace BbSettings {struct S{bool mouse_capture=true;};S&Get(){static S s;return s;}}
std::atomic<bool> is_open{true}, capture_requested{false}, mouse_captured{false}, mouse_look{true};
bool text_active=false,pointer=false,released_for_pointer=false,mouse_user_released=false,mouse_grabbed=false,pointer_shown=false;
int window=0; u64 last_mouse_motion_ms=0; unsigned captures=0, blocks=0, cancels=0;
struct HostInput {void Block(bool){++blocks;} void CancelMotion(){++cancels;}} host_input;
Event event{};
unsigned SDL_GetWindowFlags(int){return SDL_WINDOW_INPUT_FOCUS;}
u64 SDL_GetTicks(){return 1000;}
void SDL_GetWindowSize(int,int*w,int*h){*w=1920;*h=1080;}
void SDL_WarpMouseInWindow(int,float,float){}
void SDL_SetWindowMouseGrab(int,bool){}
void CaptureMouse(bool c){mouse_captured=c;if(c)++captures;}
void UpdateCursor(){}
''' + 'bool Close(){ event.type=SDL_EVENT_QUIT;\n' + close + '\n}\n return true; }\n' + \
        'bool Tail(){\n' + tail + '\n return true; }\n' + r'''
int main(){
 assert(!Close());
 assert(!is_open.load() && blocks && cancels && !mouse_captured && !capture_requested.load());
 capture_requested.store(true);
 assert(!Tail());
 assert(captures==0 && !mouse_captured.load());
 puts("PASS: actual close branch and post-close capture tail are terminal");
}
'''
    (tmp/'window.cpp').write_text(window_source)
    subprocess.run([cxx, *flags, str(tmp/'window.cpp'), '-o', str(tmp/'window')], check=True, env=env)
    subprocess.run([str(tmp/'window')], check=True, env=env)
