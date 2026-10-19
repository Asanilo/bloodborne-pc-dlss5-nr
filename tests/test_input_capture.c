// SPDX-License-Identifier: GPL-2.0-or-later
#include "../tools/input_capture.h"
#include <assert.h>
#include <string.h>

static int key(BbInputCapture *state, SDL_Scancode scan, int down, char *out) {
    SDL_Event event = {0};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scan;
    event.key.repeat = false;
    return bb_capture_event(state, &event, out, 256);
}
int main(void) {
    BbInputCapture capture = {0}; char name[256];
    assert(!key(&capture, SDL_SCANCODE_LCTRL, 1, name));
    assert(!key(&capture, SDL_SCANCODE_LSHIFT, 1, name));
    assert(key(&capture, SDL_SCANCODE_C, 1, name) == 1);
    assert(!strcmp(name, "Left Ctrl+Left Shift+C"));
    capture = (BbInputCapture){0};
    assert(!key(&capture, SDL_SCANCODE_RALT, 1, name));
    SDL_Event mouse = {0}; mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN; mouse.button.button = SDL_BUTTON_LEFT;
    assert(bb_capture_event(&capture, &mouse, name, sizeof(name)) == 1);
    assert(!strcmp(name, "Right Alt+Mouse Left"));
    capture = (BbInputCapture){0};
    assert(!key(&capture, SDL_SCANCODE_LCTRL, 1, name));
    assert(key(&capture, SDL_SCANCODE_LCTRL, 0, name) == 1 && !strcmp(name, "Left Ctrl"));
    assert(!key(&capture, SDL_SCANCODE_LCTRL, 1, name));
    SDL_Event focus = {0}; focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    assert(bb_capture_event(&capture, &focus, name, sizeof(name)) == -1);
    assert(!capture.modifiers && capture.pending == SDL_SCANCODE_UNKNOWN);
    capture = (BbInputCapture){0}; // New assignment after cancellation.
    assert(key(&capture, SDL_SCANCODE_C, 1, name) == 1 && !strcmp(name, "C"));
    // A modifier-only chord must not silently become plain Shift for one release order.
    for (int release_ctrl = 0; release_ctrl < 2; ++release_ctrl) {
        capture = (BbInputCapture){0};
        assert(!key(&capture, SDL_SCANCODE_LCTRL, 1, name));
        assert(!key(&capture, SDL_SCANCODE_LSHIFT, 1, name));
        assert(key(&capture, release_ctrl ? SDL_SCANCODE_LCTRL : SDL_SCANCODE_LSHIFT, 0, name) == 1);
        assert(!strcmp(name, "Left Ctrl+Left Shift"));
    }
    capture = (BbInputCapture){0};
    assert(key(&capture, SDL_SCANCODE_COMMA, 1, name) == 1 && !strcmp(name, "Comma"));
    assert(key(&capture, SDL_SCANCODE_KP_COMMA, 1, name) == 1 && !strcmp(name, "Keypad Comma"));
    capture = (BbInputCapture){0};
    assert(key(&capture, SDL_SCANCODE_KP_PLUS, 1, name) == 1 && !strcmp(name, "Keypad +"));
    assert(key(&capture, SDL_SCANCODE_ESCAPE, 1, name) == -1);
    assert(key(&capture, SDL_SCANCODE_UNKNOWN, 1, name) == 0);
    SDL_Event repeat = {0}; repeat.type = SDL_EVENT_KEY_DOWN; repeat.key.scancode = SDL_SCANCODE_C; repeat.key.repeat = true;
    assert(!bb_capture_event(&capture, &repeat, name, sizeof(name)));
    mouse.button.button = 255;
    assert(!bb_capture_event(&capture, &mouse, name, sizeof(name)));
    char small[4];
    assert(!bb_capture_name("Mouse Left", SDL_KMOD_LCTRL, small, sizeof(small)) && !small[0]);
    puts("PASS: modifier-only, keyboard/mouse chords, keypad plus, cancel and bounded capture");
}
