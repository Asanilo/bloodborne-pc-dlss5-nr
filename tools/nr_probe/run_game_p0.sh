#!/usr/bin/env bash
# Managed experimental P0 Quality; Integration owns GPU lock/watchdog and approvals.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
POC_ROOT=${BB_GAME_POC_ROOT:-/home/arinp/Code/nrbridge-design/game-poc-20261011}
SESSION=${1:-}
MODE=${2:-}
FRAME_COUNT=${3:-}
DIAGNOSTIC_CASE=${4:-}
EXECUTION_MODE=${5:-}
[[ $SESSION =~ ^[A-Za-z0-9][A-Za-z0-9-]{0,62}$ && ( $EXECUTION_MODE == diagnostic || $EXECUTION_MODE == hot ) ]] || {
    echo 'Usage: run_game_p0.sh SESSION resident|skipEvaluate|srOnly 4|120|1800|18000 CASE hot|diagnostic' >&2; exit 2;
}
[[ ( $EXECUTION_MODE == diagnostic && $FRAME_COUNT == 4 && ( $MODE == resident || $MODE == skipEvaluate ) ) ||
   ( $EXECUTION_MODE == hot && ( $MODE == resident || $MODE == srOnly ) && ( $FRAME_COUNT == 4 || $FRAME_COUNT == 120 || $FRAME_COUNT == 1800 || $FRAME_COUNT == 18000 ) ) ]] || {
    echo 'P0_LAUNCH BLOCKED mode/count/execution bounds' >&2; exit 3;
}
[[ $DIAGNOSTIC_CASE =~ ^[A-Za-z0-9-]{1,63}$ ]] || exit 2
GAME="$POC_ROOT/playtest/assets/CUSA03173"
BUILD_DIR=${BB_P0_BUILD_DIR:-$WT/out}
INITIAL=${BB_NR_P0_INITIAL_ENABLE:-1}
STARTS=${BB_NR_P0_MAX_HELPER_STARTS:-32}
WALL=${BB_NR_P0_SERVICE_WALL_SECONDS:-900}
[[ $INITIAL == 0 || $INITIAL == 1 ]] || exit 2
[[ $STARTS =~ ^[0-9]+$ && $WALL =~ ^[0-9]+$ && $STARTS -le 32 && $WALL -ge 1 && $WALL -le 900 ]] || exit 2
if [[ $MODE == srOnly ]]; then INITIAL=0; STARTS=0; else [[ $STARTS -ge 1 ]] || exit 2; fi
RUN="$POC_ROOT/bbport/runs/p0-game-$SESSION"
PROFILE="$RUN/profile"
GE=/home/arinp/.local/share/Steam/compatibilitytools.d/GE-Proton11-7-x86_64
WINE="$GE/files/lib/wine/x86_64-unix/wine"
PRELOADER="$GE/files/lib/wine/x86_64-unix/wine-preloader"
HELPER="$POC_ROOT/opti-worktree/poc/game_nr/playable-quality/out/wine_game_nr_playable.exe"
DLLPATH="$GE/files/lib/vkd3d:$GE/files/lib/wine"

[[ -f "$BUILD_DIR/bb-probe" && -f "$BUILD_DIR/gpu/libbbgpu.so" && -f "$BUILD_DIR/libbbport_dlss.so" ]] || {
    echo "P0_LAUNCH BLOCKED missing P0 worktree build under $WT/out" >&2; exit 3;
}
[[ $MODE == srOnly || -f "$HELPER" ]] || { echo "P0_LAUNCH BLOCKED missing reviewed helper $HELPER" >&2; exit 3; }
[[ -f "$GAME/eboot.bin" ]] || { echo "P0_LAUNCH BLOCKED missing read-only game asset $GAME/eboot.bin" >&2; exit 3; }
[[ -f "$PROFILE/.p0-profile" && $(cat "$PROFILE/.p0-profile") == "$SESSION" ]] || {
    echo "P0_LAUNCH BLOCKED private P0 seed/profile marker missing or mismatched; use integration/prepare_seed.py p0 $SESSION" >&2
    exit 3
}
mkdir -p -m 700 "$RUN" "$PROFILE/logs" "$RUN/dump" "$PROFILE/wine-prefix"
chmod 700 "$RUN" "$PROFILE" "$PROFILE/logs" "$RUN/dump" "$PROFILE/wine-prefix"
cat > "$PROFILE/bbport.ini" <<'CONFIG'
upscaler=dlss
preset=1
output_res=1920x1080
jitter=1
object_motion=1
sharpen=0
CONFIG
chmod 600 "$PROFILE/bbport.ini"

INVOCATION=$(date -u +%Y%m%dT%H%M%SZ)
ENV_LOG="$RUN/launch-$INVOCATION.env"
{
    printf 'session=%s\nmode=%s\nprofile_id=p0-quality-hdr-direct720-experimental-v1\n' "$SESSION" "$MODE"
    printf 'worktree=%s\nhead=%s\ngame_asset_readonly=%s\n' "$WT" "$(git -C "$WT" rev-parse HEAD)" "$GAME"
    printf 'profile=%s\nprivate_save=%s\nprivate_wineprefix=%s\n' "$PROFILE" "$PROFILE/user/savedata" "$PROFILE/wine-prefix"
    printf 'helper=%s\nhelper_sha256=%s\n' "$HELPER" "$(if [[ -f $HELPER ]]; then sha256sum "$HELPER" | awk '{print $1}'; else printf NOT_RUN; fi)"
    printf 'wine=%s\npreloader=%s\nWINELOADERNOEXEC=1\n' "$WINE" "$PRELOADER"
    printf 'WINEDLLOVERRIDES=mscoree,mshtml=;nvapi64=n;dxgi=n\nWINEDLLPATH=%s\n' "$DLLPATH"
    printf 'WINEDEBUG=-all,warn+vulkan,err+vulkan\nDXVK_ENABLE_NVAPI=1\nDXVK_NVAPI_LOG_LEVEL=trace\nNVIDIA_WINE_DLL_DIR=/usr/lib64/nvidia/wine\n'
    printf 'protocol_version=6\nexecution_mode=%s\n' "$EXECUTION_MODE"
    printf 'frame_count=%s\ndiagnostic_case=%s\nBB_FPS=uncap\nBB_FPS_LIMIT=90\n' "$FRAME_COUNT" "$DIAGNOSTIC_CASE"
    printf 'BB_NR_P0_INITIAL_ENABLE=%s\nBB_NR_P0_MAX_HELPER_STARTS=%s\nBB_NR_P0_SERVICE_WALL_SECONDS=%s\nBB_P0_BUILD_DIR=%s\nauto_start=first_supported_SR_frame_no_trigger\n' "$INITIAL" "$STARTS" "$WALL" "$BUILD_DIR"
    printf 'exposure_valid=false\nmanual_white_point=1\n'
    printf 'gpu_test=owned by integration/run_quality_nr.py; launcher itself does not acquire GPU lock\n'
} > "$ENV_LOG"
chmod 600 "$ENV_LOG"

export BB_GAME_DIR="$GAME" BB_DATA_DIR="$PROFILE" BB_CONFIG="$PROFILE/bbport.ini"
export BB_USER_DIR="$PROFILE/user" BB_GPU_USER_DIR="$PROFILE/user"
export BB_NR_P0_INITIAL_ENABLE="$INITIAL" BB_NR_P0_MAX_HELPER_STARTS="$STARTS" BB_NR_P0_SERVICE_WALL_SECONDS="$WALL"
export BB_PREBUILT=1 BB_PROBE="$BUILD_DIR/bb-probe" BB_UPSCALER=dlss BB_UPSCALE_PRESET=1
export BB_DLSS=1 BB_DLSS_AUDIT=1 BB_FPS=uncap BB_FPS_LIMIT=90 BB_FRAME_STATS=1 BB_FRAME_LOG="$PROFILE/logs/frames.csv"
export BB_SAVE_LOG=1 BB_MODS_ENABLED=0 BB_IME_TEXT=NRTest
export BB_PAD_FILE="$RUN/pad.txt" BB_DUMP_DIR="$RUN/dump"
: > "$BB_PAD_FILE"
chmod 600 "$BB_PAD_FILE"
unset BB_NR_C1_MODE BB_NR_C2_MODE BB_NR_C3_MODE BB_NR_C4_MODE BB_NR_C4_BASELINE BB_NR_Q1_MODE BB_NR_Q0_CAPTURE BB_NR_C0_CAPTURE BB_Q0_SLOW_WAIT_DIAG BB_Q0_WAIT_STATE_DIAG
export BB_NR_P0_EXECUTION_MODE="$EXECUTION_MODE"
export BB_NR_P0_FRAME_COUNT="$FRAME_COUNT" BB_NR_P0_DIAGNOSTIC_CASE="$DIAGNOSTIC_CASE"
export BB_NR_P0_MODE="$MODE" BB_NR_P0_SESSION="$SESSION" BB_NR_P0_RUN_DIR="$RUN"
export BB_NR_P0_RUN_DIR_WINDOWS="Z:$RUN" BB_NR_P0_PRELOADER="$PRELOADER" BB_NR_P0_WINE="$WINE"
export BB_NR_P0_HELPER_EXE="$HELPER" BB_NR_P0_WINEPREFIX="$PROFILE/wine-prefix"
export BB_NR_P0_WINEDLLOVERRIDES='mscoree,mshtml=;nvapi64=n;dxgi=n'
export BB_NR_P0_WINEDLLPATH="$DLLPATH" BB_NR_P0_WINEDEBUG='-all,warn+vulkan,err+vulkan'
export WINEDLLOVERRIDES="$BB_NR_P0_WINEDLLOVERRIDES" WINEARCH=win64
export WINEDLLPATH="$DLLPATH" WINEDEBUG="$BB_NR_P0_WINEDEBUG"
export NVIDIA_WINE_DLL_DIR=/usr/lib64/nvidia/wine DXVK_NVAPI_LOG_LEVEL=trace DXVK_ENABLE_NVAPI=1
export BB_JITTER=1 BB_PRESENT_DUMP_TRIGGER="$RUN/present-dump-trigger" BB_PRESENT_DUMP_COUNT=1
export DLSSNR_DISABLE=1 DLSSNR_AUTO_DOWNLOAD=0
unset BB_RENDER_RES BB_OUTPUT_RES BB_AUTO_RENDER_RES BB_PRESET_FILE BB_TOGGLE_FILE BB_TIMEOUT BB_AUDIT_SAVE_BACKUP
printf 'P0_LAUNCH_READY session=%s mode=%s profile=%s helper=%s env=%s\n' "$SESSION" "$MODE" "$PROFILE" "$HELPER" "$ENV_LOG"
exec bash "$WT/run.sh"
