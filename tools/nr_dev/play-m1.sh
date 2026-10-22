#!/usr/bin/env bash
# Source-built M1 entry. Models, Wine/GE, game and saves are explicitly sealed private inputs.
set -euo pipefail
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
if (($# < 1)) || [[ $1 == --help || -z ${NRBRIDGE_HOME:-} ]]; then
    echo 'Usage: NRBRIDGE_HOME=/absolute/private-root bash tools/nr_dev/play-m1.sh PROFILE.json [--fg] [--sr-only] [play options]'
    echo 'FG defaults off. --fg selects Experimental HUDless FSR x2; --sr-only disables NR.'
    exit 2
fi
profile=$1
shift
options=()
for option in "$@"; do
    if [[ $option == --fg ]]; then
        options+=(--fg-native --experimental-fg fsr31-hudless)
    else
        options+=("$option")
    fi
done
exec bash "$source_root/tools/nr_dev/play-experiment.sh" "$profile" "${options[@]}"
