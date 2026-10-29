#!/usr/bin/env bash
# Display-side A/B for the driver layer: identical scene, save, fps cap and
# present mode, only NVPRESENT_ENABLE_SMOOTH_MOTION differs.
#
# Both rounds run through the lab launcher (no official runner), capture the
# composited output for FRAME_SECONDS while in game, and print the content
# change rate. Interpretation: the compositor content-change rate should
# roughly double with the layer on; this is not scanout/photon timing.
#
# usage: display_ab.sh [in-game seconds] [run-name]
set -euo pipefail
LAB=/home/arinp/Code/bloodborne_pc/.worktree/sm/out/sm-lab
WT=/home/arinp/Code/bloodborne_pc/.worktree/sm
SAVE=/home/arinp/Code/bloodborne_pc/.worktree/nrbridge/out/nr-dev/runs/m3-sm-r62/profile/user/savedata
seconds=${1:-45}
stamp=${2:-$(date +%H%M%S)}
exec 9>/tmp/nrbridge-poc-gpu.lock
if ! flock -n 9; then echo "GPU_LOCK_BUSY"; exit 9; fi
export BB_GPU_LOCK_HELD=1

run_round() {
    local sm=$1 name=disp-${stamp}-sm${sm}
    echo "===== ROUND SM=$sm ($name) ====="
    SM=$sm FRAME_CAPTURE=1 FRAME_SECONDS=4 \
        bash "$WT/out/sm-lab/play.sh" "$name" "$seconds" \
        SAVEDATA_SRC="$SAVE" PRESS_AT=25 PRESS_AGAIN=6 BB_PC_MODEL=0 \
        2>&1 | grep -E "STARTED|WINDOW=|SAVEDATA|PAD_|CLOSE_REQUESTED|EXIT=|SM_EXIT|HOLD|DeviceLost|captures|content_changes|content_rate"
    python3 - "$LAB/nrhome/runs/$name" <<'PY' || true
import json, sys
from pathlib import Path
run = Path(sys.argv[1])
summary = run / 'display-frames.summary'
print('DISPLAY', summary.read_text().strip() if summary.exists() else 'MISSING')
log = run / 'game.log'
if log.exists():
    text = log.read_text(errors='replace')
    print('MARKERS', [l.split(' ', 1)[0] for l in text.splitlines()
                      if l.startswith(('SM_EXIT_', 'FG_EXIT_', 'FG_PRESENT_RETIRED'))])
PY
}

run_round 1
run_round 0
echo "===== DISPLAY_AB_DONE ====="
