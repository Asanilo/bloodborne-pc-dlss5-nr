#!/usr/bin/env bash
# Post-reboot preflight and one-command SM acceptance sequence.
#
#   reboot_preflight.sh            check the current boot admits a daily session
#   acceptance.sh "sm off sm sm"   run the plan in order (SM rounds + one off baseline)
#
# The daily runner takes the project GPU lock itself. Every round leaves its own
# session directory; nothing is deleted or rewritten.
set -euo pipefail
WT=/home/arinp/Code/bloodborne_pc/.worktree/sm
HOME_NR=/home/arinp/Code/nr-home/build/sm-official
RECEIPT=${RECEIPT:-/run/user/$(id -u)/bbport-nrbridge-session.json}
LOCK=/tmp/nrbridge-poc-gpu.lock

preflight() {
    local boot state root session
    boot=$(cat /proc/sys/kernel/random/boot_id)
    echo "BOOT=$boot"
    if [[ -f $RECEIPT ]]; then
        # A receipt from an older boot is ignored by the runner's admit(); only the
        # current boot's pending/HOLD blocks new daily sessions.
        read -r rboot state root session < <(python3 - "$RECEIPT" <<'PY_RECEIPT'
import json, sys
d = json.load(open(sys.argv[1]))
print(d.get('boot_id'), d.get('state'), d.get('root'), d.get('session'))
PY_RECEIPT
)
        echo "RECEIPT boot=$rboot state=$state root=$root session=$session"
        if [[ $rboot != "$boot" ]]; then
            echo "STALE_RECEIPT from a previous boot; the runner ignores it"
        elif [[ $state != retired ]]; then
            echo "BLOCKED receipt is $state from this boot's earlier session; a new boot is required"
            return 1
        fi
    else
        echo "RECEIPT absent (first session in this boot)"
    fi
    if pgrep -x bb-probe >/dev/null 2>&1; then echo "BLOCKED bb-probe is running"; return 1; fi
    if pgrep -f wine_runtime_model.exe >/dev/null 2>&1; then echo "BLOCKED Helper is running"; return 1; fi
    if ! nvidia-smi -L >/dev/null 2>&1; then echo "BLOCKED nvidia-smi cannot talk to the driver"; return 1; fi
    # Probe only: the daily runner takes the lock itself for every round, so this
    # shell must not keep it open past the check.
    if ! ( exec 9>"$LOCK"; flock -n 9 ); then echo "BLOCKED GPU lock is held"; return 1; fi
    echo "PREFLIGHT_OK gpu=$(nvidia-smi --query-gpu=name --format=csv,noheader | head -1)"
}

if [[ ${1:-} == preflight ]]; then
    preflight
    exit
fi

plan=${1:-sm}
seconds=${2:-45}
preflight

prefix_owner=
index=0
for kind in $plan; do
    index=$((index + 1))
    session="sm-acc-$(date +%m%d-%H%M%S)-${kind}${index}"
    args=()
    # PrefixLease moves the borrowed prefix back to its owner after a normal
    # retirement, so later rounds must reuse the session that created it.
    [[ -n $prefix_owner ]] && args+=(--reuse-nr-prefix "$prefix_owner")
    echo "===== ROUND $index/$kind session=$session ====="
    if [[ $kind == off ]]; then SM_MODE=off FRAME_CAPTURE=${FRAME_CAPTURE:-1} \
        bash "$WT/tools/sm_lab/official_play.sh" "$session" "$seconds" "${args[@]}"; else
        SM_MODE=on FRAME_CAPTURE=${FRAME_CAPTURE:-1} \
        bash "$WT/tools/sm_lab/official_play.sh" "$session" "$seconds" "${args[@]}"; fi
    python3 - "$HOME_NR/runs/$session" "$kind" <<'PY' || true
import json, sys
from pathlib import Path
run, kind = Path(sys.argv[1]), sys.argv[2]
result = json.loads((run / 'result.json').read_text()) if (run / 'result.json').exists() else {}
print('ROUND', kind, 'status', result.get('status'), 'returncode', result.get('returncode'))
manifest = json.loads((run / 'manifest.json').read_text()) if (run / 'manifest.json').exists() else {}
env = manifest.get('environment', {})
print('  pacing fps_limit', env.get('BB_FPS_LIMIT'), 'present_mode', env.get('BB_PRESENT_MODE'))
log = run / 'game.log'
if log.exists():
    text = log.read_text(errors='replace')
    print('  markers', [l.split(' ', 1)[0] for l in text.splitlines()
                        if l.startswith(('SM_EXIT_', 'FG_EXIT_', 'NATIVE_ASYNC_PRE_SR_EXIT_PROOF'))])
    print('  device_lost', 'DeviceLost' in text or 'DEVICE_LOST' in text)
summary = run / 'display-frames.summary'
if summary.exists():
    print('  display', summary.read_text().strip())
PY
    [[ -z $prefix_owner ]] && prefix_owner=$session
    if [[ -f $HOME_NR/runs/$session/result.json ]]; then
        status=$(python3 -c "import json;print(json.load(open('$HOME_NR/runs/$session/result.json')).get('status'))")
        if [[ $status == HOLD ]]; then echo "ROUND_HOLD $session; stopping the plan"; exit 5; fi
    fi
done
echo "PLAN_DONE $plan"
