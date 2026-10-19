// SPDX-License-Identifier: GPL-2.0-or-later
/* Keyboard/mouse binding capture shared by the SDL assignment window and CPU tests. */
#ifndef BB_INPUT_CAPTURE_H
#define BB_INPUT_CAPTURE_H
#include <SDL3/SDL.h>
#include <stdio.h>

typedef struct { SDL_Keymod modifiers; SDL_Scancode pending; } BbInputCapture;

/* A comma separates alternatives in bbport.ini, so its key needs a delimiter-free name. */
static const char *bb_key_name(SDL_Scancode key) {
    if (key == SDL_SCANCODE_COMMA) return "Comma";
    if (key == SDL_SCANCODE_KP_COMMA) return "Keypad Comma";
    return SDL_GetScancodeName(key);
}

static SDL_Keymod bb_modifier(SDL_Scancode key) {
    switch (key) {
    case SDL_SCANCODE_LCTRL: return SDL_KMOD_LCTRL;
    case SDL_SCANCODE_RCTRL: return SDL_KMOD_RCTRL;
    case SDL_SCANCODE_LSHIFT: return SDL_KMOD_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return SDL_KMOD_RSHIFT;
    case SDL_SCANCODE_LALT: return SDL_KMOD_LALT;
    case SDL_SCANCODE_RALT: return SDL_KMOD_RALT;
    case SDL_SCANCODE_LGUI: return SDL_KMOD_LGUI;
    case SDL_SCANCODE_RGUI: return SDL_KMOD_RGUI;
    default: return SDL_KMOD_NONE;
    }
}

static int bb_capture_name(const char *key, SDL_Keymod mods, char *out, size_t size) {
    static const SDL_Scancode order[] = {
        SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_LALT, SDL_SCANCODE_RALT, SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI,
    };
    size_t used = 0;
    if (!key || !*key || !size) return 0;
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i) {
        if (!(mods & bb_modifier(order[i]))) continue;
        int n = snprintf(out + used, size - used, "%s+", SDL_GetScancodeName(order[i]));
        if (n < 0 || (size_t)n >= size - used) { out[0] = 0; return 0; }
        used += (size_t)n;
    }
    int n = snprintf(out + used, size - used, "%s", key);
    if (n < 0 || (size_t)n >= size - used) { out[0] = 0; return 0; }
    return 1;
}

/* 1: chosen, 0: wait, -1: cancel. A modifier-only chord commits on first release,
 * with its last pressed key as primary, regardless of the release order. */
static int bb_capture_event(BbInputCapture *state, const SDL_Event *event, char *out, size_t size) {
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        state->modifiers = SDL_KMOD_NONE;
        state->pending = SDL_SCANCODE_UNKNOWN;
        return -1; // Cancel: held modifiers cannot be reconstructed safely after refocusing.
    }
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) {
        const SDL_Scancode key = event->key.scancode;
        const SDL_Keymod mod = bb_modifier(key);
        if (event->type == SDL_EVENT_KEY_DOWN) {
            if (event->key.repeat) return 0;
            if (key == SDL_SCANCODE_ESCAPE) return -1;
            if (mod) { state->modifiers |= mod; state->pending = key; return 0; }
            return bb_capture_name(bb_key_name(key), state->modifiers, out, size);
        }
        if (mod) {
            const SDL_Keymod held = state->modifiers;
            state->modifiers &= ~mod;
            if ((held & mod) && state->pending != SDL_SCANCODE_UNKNOWN) {
                const SDL_Scancode primary = state->pending;
                state->pending = SDL_SCANCODE_UNKNOWN;
                return bb_capture_name(bb_key_name(primary), held & ~bb_modifier(primary), out, size);
            }
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        static const char *const names[] = {"", "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2"};
        unsigned button = event->button.button;
        if (button >= SDL_BUTTON_LEFT && button <= SDL_BUTTON_X2)
            return bb_capture_name(names[button], state->modifiers, out, size);
    }
    return 0;
}
#endif
