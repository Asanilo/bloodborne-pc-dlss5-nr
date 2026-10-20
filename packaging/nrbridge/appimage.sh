#!/usr/bin/env bash
# Build the private bbport NRBridge AppImage preview.  The checked Native
# baseline, Helper PE and forwarder are read-only inputs; the game, saves,
# credentials and NR model stay outside the image.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
bash packaging/nrbridge/fetch-tools.sh
exec python3 packaging/nrbridge/build.py "$@"
