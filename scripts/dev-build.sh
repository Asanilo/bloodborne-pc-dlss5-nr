#!/usr/bin/env bash
# Incremental development build for any Bloodborne worktree.
#
#   bash scripts/dev-build.sh                  # build the current worktree into its out/
#   bash scripts/dev-build.sh --test           # build and run the C runtime tests
#   bash scripts/dev-build.sh --worktree .worktree/fg --test
#   bash scripts/dev-build.sh --clean          # remove only rebuildable build outputs
#
# Shared, read-only dependencies live in the main workspace's out/deps; the compiler cache
# lives in out/cache/ccache. Neither is copied into a worktree. Each worktree keeps its own
# build.sh (NR/FG carry branch-specific online/prepared-deps handling); this script supplies
# the shared environment and cache. The mutable out/ tree is the development build;
# release/baseline builds use scripts/release-build.sh.
set -euo pipefail
# The scripts are safe to copy into a development branch: find the main worktree (where
# out/deps, out/toolchain and out/cache live) instead of assuming this checkout is it.
main_worktree() {
    local first
    first=$(git -C "$1" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' | head -n1)
    printf '%s\n' "${first:-$1}"
}
script_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
shared_root=${BB_SHARED_ROOT:-$(main_worktree "$script_root")}
shared_root=$(cd -- "$shared_root" && pwd)
target=$PWD
build_args=()
clean=0
while (($#)); do
    case $1 in
        --worktree)
            [[ $# -ge 2 ]] || { echo '--worktree needs a path' >&2; exit 2; }
            target=$2; shift 2 ;;
        --clean)
            clean=1; shift ;;
        -h|--help)
            sed -n '2,17p' "$0"; exit 0 ;;
        *)
            build_args+=("$1"); shift ;;
    esac
done
target=$(cd -- "$target" && pwd)
[[ -f $target/build.sh ]] || { echo "Not a source worktree: $target" >&2; exit 2; }
cd "$target"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || { echo "Not a git worktree: $target" >&2; exit 2; }

deps=$shared_root/out/deps
[[ -f $deps/bbport-env.sh ]] || { echo "Missing shared dependencies: $deps/bbport-env.sh" >&2; exit 2; }
# shellcheck disable=SC1091
source "$deps/bbport-env.sh"

# One cache for every worktree. Each worktree sees dependencies through its own out/deps
# symlink, so CMake emits the same relative include paths in every tree; CCACHE_BASEDIR then
# rewrites the remaining absolute paths. This is what makes cross-worktree hits possible.
deps_view=$target/out/deps
[[ -e $deps_view ]] || deps_view=$deps
export CMAKE_PREFIX_PATH="$deps_view/root/usr"
export CPATH="$deps_view/root/usr/include:$deps_view/xbyak"
export LIBRARY_PATH="$deps_view/root/usr/lib"
export LD_LIBRARY_PATH="$deps_view/root/usr/lib:$deps_view/vulkan-validation/usr/lib"
export VK_LAYER_PATH="$deps_view/vulkan-validation/usr/share/vulkan/explicit_layer.d"
export DLSS_SDK_ROOT="$deps_view/dlss-sdk"
export CCACHE_DIR=${CCACHE_DIR:-$shared_root/out/cache/ccache}
export CCACHE_BASEDIR=${CCACHE_BASEDIR:-$target}
export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-10G}
export CCACHE_COMPILERCHECK=${CCACHE_COMPILERCHECK:-content}
export CCACHE_SLOPPINESS=${CCACHE_SLOPPINESS:-time_macros}
mkdir -p "$CCACHE_DIR"

ccache_bin=$shared_root/out/toolchain/ccache/ccache
if [[ -x $ccache_bin ]]; then
    export BB_CCACHE_BIN=$ccache_bin
    export BB_REAL_CC=${BB_REAL_CC:-$(command -v cc || command -v gcc)}
    export BB_REAL_CXX=${BB_REAL_CXX:-$(command -v c++ || command -v g++)}
    export BB_CCACHE_WRAPPER=1
    export CC=$shared_root/out/toolchain/bin/cc
    export CXX=$shared_root/out/toolchain/bin/c++
    # Keep the cache honest when a different compiler is selected explicitly.
    export CCACHE_PATH=$(dirname -- "$BB_REAL_CC"):$(dirname -- "$BB_REAL_CXX")
else
    echo 'ccache not installed; building without a compiler cache (see scripts/install-toolchain.sh).' >&2
fi

# Development defaults: no LTO (faster links), no PGO unless asked.
export BB_LTO=${BB_LTO:-OFF}
export BB_PGO=${BB_PGO:-off}

if ((clean)); then
    rm -rf -- "$target/out/gpu" "$target/out/atrac9" "$target/out/dlss-bridge" "$target/out/pgo"
    rm -f -- "$target/out/bb-probe" "$target/out/bb-gpu-capabilities" \
        "$target/out/libatrac9.a" "$target/out/gpu-build.log" "$target/out/gpu-cmake.log" \
        "$target/out/pad-test" "$target/out/runtime-test" "$target/out/file-mods-test" \
        "$target/out/sema-test" "$target/out/content-test"
    echo "Cleaned development build outputs in $target/out (evidence and shared deps kept)."
fi

# Older NR/FG build.sh uses BB_BUILD_DIR; the master build.sh uses BB_OUT_DIR. Map one to
# the other so release-build can pass a single variable.
if grep -q 'BB_BUILD_DIR' "$target/build.sh"; then
    export BB_BUILD_DIR=${BB_OUT_DIR:-$target/out}
    build_out=$BB_BUILD_DIR
else
    export BB_OUT_DIR=${BB_OUT_DIR:-$target/out}
    build_out=$BB_OUT_DIR
fi
echo "dev-build: $target"
echo "  out:   $build_out"
echo "  deps:  $deps"
echo "  cache: $CCACHE_DIR"
exec bash "$target/build.sh" "${build_args[@]}"
