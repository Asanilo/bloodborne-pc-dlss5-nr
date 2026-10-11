#!/usr/bin/env bash
# Opt-in continuous C3 resident|skipEvaluate game candidate. Integration owns GPU lock/watchdog.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
POC_ROOT=${BB_GAME_POC_ROOT:-/home/arinp/Code/nrbridge-design/game-poc-20261011}
SESSION=${1:-}
MODE=${2:-}
FRAME_COUNT=${3:-}
DIAGNOSTIC_CASE=${4:-}
[[ $SESSION =~ ^[A-Za-z0-9][A-Za-z0-9-]{0,62}$ && ( $MODE == resident || $MODE == skipEvaluate ) ]] || {
    echo 'Usage: run_game_c3.sh SESSION resident|skipEvaluate' >&2; exit 2;
}
[[ ( $MODE == resident && $FRAME_COUNT == 4 && $DIAGNOSTIC_CASE == smoke ) ||
   ( $MODE == resident && $FRAME_COUNT == 120 && ( $DIAGNOSTIC_CASE == static || $DIAGNOSTIC_CASE == motion ) ) ||
   ( $MODE == skipEvaluate && $FRAME_COUNT == 4 && $DIAGNOSTIC_CASE == skipEvaluate ) ]] || {
    echo 'C3_LAUNCH BLOCKED mode/frame_count/diagnostic_case mismatch' >&2; exit 3;
}
GAME=${BB_GAME_DIR:-/home/arinp/bloodborne/CUSA03173}
RUN="$POC_ROOT/bbport/runs/c3-game-$SESSION"
PROFILE="$RUN/profile"
GE=/home/arinp/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64
WINE="$GE/files/lib/wine/x86_64-unix/wine"
PRELOADER="$GE/files/lib/wine/x86_64-unix/wine-preloader"
HELPER="$POC_ROOT/opti-worktree/poc/game_nr/c3/out/wine_game_nr_resident.exe"
DLLPATH="$GE/files/lib/vkd3d:$GE/files/lib/wine"
TRIGGER="$RUN/c3-trigger"

[[ -f "$WT/out/bb-probe" && -f "$WT/out/gpu/libbbgpu.so" && -f "$WT/out/libbbport_dlss.so" ]] || {
    echo "C3_LAUNCH BLOCKED missing C3 worktree build under $WT/out" >&2; exit 3;
}
[[ -f "$HELPER" ]] || { echo "C3_LAUNCH BLOCKED missing reviewed helper $HELPER" >&2; exit 3; }
[[ -f "$GAME/eboot.bin" ]] || { echo "C3_LAUNCH BLOCKED missing read-only game asset $GAME/eboot.bin" >&2; exit 3; }
[[ -f "$PROFILE/.c3-profile" && $(cat "$PROFILE/.c3-profile") == "$SESSION" ]] || {
    echo "C3_LAUNCH BLOCKED private C3 seed/profile marker missing or mismatched; use integration/prepare_seed.py c3 $SESSION" >&2
    exit 3
}
[[ ! -e "$TRIGGER" ]] || { echo "C3_LAUNCH BLOCKED refusing existing one-shot trigger: $TRIGGER" >&2; exit 3; }
mkdir -p -m 700 "$RUN" "$PROFILE/logs" "$RUN/dump" "$PROFILE/wine-prefix"
chmod 700 "$RUN" "$PROFILE" "$PROFILE/logs" "$RUN/dump" "$PROFILE/wine-prefix"
cat > "$PROFILE/bbport.ini" <<'CONFIG'
upscaler=dlss
preset=0
output_res=1920x1080
jitter=0
object_motion=1
sharpen=0
CONFIG
chmod 600 "$PROFILE/bbport.ini"

INVOCATION=$(date -u +%Y%m%dT%H%M%SZ)
ENV_LOG="$RUN/launch-$INVOCATION.env"
{
    printf 'session=%s\nmode=%s\nprofile_id=hdr-run-dlaa-1to1-j0-manual-wp1-v1\n' "$SESSION" "$MODE"
    printf 'worktree=%s\nhead=%s\ngame_asset_readonly=%s\n' "$WT" "$(git -C "$WT" rev-parse HEAD)" "$GAME"
    printf 'profile=%s\nprivate_save=%s\nprivate_wineprefix=%s\n' "$PROFILE" "$PROFILE/user/savedata" "$PROFILE/wine-prefix"
    printf 'helper=%s\nhelper_sha256=%s\n' "$HELPER" "$(sha256sum "$HELPER" | awk '{print $1}')"
    printf 'wine=%s\npreloader=%s\nWINELOADERNOEXEC=1\n' "$WINE" "$PRELOADER"
    printf 'WINEDLLOVERRIDES=mscoree,mshtml=;nvapi64=n;dxgi=n\nWINEDLLPATH=%s\n' "$DLLPATH"
    printf 'WINEDEBUG=-all,warn+vulkan,err+vulkan\nDXVK_ENABLE_NVAPI=1\nDXVK_NVAPI_LOG_LEVEL=trace\nNVIDIA_WINE_DLL_DIR=/usr/lib64/nvidia/wine\n'
    printf 'BB_JITTER=0\nBB_NR_C3_MODE=%s\nBB_NR_C3_TRIGGER=%s\ntrigger_initial_state=absent\n' "$MODE" "$TRIGGER"
    printf 'frame_count=%s\ndiagnostic_case=%s\nBB_FPS=uncap\nBB_FPS_LIMIT=90\n' "$FRAME_COUNT" "$DIAGNOSTIC_CASE"
    printf 'exposure_valid=false\nmanual_white_point=1\n' 
    printf 'gpu_test=owned by integration/run_continuous_nr.py; launcher itself does not acquire GPU lock\n'
} > "$ENV_LOG"
chmod 600 "$ENV_LOG"

export BB_GAME_DIR="$GAME" BB_DATA_DIR="$PROFILE" BB_CONFIG="$PROFILE/bbport.ini"
export BB_USER_DIR="$PROFILE/user" BB_GPU_USER_DIR="$PROFILE/user"
export BB_PREBUILT=1 BB_PROBE="$WT/out/bb-probe" BB_UPSCALER=dlss BB_UPSCALE_PRESET=0
export BB_DLSS=1 BB_DLSS_AUDIT=1 BB_FPS=uncap BB_FPS_LIMIT=90 BB_FRAME_STATS=1 BB_FRAME_LOG="$PROFILE/logs/frames.csv"
export BB_SAVE_LOG=1 BB_MODS_ENABLED=0 BB_IME_TEXT=NRTest
export BB_PAD_FILE="$RUN/pad.txt" BB_DUMP_DIR="$RUN/dump"
: > "$BB_PAD_FILE"
chmod 600 "$BB_PAD_FILE"
unset BB_NR_C1_MODE BB_NR_C2_MODE
export BB_NR_C3_FRAME_COUNT="$FRAME_COUNT" BB_NR_C3_DIAGNOSTIC_CASE="$DIAGNOSTIC_CASE"
export BB_NR_C3_MODE="$MODE" BB_NR_C3_SESSION="$SESSION" BB_NR_C3_RUN_DIR="$RUN"
export BB_NR_C3_RUN_DIR_WINDOWS="Z:$RUN" BB_NR_C3_PRELOADER="$PRELOADER" BB_NR_C3_WINE="$WINE"
export BB_NR_C3_HELPER_EXE="$HELPER" BB_NR_C3_WINEPREFIX="$PROFILE/wine-prefix"
export BB_NR_C3_WINEDLLOVERRIDES='mscoree,mshtml=;nvapi64=n;dxgi=n'
export BB_NR_C3_WINEDLLPATH="$DLLPATH" BB_NR_C3_WINEDEBUG='-all,warn+vulkan,err+vulkan'
export BB_NR_C3_TRIGGER="$TRIGGER"
export WINEDLLOVERRIDES="$BB_NR_C3_WINEDLLOVERRIDES" WINEARCH=win64
export WINEDLLPATH="$DLLPATH" WINEDEBUG="$BB_NR_C3_WINEDEBUG"
export NVIDIA_WINE_DLL_DIR=/usr/lib64/nvidia/wine DXVK_NVAPI_LOG_LEVEL=trace DXVK_ENABLE_NVAPI=1
export BB_JITTER=0 BB_PRESENT_DUMP_TRIGGER="$RUN/present-dump-trigger" BB_PRESENT_DUMP_COUNT=1
export DLSSNR_DISABLE=1 DLSSNR_AUTO_DOWNLOAD=0
unset BB_RENDER_RES BB_OUTPUT_RES BB_AUTO_RENDER_RES BB_PRESET_FILE BB_TOGGLE_FILE BB_TIMEOUT BB_AUDIT_SAVE_BACKUP
printf 'C3_LAUNCH_READY session=%s mode=%s profile=%s helper=%s env=%s\n' "$SESSION" "$MODE" "$PROFILE" "$HELPER" "$ENV_LOG"
printf 'C3 one-shot trigger initially absent: %s\n' "$TRIGGER"
printf 'Root: after Clinic is visible and C3 reports profile/setup readiness, create this path once to start the requested sequence of actual DLSS frames.\n'
exec bash "$WT/run.sh"
