#!/usr/bin/env bash
# Private Experimental entry; never replaces the player's installed launcher.
set -euo pipefail
entry=$(realpath -- "${BASH_SOURCE[0]}")
source_root=$(cd -- "$(dirname -- "$entry")/../.." && pwd -P)
export NRBRIDGE_HOME=${NRBRIDGE_HOME:-/home/arinp/Code/bloodborne_pc/.worktree/nrbridge/out/nr-dev}
preset=${1:-B}
if (($#)); then shift; fi
case "$preset" in
  A|B) config="$NRBRIDGE_HOME/localinputs/nr310-$preset-playable.json" ;;
  old) config="$NRBRIDGE_HOME/localinputs/migrated-old.json" ;;
  -h|--help) printf '%s\n' 'Usage: bash out/play-dlssg.sh [A|B|old] [play options]' \
    'Validated: B, 1080p Quality, Old memory model; Hunters Dream private save.' \
    'F8: NR start/toggle. Delete: independent DLSS FG checkbox.' \
    'Optional fallback: --dlssg-transport files-v1'; exit 0 ;;
  *) printf '%s\n' 'Usage: bash out/play-dlssg.sh [A|B|old] [play options]' >&2; exit 2 ;;
esac
exec python3 "$source_root/tools/nr_dev/play.py" --daily --fg-native \
  --experimental-fg dlssg --dlssg-transport mapped-v1 \
  --dlssg-model "${BB_DLSSG_MODEL:-/home/arinp/Code/dlss5_linux/dll/runtime/fg/streamline-2.14.1/nvngx_dlssg.dll}" \
  --dlssg-prefix-session m2-native-dlssg-validation-r17 \
  --native-build "$source_root/out" --local-inputs "$config" \
  --continue-session play-20261022-141640 "$@"
