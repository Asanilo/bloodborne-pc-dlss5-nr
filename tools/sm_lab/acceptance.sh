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
if [[ ${1:-} == plan ]]; then
    # Offline: print the rounds and the prefix owner without touching the GPU.
    python3 "$WT/tools/sm_lab/prefix_plan.py" plan --stamp "${3:-test}" --plan "${2:-sm}"
    exit
fi

plan=${1:-sm}
seconds=${2:-45}
preflight

stamp=$(date +%m%d-%H%M%S)
while IFS=$'\t' read -r index kind session reuse; do
    args=()
    if [[ $reuse != - ]]; then
        # A borrowed session ends empty after the return; only the initialized
        # owner may be borrowed from. Refuse instead of handing over an empty dir.
        if ! python3 "$WT/tools/sm_lab/prefix_plan.py" check-owner --home "$HOME_NR" --session "$reuse"; then
            echo "OWNER_NOT_INITIALIZED $reuse; stopping the plan"
            exit 6
        fi
        args+=(--reuse-nr-prefix "$reuse")
    fi
    echo "===== ROUND $((index + 1))/$kind session=$session reuse=$reuse ====="
    if [[ $kind == off ]]; then
        SM_MODE=off FRAME_CAPTURE=${FRAME_CAPTURE:-1} \
            bash "$WT/tools/sm_lab/official_play.sh" "$session" "$seconds" "${args[@]}"
    else
        SM_MODE=on FRAME_CAPTURE=${FRAME_CAPTURE:-1} \
            bash "$WT/tools/sm_lab/official_play.sh" "$session" "$seconds" "${args[@]}"
    fi
    python3 "$WT/tools/sm_lab/round_report.py" "$HOME_NR/runs/$session" "$kind" || true
    if [[ -f $HOME_NR/runs/$session/result.json ]]; then
        status=$(python3 -c "import json;print(json.load(open('$HOME_NR/runs/$session/result.json')).get('status'))")
        if [[ $status == HOLD ]]; then echo "ROUND_HOLD $session; stopping the plan"; exit 5; fi
    fi
done < <(python3 "$WT/tools/sm_lab/prefix_plan.py" plan --stamp "$stamp" --plan "$plan")
echo "PLAN_DONE $plan"
