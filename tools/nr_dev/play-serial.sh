#!/usr/bin/env bash
set -euo pipefail
: "${NRBRIDGE_HOME:?Select the isolated serial candidate home}"
if [[ $# -lt 1 ]]; then
    echo "usage: NRBRIDGE_HOME=/private/candidate play-serial.sh /private/candidate/localinputs/runtime.json [play.py options]" >&2
    exit 2
fi
inputs=$1
shift
here=$(cd -- "$(dirname -- "$0")" && pwd -P)
exec python3 "$here/play.py" --daily --serial-multipass --fg-backend off --local-inputs "$inputs" "$@"
