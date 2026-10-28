#!/usr/bin/env bash
# SM lab session: run the native port with the NVIDIA present layer, close the
# window normally, keep the log and exit code.
#
# usage: play.sh <run-name> <seconds-after-arm> [KEY=VALUE ...]
#   NR=1                 start the NR pipeline (Helper + scene arm tokens)
#   SAVEDATA_SRC=<dir>   private save copy to load
#   PRESS_AT=<sec>       press PRESS_BUTTONS (default cross) after the window appears
#   BB_GPU_LOCK_HELD=1   a batch already holds the GPU lock
#   SM=0                 run the same scene without the driver layer (A/B)
set -euo pipefail
LAB=/home/arinp/Code/bloodborne_pc/.worktree/sm/out/sm-lab
WT=/home/arinp/Code/bloodborne_pc/.worktree/sm
BUILD=/home/arinp/Code/nr-home/build/sm-present
GAME=/home/arinp/bloodborne/CUSA03173
GE=/home/arinp/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64/files
NRHOME=$LAB/nrhome
name=$1
seconds=$2
shift 2
for kv in "$@"; do export "$kv"; done

run=$NRHOME/runs/$name
rm -rf "$run"
mkdir -p "$run/profile/user" "$run/profile/logs" "$run/profile/nv-cache" \
         "$run/profile/xdg/config" "$run/profile/xdg/cache" "$run/profile/xdg/data" \
         "$run/profile/xdg/state"

if [[ ${BB_GPU_LOCK_HELD:-0} != 1 ]]; then
    exec 9>/tmp/nrbridge-poc-gpu.lock
    if ! flock -n 9; then echo "GPU_LOCK_BUSY $name"; exit 9; fi
fi

if [[ -n ${SAVEDATA_SRC:-} ]]; then
    mkdir -p "$run/profile/user/savedata"
    cp -a "$SAVEDATA_SRC/." "$run/profile/user/savedata/"
    # The private copy keeps another title id's folder; the mounted title is CUSA03173.
    for t in "$run/profile/user/savedata"/*/CUSA*; do
        [[ -d $t && $(basename "$t") != "${SAVE_TITLE:-CUSA03173}" ]] &&
            cp -a "$t" "$(dirname "$t")/${SAVE_TITLE:-CUSA03173}"
    done
    echo "SAVEDATA_COPIED $(du -sh "$run/profile/user/savedata" | cut -f1)"
fi

cat > "$run/profile/bbport.ini" <<'EOF'
upscaler=dlss
preset=1
aspect_ratio=16:9
output_res=1920x1080
jitter=1
object_motion=1
sharpen=0
show_fps=1
EOF

export NRBRIDGE_HOME="$NRHOME"
export BB_PREBUILT=1
export BB_PROBE="$BUILD/bb-probe"
export BB_GAME_DIR="$GAME"
export BB_DATA_DIR="$run/profile"
export BB_CONFIG="$run/profile/bbport.ini"
export BB_USER_DIR="$run/profile/user"
export BB_GPU_USER_DIR="$run/profile/user"
export BB_MODS_ENABLED=0
export BB_SAVE_LOG=0
export BB_STARTUP_CHECK=0
export BB_FULLSCREEN=0
export BB_SKIP_NETWORK_CHOICE=1
export BB_FRAME_GEN=0
export BB_FRAME_GEN_BACKEND=smooth-motion
export BB_FRAME_GEN_AUDIT=1
export BB_FPS=uncap
export BB_FPS_LIMIT=60
export BB_PRESENT_MODE=Immediate
export BB_FRAME_STATS=0
export BB_PAD_FILE="$run/pad.txt"
export XDG_CONFIG_HOME="$run/profile/xdg/config"
export XDG_CACHE_HOME="$run/profile/xdg/cache"
export XDG_DATA_HOME="$run/profile/xdg/data"
export XDG_STATE_HOME="$run/profile/xdg/state"
export __GL_SHADER_DISK_CACHE_PATH="$run/profile/nv-cache"
export LD_LIBRARY_PATH="$BUILD/gpu:$BUILD:/home/arinp/Code/bloodborne_pc/out/deps/root/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ ${SM:-1} == 1 ]]; then
    export NVPRESENT_ENABLE_SMOOTH_MOTION=1
else
    unset NVPRESENT_ENABLE_SMOOTH_MOTION
    export BB_FRAME_GEN_BACKEND=off
fi
export NVPRESENT_LOG_LEVEL=4
export NVPRESENT_LOG_FILE="$run/nvpresent.log"
export VK_LOADER_LAYERS_ALLOW=VK_LAYER_NV_present
export VK_LOADER_DEBUG=layer

if [[ ${NR:-0} == 1 ]]; then
    cp /home/arinp/Code/bloodborne_pc/.worktree/nrbridge/out/nr-dev/runs/m3-sm-r62/helper-runtime.json "$run/helper-runtime.json"
    export BB_NR_DAILY=1
    export BB_NR_NATIVE_ASYNC=0
    export BB_NR_NATIVE_ASYNC_PRE_SR=1
    export BB_NR_NATIVE_ASYNC_SCENE_FIRST=1
    export BB_NR_NATIVE_ASYNC_PRE_SR_PLAY=1
    export BB_NR_NATIVE_ASYNC_PRE_SR_SKY=off
    export BB_NATIVE_PERSISTENT_HISTORY=1
    export BB_NR_NATIVE_ASYNC_PRE_SR_SR_HISTORY=game_only
    export BB_NR_NATIVE_ASYNC_PRE_SR_SR_INPUT=switching
    export BB_NR_NATIVE_ASYNC_SESSION="$name"
    export BB_NR_NATIVE_ASYNC_MODE=resident
    export BB_NR_NATIVE_ASYNC_RUN_DIR="$run"
    export BB_NR_NATIVE_ASYNC_SCENE_ARM="$run/native-scene-arm"
    export BB_NR_NATIVE_ASYNC_SOURCE_LIMIT=0
    export BB_NR_NATIVE_ORIGIN_RUN_DIR="$run"
    export BB_NR_NATIVE_ORIGIN_HELPER=/home/arinp/Code/bloodborne_pc/.worktree/nrbridge/out/nr-dev/build/helper-runtime-model/16bdc730da6029bd/wine_runtime_model.exe
    export BB_NR_NATIVE_ORIGIN_WINE="$GE/lib/wine/x86_64-unix/wine"
    export BB_NR_NATIVE_ORIGIN_PRELOADER="$GE/lib/wine/x86_64-unix/wine-preloader"
    export BB_NR_NATIVE_ORIGIN_GE_FILES="$GE"
    export BB_NR_NATIVE_ORIGIN_PREFIX="$run/profile/wine-prefix"
    export BB_NR_NATIVE_ORIGIN_WINEDEBUG=-all
    export BB_NR_NATIVE_ORIGIN_MODE=resident
    export BB_NR_NATIVE_ORIGIN_NGX_DIR=/usr/lib/nvidia/wine
    export BB_NR_NATIVE_ORIGIN_RUNTIME_CONFIG="$run/helper-runtime.json"
    export BB_NR_RUNTIME_MODEL=1
    : > "$run/pad.txt"
fi

cd "$WT"
setsid bash run.sh > "$run/game.log" 2>&1 &
pid=$!
echo "$pid" > "$run/pid"
echo "STARTED run=$run pid=$pid"

win=
for _ in $(seq 1 120); do
    if ! kill -0 "$pid" 2>/dev/null; then echo "PROCESS_EXITED_EARLY"; break; fi
    win=$(niri msg --json windows 2>/dev/null | python3 -c 'import json,sys
try: w=json.load(sys.stdin)
except Exception: w=[]
ids=[x["id"] for x in w if x.get("app_id")=="bb-probe"]
print(ids[0] if ids else "")' 2>/dev/null || true)
    [[ -n $win ]] && break
    sleep 1
done
echo "WINDOW=$win"
if [[ -n ${PRESS_AT:-} && -n $win ]]; then
    sleep "$PRESS_AT"
    printf '%s' "${PRESS_BUTTONS:-cross}" > "$run/pad.txt"
    sleep 0.4
    : > "$run/pad.txt"
    echo "PAD_PRESSED ${PRESS_BUTTONS:-cross}"
    if [[ -n ${PRESS_AGAIN:-} ]]; then
        sleep "$PRESS_AGAIN"
        printf '%s' "${PRESS_BUTTONS:-cross}" > "$run/pad.txt"
        sleep 0.4
        : > "$run/pad.txt"
        echo "PAD_PRESSED_AGAIN ${PRESS_BUTTONS:-cross}"
    fi
fi
if [[ ${NR:-0} == 1 && -n $win ]]; then
    sleep "${NR_SETTLE:-45}"
    printf '%s\n' "$name" > "$run/native-helper-start"
    echo "HELPER_START_TOKEN"
    sleep "${NR_HELPER_WAIT:-15}"
    printf '%s\n' "$name" > "$run/native-scene-arm"
    echo "SCENE_ARM_TOKEN"
    sleep "${NR_ARM_SETTLE:-15}"
fi
if [[ ${RESIZE:-0} == 1 && -n $win ]]; then
    niri msg action focus-window --id "$win" >/dev/null 2>&1 || true
    sleep 1
    niri msg action set-window-width 1280 >/dev/null 2>&1 || true
    niri msg action set-window-height 720 >/dev/null 2>&1 || true
    echo "RESIZE small"
    sleep "${RESIZE_HOLD:-4}"
    grim "$run/resized-small.png" 2>/dev/null || true
    niri msg action set-window-width 1920 >/dev/null 2>&1 || true
    niri msg action set-window-height 1080 >/dev/null 2>&1 || true
    echo "RESIZE large"
    sleep "${RESIZE_HOLD:-4}"
fi
sleep "$seconds"
grim "$run/before-close.png" 2>/dev/null && echo "SHOT=$run/before-close.png"
if [[ -n $win ]]; then
    niri msg action close-window --id "$win" || true
    echo "CLOSE_REQUESTED $(date +%s.%N)"
fi
status=0
for _ in $(seq 1 90); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
done
if kill -0 "$pid" 2>/dev/null; then
    echo "STILL_RUNNING_AFTER_90S"
    kill -TERM "$pid" 2>/dev/null || true
    sleep 5
    kill -KILL "$pid" 2>/dev/null || true
    echo "EXIT=KILLED"
else
    wait "$pid" || status=$?
    echo "EXIT=$status"
fi
echo "=== tail game.log ==="
tail -30 "$run/game.log"
echo "=== tail nvpresent.log ==="
tail -15 "$run/nvpresent.log" 2>/dev/null || true
echo "=== gpu after ==="
nvidia-smi --query-gpu=name,temperature.gpu,memory.used --format=csv,noheader 2>&1 | head -3
