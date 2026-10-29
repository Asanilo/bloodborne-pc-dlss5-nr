#!/usr/bin/env bash
# Build the wlr-screencopy frame counter used for display-side SM evidence.
set -euo pipefail
WT=/home/arinp/Code/bloodborne_pc/.worktree/sm
OUT=$WT/out/sm-lab/frame
XML=${WLR_SCREENCOPY_XML:-/tmp/wlr-screencopy.xml}
mkdir -p "$OUT"
if [[ ! -s $XML ]]; then
    curl -sL --max-time 30 \
        https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/raw/master/unstable/wlr-screencopy-unstable-v1.xml \
        -o "$XML"
fi
wayland-scanner client-header "$XML" "$OUT/wlr-screencopy-client.h"
wayland-scanner private-code "$XML" "$OUT/wlr-screencopy-protocol.c"
gcc -O2 -Wall -Wextra -o "$OUT/frame-count" \
    "$WT/tools/sm_lab/frame_count.c" "$OUT/wlr-screencopy-protocol.c" \
    -I"$OUT" $(pkg-config --cflags --libs wayland-client)
echo "BUILT $OUT/frame-count"
