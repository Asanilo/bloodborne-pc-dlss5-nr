#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-level guard for the one native-mouse writer and its loader ordering.

The old f7f7ac2 fixture asserted that BB_NATIVE_MOUSE=1 could never install. That contract is
obsolete: the real PatchImage/default-off behavior is covered by test_native_mouse_stub.cpp and
test_native_mouse_input.cpp. This guard only pins the integration invariants those tests cannot
see without linking the whole GPU library.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
hooks = (root / 'gpu/shim/bbport_guest_hooks.cpp').read_text()
header = (root / 'gpu/shim/bbport_guest_hooks.h').read_text()
probe = (root / 'src/probe.c').read_text()
glue = (root / 'gpu/shim/bbgpu.cpp').read_text()
pad = (root / 'src/runtime_pad.c').read_text()
window = (root / 'gpu/shim/window.cpp').read_text()

for old in ('InstallLookHook', 'CameraLookAdd', 'CameraLookAvailable', 'NativeMouseHookValidated'):
    assert old not in hooks, old
    assert old not in header, old
assert 'bbgpu_camera_look' not in pad
assert 'bbgpu_route_mouse' in pad and 'bbgpu_cancel_mouse_input' in pad

# Install after all existing image hooks, before final protection/initializers.
image_patch = probe.index('bbgpu_patch_image(image, size);')
mouse_patch = probe.index('bbgpu_patch_mouse_image(image, size, segments, ns);')
protect_image = probe.index('protect(image, round_page(size), 0);')
assert image_patch < mouse_patch < protect_image

# Known overlapping writers fail closed before the candidate publishes anything.
for marker in ('BB_ONLINE', 'BB_BBSC', 'BB_RE_CAMERA', 'BB_RE_ENTRY',
               'runtime_net_native_mouse_compatible'):
    assert marker in glue, marker
assert glue.index('NATIVE_MOUSE status=refused') < glue.index('BbMouseLook::PatchImage')
assert probe.index('runtime_net_image_mapped(image, size);') < probe.index('bbgpu_prepare_mouse_image(')

# A pointer/menu release happens before motion dispatch; lifecycle events precede module filters.
assert window.index('if (pointer && mouse_captured') < window.index('if (event.type == SDL_EVENT_MOUSE_MOTION)')
assert window.index('SDL_EVENT_WINDOW_FOCUS_LOST') < window.index('FilteredByModule(&event, window)')
assert window.index('SDL_EVENT_WINDOW_CLOSE_REQUESTED') < window.index('FilteredByModule(&event, window)')
assert 'host_input.CancelMotion()' in window

print('PASS: one camera writer, loader order, fail-closed overlap, and pre-filter lifecycle gates')
