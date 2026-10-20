#!/usr/bin/env bash
# Prepare and launch the normal live Quality HDR capture; this script is opt-in.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
POC_ROOT="${BB_GAME_POC_ROOT:-$HOME/Code/nrbridge-design/game-poc-20261011}"
SESSION=${1:-}
[[ $SESSION =~ ^[A-Za-z0-9][A-Za-z0-9-]{0,62}$ ]] || {
    echo 'Usage: run_game_q0.sh SESSION' >&2; exit 2;
}
GAME="${BB_GAME_DIR:-$HOME/bloodborne/CUSA03173}"
RUN="$POC_ROOT/bbport/runs/q0-game-$SESSION"
PROFILE="$RUN/profile"
[[ -f "$WT/out/bb-probe" && -f "$WT/out/libbbport_dlss.so" ]] || {
    echo "Q0_LAUNCH BLOCKED missing worktree build under $WT/out; run tools/nr_probe/build_q0.sh" >&2; exit 3;
}
[[ -f "$GAME/eboot.bin" ]] || { echo "Q0_LAUNCH BLOCKED missing read-only game asset $GAME/eboot.bin" >&2; exit 3; }
mkdir -p -m 700 "$RUN" "$PROFILE/user" "$PROFILE/logs" "$RUN/dump"
chmod 700 "$RUN" "$PROFILE" "$PROFILE/user" "$PROFILE/logs" "$RUN/dump"
if [[ -e "$PROFILE/.q0-profile" ]]; then
    [[ $(cat "$PROFILE/.q0-profile") == "$SESSION" ]] || { echo 'Q0_LAUNCH BLOCKED profile session mismatch' >&2; exit 3; }
else
    [[ -z $(find "$PROFILE/user" -mindepth 1 -maxdepth 1 -print -quit) ]] || {
        echo 'Q0_LAUNCH BLOCKED refusing to adopt a nonempty save directory without this PoC marker' >&2; exit 3;
    }
    printf '%s\n' "$SESSION" > "$PROFILE/.q0-profile"
    chmod 600 "$PROFILE/.q0-profile"
fi
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
    printf 'session=%s\nprofile_id=hdr-run-quality-j1-capture-v1\n' "$SESSION"
    printf 'worktree=%s\nhead=%s\ngame_asset_readonly=%s\n' "$WT" "$(git -C "$WT" rev-parse HEAD)" "$GAME"
    printf 'profile=%s\nconfig=%s\nprivate_save=%s\n' "$PROFILE" "$PROFILE/bbport.ini" "$PROFILE/user"
    printf 'upscaler=dlss\npreset=1\noutput_res=1920x1080\nrender_res_override=unset\nhdr_route=must_be_confirmed_in_runtime_log\nexposure_valid=false\n'
    printf 'nr_helper=NOT_STARTED\nWine=NOT_STARTED\nBB_FPS=uncap\nBB_FPS_LIMIT=90\nBB_JITTER=1\n'
    printf 'q0_capture=1\ndump_trigger=%s/dump-trigger\ndump_dir=%s/dump\ndump_frames=2\npresent_dump_trigger=%s/present-dump-trigger\n' "$RUN" "$RUN" "$RUN"
    printf 'ime_text=NRTest\npad_file=%s/pad.txt\n' "$RUN"
    printf 'gpu_test=not_run_by_launcher_preparer\n'
} > "$ENV_LOG"
chmod 600 "$ENV_LOG"
export BB_GAME_DIR="$GAME" BB_DATA_DIR="$PROFILE" BB_CONFIG="$PROFILE/bbport.ini"
export BB_USER_DIR="$PROFILE/user" BB_GPU_USER_DIR="$PROFILE/user"
export BB_PREBUILT=1 BB_PROBE="$WT/out/bb-probe" BB_UPSCALER=dlss BB_UPSCALE_PRESET=1
export BB_DLSS=1 BB_DLSS_AUDIT=1 BB_FPS=uncap BB_FPS_LIMIT=90 BB_FRAME_STATS=1 BB_FRAME_LOG="$PROFILE/logs/frames.csv"
export BB_SAVE_LOG=1 BB_MODS_ENABLED=0
unset BB_NR_C0_CAPTURE BB_NR_C1_MODE BB_NR_C2_MODE BB_NR_C3_MODE BB_NR_C4_MODE BB_NR_C4_BASELINE
export BB_NR_Q0_CAPTURE=1 BB_JITTER=1
export BB_DUMP_TRIGGER="$RUN/dump-trigger" BB_DUMP_DIR="$RUN/dump" BB_DUMP_FRAMES=2
export BB_FINAL_DUMP_TRIGGER="$RUN/final-dump-trigger"
export BB_PRESENT_DUMP_TRIGGER="$RUN/present-dump-trigger" BB_PRESENT_DUMP_COUNT=1
export BB_IME_TEXT=NRTest BB_PAD_FILE="$RUN/pad.txt"
: > "$BB_PAD_FILE"
chmod 600 "$BB_PAD_FILE"
unset BB_RENDER_RES BB_OUTPUT_RES BB_AUTO_RENDER_RES BB_PRESET_FILE BB_TOGGLE_FILE BB_TIMEOUT BB_AUDIT_SAVE_BACKUP
printf 'Q0_LAUNCH_READY session=%s profile=%s env=%s\n' "$SESSION" "$PROFILE" "$ENV_LOG"
printf 'After the private 3D scene is visible, touch %q for two Q0 frames (indices remain unique across sequences); touch %q for one UI-inclusive present image.\n' "$BB_DUMP_TRIGGER" "$BB_PRESENT_DUMP_TRIGGER"
exec bash "$WT/run.sh"
