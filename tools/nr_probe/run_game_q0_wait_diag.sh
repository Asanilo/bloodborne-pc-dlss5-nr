#!/usr/bin/env bash
# Explicit diagnostic entry point; original Quality launcher remains unchanged/default-off.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
[[ $# -eq 1 ]] || { echo 'Usage: run_game_q0_wait_diag.sh SESSION' >&2; exit 2; }
export BB_Q0_SLOW_WAIT_DIAG=1
printf 'Q0_WAIT_DIAG_LAUNCH BB_Q0_SLOW_WAIT_DIAG=1 slow_threshold_ms=100 monitor_period_ms=10 diagnostics_not_FPS_scored=1\n'
exec bash "$WT/tools/nr_probe/run_game_q0.sh" "$1"
