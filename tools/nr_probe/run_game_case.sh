#!/usr/bin/env bash
# Opt-in real-game capture using the worktree build and a disposable save/config profile.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
case ${1:-} in dlaa) preset=0;; quality) preset=1;; *) echo 'Usage: run_game_case.sh dlaa|quality' >&2; exit 2;; esac
: "${BB_AUDIT_GAME_DIR:?Set this to your existing game extraction (read only)}"
for file in out/bb-probe out/libbbport_dlss.so; do
    [[ -f $file ]] || { echo "DLSS_GAME FAIL missing $file; build the worktree first" >&2; exit 2; }
done
compgen -G 'out/libnvidia-ngx-dlss.so.*' >/dev/null || { echo 'DLSS_GAME FAIL missing NGX runtime' >&2; exit 2; }
mkdir -p out/audit/game-cases
profile=$(mktemp -d "$PWD/out/audit/game-cases/${1}.XXXXXXXX")
mkdir -p "$profile/user" "$profile/dump"
# Optional archived snapshot, never the live installation's user_dir.
if [[ -n ${BB_AUDIT_SAVE_BACKUP:-} ]]; then
    [[ -f $BB_AUDIT_SAVE_BACKUP/manifest.json && -d $BB_AUDIT_SAVE_BACKUP/savedata ]] || {
        echo 'DLSS_GAME FAIL expected a backup directory with manifest.json and savedata/' >&2; exit 2;
    }
    cp -a -- "$BB_AUDIT_SAVE_BACKUP/savedata" "$profile/user/"
fi
cat > "$profile/bbport.ini" <<CONFIG
upscaler=dlss
preset=$preset
output_res=1920x1080
jitter=1
object_motion=1
sharpen=0
hdr=0
CONFIG
export BB_GAME_DIR="$BB_AUDIT_GAME_DIR" BB_DATA_DIR="$profile" BB_CONFIG="$profile/bbport.ini"
export BB_USER_DIR="$profile/user" BB_GPU_USER_DIR="$profile/user"
export BB_PREBUILT=1 BB_PROBE="$PWD/out/bb-probe" BB_UPSCALER=dlss BB_UPSCALE_PRESET="$preset"
export BB_DLSS=1 BB_DLSS_AUDIT=1 BB_FPS=60 BB_FRAME_STATS=1 BB_FRAME_LOG="$profile/frames.csv"
export BB_DUMP_TRIGGER="$profile/dump-trigger" BB_DUMP_DIR="$profile/dump" BB_DUMP_FRAMES=8
# Keep the audited baseline independent of the launcher's other environment options.
unset BB_RENDER_RES BB_OUTPUT_RES BB_AUTO_RENDER_RES BB_PRESET_FILE BB_TOGGLE_FILE BB_TIMEOUT
export BB_MODS_ENABLED=0
printf 'DLSS_GAME profile=%s\nDump during gameplay: touch %q\n' "$profile" "$BB_DUMP_TRIGGER"
bash run.sh 2>&1 | tee "$profile/run.log"
python3 tools/nr_probe/check_dlss_log.py "$profile/run.log" --mode "$1"
