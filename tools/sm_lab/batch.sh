#!/usr/bin/env bash
# Repeat SM exit sessions under one GPU lock and summarize each result.
# usage: batch.sh <count> <seconds> [KEY=VALUE ...]
set -euo pipefail
LAB=/home/arinp/Code/bloodborne_pc/.worktree/sm/out/sm-lab
count=$1; seconds=$2; shift 2
exec 9>/tmp/nrbridge-poc-gpu.lock
if ! flock -n 9; then echo "GPU_LOCK_BUSY"; exit 9; fi
export BB_GPU_LOCK_HELD=1
stamp=$(date +%H%M%S)
for i in $(seq 1 "$count"); do
    echo "===== SESSION $i/$count $(date +%H:%M:%S) ====="
    bash "$LAB/play.sh" "sm-r${stamp}-${i}" "$seconds" "$@" 2>&1 | grep -E "STARTED|WINDOW=|SAVEDATA|PAD_PRESSED|HELPER_START|SCENE_ARM|CLOSE_REQUESTED|EXIT=|SHOT=|SM_EXIT|FG_PRESENT_RETIRED|FG_EXIT|HOLD|DeviceLost|GPU_LOCK"
    python3 "$LAB/summarize.py" "$LAB/nrhome/runs/sm-r${stamp}-${i}" || true
done
echo "===== BATCH DONE ====="
