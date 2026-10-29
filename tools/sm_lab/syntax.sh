#!/usr/bin/env bash
# Syntax-only compile of the SM-touched translation units with the build's own flags.
set -uo pipefail
cd /home/arinp/Code/nr-home/build/sm-present/gpu || exit 1
rc=0
for f in render/presenter.cpp vulkan/swapchain.cpp; do
    cmd=$(ninja -t commands bbgpu | grep "$f.o" | head -1)
    cmd=$(printf '%s' "$cmd" | sed -E 's# -o [^ ]+\.o##')
    if eval "$cmd -fsyntax-only"; then echo "SYNTAX_OK $f"; else echo "SYNTAX_FAIL $f"; rc=1; fi
done
exit $rc
