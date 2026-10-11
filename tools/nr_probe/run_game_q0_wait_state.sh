#!/usr/bin/env bash
# Waiting-thread observer only; no Q0 10ms monitor. Original launchers remain unchanged.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
[[ $# -eq 1 ]] || { echo 'Usage: run_game_q0_wait_state.sh SESSION' >&2; exit 2; }
unset BB_Q0_SLOW_WAIT_DIAG
export BB_Q0_WAIT_STATE_DIAG=1
printf 'Q0_WAIT_STATE_LAUNCH BB_Q0_WAIT_STATE_DIAG=1 BB_Q0_SLOW_WAIT_DIAG=UNSET observer=waiting_thread limit=3 threshold_ms=100 diagnostics_not_FPS_scored=1\n'
exec bash "$WT/tools/nr_probe/run_game_q0.sh" "$1"
