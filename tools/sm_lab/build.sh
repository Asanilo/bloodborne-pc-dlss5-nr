#!/usr/bin/env bash
# SM experiment build: independent build dir, prepared deps, no LTO.
set -euo pipefail
WT=/home/arinp/Code/bloodborne_pc/.worktree/sm
BUILD=/home/arinp/Code/nr-home/build/sm-present
mkdir -p "$BUILD"
cd "$WT"
export NR_SOURCE_DEPS_ROOT=/home/arinp/Code/nr-home/inputs/source-deps
export NR_BUILD_ENV_ROOT=/home/arinp/Code/bloodborne_pc/out/deps
export BB_BUILD_DIR="$BUILD"
export BB_LTO=OFF BB_PGO=off
export CMAKE_BUILD_PARALLEL_LEVEL=${JOBS:-8}
exec bash tools/nr_dev/build.sh
