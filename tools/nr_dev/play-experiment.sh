#!/usr/bin/env bash
# Launch one explicitly sealed profile through the existing scene-first Runner.
set -euo pipefail
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
if (($# < 1)) || [[ -z ${NRBRIDGE_HOME:-} ]]; then
    echo 'Usage: NRBRIDGE_HOME=/absolute/private-root bash tools/nr_dev/play-experiment.sh /absolute/private-root/localinputs/profile.json [play options]' >&2
    exit 2
fi
profile=$1
shift
exec python3 "$source_root/tools/nr_dev/play.py" --daily \
    --native-build "$source_root/out" --local-inputs "$profile" "$@"
