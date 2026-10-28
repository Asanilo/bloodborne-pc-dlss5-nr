#!/usr/bin/env bash
# Official daily runner acceptance for the Smooth Motion backend.
#
# Runs tools/nr_dev/play.py --daily --smooth-motion against the private
# NRBRIDGE_HOME prepared by official_inputs.py, drives the window through the
# menu into the save, arms NR, screenshots, closes normally and reports the
# runner's own result.
#
# usage: [SM_MODE=on|off] official_play.sh <session> [seconds-in-game] [extra play.py args...]
set -euo pipefail
WT=/home/arinp/Code/bloodborne_pc/.worktree/sm
HOME_NR=/home/arinp/Code/nr-home/build/sm-official
BUILD=$HOME_NR/build/native
session=${1:-sm-official-$(date +%H%M%S)}
seconds=${2:-60}
shift 2 || true
run=$HOME_NR/runs/$session

export NRBRIDGE_HOME=$HOME_NR
mkdir -p "$HOME_NR/runs"
if [[ -e $run ]]; then echo "SESSION_EXISTS $run"; exit 2; fi

cd "$WT"
if [[ ${SM_MODE:-on} == off ]]; then
    backend=(--fg-backend off)
else
    backend=(--smooth-motion)
fi
setsid python3 tools/nr_dev/play.py --daily "${backend[@]}" \
    --local-inputs "$HOME_NR/localinputs/sm-B-v1.json" \
    --native-build "$BUILD" --session "$session" "$@" \
    > "$HOME_NR/runs/$session.launcher.log" 2>&1 &
pid=$!
echo "LAUNCHER=$pid SESSION=$session RUN=$run"

win=
for _ in $(seq 1 150); do
    if ! kill -0 "$pid" 2>/dev/null; then echo "LAUNCHER_EXITED_EARLY"; break; fi
    win=$(niri msg --json windows 2>/dev/null | python3 -c 'import json,sys
try: w=json.load(sys.stdin)
except Exception: w=[]
ids=[x["id"] for x in w if x.get("app_id")=="bb-probe"]
print(ids[0] if ids else "")' 2>/dev/null || true)
    [[ -n $win ]] && break
    sleep 1
done
echo "WINDOW=$win"
if [[ -z $win ]]; then
    tail -30 "$HOME_NR/runs/$session.launcher.log" || true
    exit 3
fi

press() {
    printf '%s' "${1:-cross}" > "$run/pad.txt" 2>/dev/null || true
    sleep 0.4
    : > "$run/pad.txt" 2>/dev/null || true
}
# Title screen -> main menu -> Continue (the save copy mounted read-only).
sleep 25; press cross
sleep 6; press cross
echo "PAD_MENU $(date +%H:%M:%S)"
sleep 40
printf '%s\n' "$session" > "$run/native-helper-start" 2>/dev/null || true
echo "HELPER_START_TOKEN"
sleep 30
printf '%s\n' "$session" > "$run/native-scene-arm" 2>/dev/null || true
echo "SCENE_ARM_TOKEN"
if [[ ${FRAME_CAPTURE:-0} == 1 && -x $WT/out/sm-lab/frame/frame-count ]]; then
    "$WT/out/sm-lab/frame/frame-count" "${FRAME_SECONDS:-4}" > "$run/display-frames.txt" 2>/dev/null || true
    python3 "$WT/tools/sm_lab/frame_report.py" "$run/display-frames.txt" | tee "$run/display-frames.summary" || true
fi
sleep "$seconds"
grim "$run/before-close.png" 2>/dev/null && echo "SHOT=$run/before-close.png"
niri msg action close-window --id "$win" >/dev/null 2>&1 || true
echo "CLOSE_REQUESTED $(date +%H:%M:%S)"

for _ in $(seq 1 120); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
done
if kill -0 "$pid" 2>/dev/null; then echo "RUNNER_STILL_ALIVE"; exit 4; fi
wait "$pid" || true

echo "=== launcher tail ==="
tail -25 "$HOME_NR/runs/$session.launcher.log" || true
echo "=== result ==="
python3 - "$run" <<'PY'
import json, sys
from pathlib import Path
run = Path(sys.argv[1])
for name in ('result.json', 'retirement-audit.json', 'present-metrics.json'):
    path = run / name
    if path.exists():
        print(name, json.dumps(json.loads(path.read_text()), ensure_ascii=False)[:600])
log = run / 'game.log'
if log.exists():
    text = log.read_text(errors='replace')
    print('markers', [line.split(' ', 1)[0] for line in text.splitlines()
                      if line.startswith(('SM_EXIT_', 'FG_EXIT_', 'NATIVE_ASYNC_PRE_SR_EXIT_PROOF'))])
    print('device_lost', 'DeviceLost' in text or 'DEVICE_LOST' in text)
PY
