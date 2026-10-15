#!/usr/bin/env bash
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
ROOT=${NR_DEV_ROOT:-/home/arinp/Code/nrbridge-dev}
STABLE=/home/arinp/Code/bloodborne_pc
BUILD=${BB_BUILD_DIR:-$ROOT/build/native}
mkdir -p "$ROOT/inputs/source-deps" "$BUILD"
exec 9>"$BUILD/build.lock"
flock -n 9 || { echo 'build directory already has a writer' >&2; exit 3; }
for spec in 'gpu/third_party/fsr-vulkan fsr-vulkan' 'gpu/third_party/imgui imgui' 'third_party/LibAtrac9 LibAtrac9'; do
 read -r rel name <<< "$spec"
 dst="$ROOT/inputs/source-deps/$name"
 if [[ ! -e $dst ]]; then
  mkdir "$dst"
  cp -a "$STABLE/$rel/." "$dst/"
  # Preserve source bytes, but no repository pointer to the stable tree.
  [[ ! -f $dst/.git ]] || mv "$dst/.git" "$dst/git-pointer.readonly.txt"
  if [[ $name == fsr-vulkan ]]; then
   for patch in "$WT"/gpu/patches/fsr-vulkan/*.patch; do
    git -C "$dst" apply --reverse --check "$patch" 2>/dev/null || git -C "$dst" apply "$patch"
   done
  fi
  (cd "$dst" && find . -type f -print0 | sort -z | xargs -0 sha256sum) > "$dst.content.sha256"
 fi
 if [[ ! -L $WT/$rel ]]; then
  rmdir "$WT/$rel" # Never replace a populated checkout.
  ln -s "$dst" "$WT/$rel"
 fi
 [[ $(realpath "$WT/$rel") == "$dst" ]] || exit 3
done
source "$STABLE/out/deps/bbport-env.sh"
export PKG_CONFIG_PATH="$STABLE/out/deps/root/usr/lib/pkgconfig:$STABLE/out/deps/root/usr/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export BB_BUILD_DIR="$BUILD" BB_PGO=off BB_LTO=OFF CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-8}
git -C "$WT" rev-parse HEAD > "$BUILD/source-head.txt"
git -C "$WT" diff --ignore-submodules=all --binary HEAD > "$BUILD/source-diff.patch"
bash "$WT/build.sh" "$@" 2>&1 | tee "$BUILD/build.log"
sha256sum "$BUILD/bb-probe" "$BUILD/gpu/libbbgpu.so" "$BUILD/libbbport_dlss.so" "$BUILD"/libnvidia-ngx-dlss.so.* > "$BUILD/artifacts.sha256"
