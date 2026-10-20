#!/usr/bin/env bash
# Build C0 in the isolated game-PoC worktree using only read-only local dependencies.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
POC_ROOT="${BB_GAME_POC_ROOT:-$HOME/Code/nrbridge-design/game-poc-20261011}"
STABLE="${BBPORT_STABLE_ROOT:-$HOME/Code/bloodborne_pc}"
DEPS="$STABLE/out/deps"
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
RUN="$POC_ROOT/bbport/runs/c0-build-$STAMP"
mkdir -p "$RUN"
chmod 700 "$RUN"
[[ -f "$DEPS/bbport-env.sh" && -d "$DEPS/dlss-sdk" ]] || {
    echo "C0_BUILD BLOCKED missing local read-only dependency root: $DEPS" | tee "$RUN/build.log"
    exit 3
}
[[ -f "$STABLE/gpu/third_party/fsr-vulkan/CMakeLists.txt" &&
   -f "$STABLE/gpu/third_party/imgui/imgui.h" &&
   -f "$STABLE/third_party/LibAtrac9/C/src/libatrac9.c" ]] || {
    echo "C0_BUILD BLOCKED required pinned source dependencies unavailable under $STABLE" | tee "$RUN/build.log"
    exit 3
}
DEPS_COPY="$RUN/dependencies"
mkdir -p "$DEPS_COPY"
tree_manifest() {
    local source=$1 manifest=$2
    (cd "$source" && find . -type f ! -path './.git' -print0 | sort -z | xargs -0 sha256sum) > "$manifest"
}
copy_dependency() {
    local rel=$1 name=$2 source="$STABLE/$1" copy="$DEPS_COPY/$2-src"
    mkdir -p "$copy"
    cp -a "$source/." "$copy/"
    rm -rf "$copy/.git"
    local head
    head=$(git -C "$source" rev-parse HEAD)
    tree_manifest "$source" "$DEPS_COPY/$name-source-content.sha256"
    tree_manifest "$copy" "$DEPS_COPY/$name-copy-content.sha256"
    printf '%s\n' "$head" > "$DEPS_COPY/$name-HEAD.txt"
    printf '%s  %s\n' "$(sha256sum "$DEPS_COPY/$name-source-content.sha256" | awk '{print $1}')" "$name-source-content.sha256" >> "$DEPS_COPY/dependency-pins.txt"
    printf '%s  %s\n' "$(sha256sum "$DEPS_COPY/$name-copy-content.sha256" | awk '{print $1}')" "$name-copy-content.sha256" >> "$DEPS_COPY/dependency-pins.txt"
    printf -v "${name//-/_}_COPY" '%s' "$copy"
}
copy_dependency gpu/third_party/fsr-vulkan fsr-vulkan
copy_dependency gpu/third_party/imgui imgui
copy_dependency third_party/LibAtrac9 LibAtrac9
for patch in "$WT"/gpu/patches/fsr-vulkan/*.patch; do
    if ! git -C "$fsr_vulkan_COPY" apply --reverse --check "$patch" >/dev/null 2>&1; then
        git -C "$fsr_vulkan_COPY" apply --check "$patch" >/dev/null 2>&1 || {
            echo "C0_BUILD BLOCKED private FSR copy cannot accept expected patch: $patch" | tee "$RUN/build.log"
            exit 3
        }
        git -C "$fsr_vulkan_COPY" apply "$patch"
    fi
done
tree_manifest "$fsr_vulkan_COPY" "$DEPS_COPY/fsr-vulkan-patched-content.sha256"
printf '%s  %s\n' "$(sha256sum "$DEPS_COPY/fsr-vulkan-patched-content.sha256" | awk '{print $1}')" fsr-vulkan-patched-content.sha256 >> "$DEPS_COPY/dependency-pins.txt"
links=()
restore_links() {
    for path in "${links[@]}"; do
        if [[ -L $path ]]; then rm -- "$path"; fi
        mkdir -p -- "$path"
    done
}
trap restore_links EXIT
link_source() {
    local rel=$1 src=$2 dst="$WT/$1"
    [[ ! -e $dst || ( -d $dst && -z $(find "$dst" -mindepth 1 -maxdepth 1 -print -quit) ) ]] || {
        echo "C0_BUILD BLOCKED expected empty gitlink mountpoint: $dst" >&2; exit 3;
    }
    rmdir -- "$dst" 2>/dev/null || true
    ln -s -- "$src" "$dst"
    links+=("$dst")
}
link_source gpu/third_party/fsr-vulkan "$fsr_vulkan_COPY"
link_source gpu/third_party/imgui "$imgui_COPY"
link_source third_party/LibAtrac9 "$LibAtrac9_COPY"
{
    printf 'worktree=%s\n' "$WT"
    printf 'worktree_head=%s\n' "$(git -C "$WT" rev-parse HEAD)"
    printf 'stable_dependency_root=%s\n' "$STABLE"
    printf 'deps_root=%s\n' "$DEPS"
    printf 'private_source_copies=fsr-vulkan,imgui,LibAtrac9\ndependency_pins=%s\n' "$DEPS_COPY/dependency-pins.txt"
    printf 'command=source %q && BB_LTO=OFF BB_PGO=off bash %q/build.sh\n' "$DEPS/bbport-env.sh" "$WT"
    printf 'network_or_install=forbidden\n'
    sha256sum "$WT/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp" \
        "$WT/gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.h" \
        "$WT/gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.h" \
        "$WT/tools/nr_probe/build_c0.sh" "$WT/tools/nr_probe/run_game_c0.sh" \
        > "$RUN/source-sha256.txt"
    cat "$DEPS_COPY/dependency-pins.txt"
    for rel in gpu/third_party/fsr-vulkan gpu/third_party/imgui third_party/LibAtrac9; do
        printf '%s=' "$rel"
        git -C "$STABLE/$rel" rev-parse HEAD
    done
} > "$RUN/commands.txt"
set +e
(
    cd -- "$WT"
    source "$DEPS/bbport-env.sh"
    export PKG_CONFIG_PATH="$DEPS/root/usr/lib/pkgconfig:$DEPS/root/usr/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export BB_LTO=OFF BB_PGO=off DLSS_SDK_ROOT="$DEPS/dlss-sdk"
    if ! command -v pkg-config >/dev/null || ! pkg-config --exists vulkan sdl3 ||
       ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
        echo 'C0_BUILD BLOCKED local pkg-config vulkan/sdl3, cmake, or ninja unavailable'
        exit 3
    fi
    printf 'C0_BUILD START worktree=%s\n' "$WT"
    printf 'C0_BUILD pkgconfig vulkan=%s sdl3=%s\n' "$(pkg-config --modversion vulkan)" "$(pkg-config --modversion sdl3)"
    bash build.sh
) > "$RUN/build.log" 2>&1
rc=$?
set -e
{
    printf '{\n  "status": "%s",\n  "exit_code": %d,\n  "worktree": "%s",\n  "head": "%s",\n  "build_log": "%s",\n  "commands": "%s",\n  "source_manifest": "%s"\n}\n' \
        "$([[ $rc -eq 0 ]] && echo PASS || { [[ $rc -eq 3 ]] && echo BLOCKED || echo FAIL; })" "$rc" "$WT" \
        "$(git -C "$WT" rev-parse HEAD)" "$RUN/build.log" "$RUN/commands.txt" \
        "$RUN/source-sha256.txt"
} > "$RUN/build-result.json"
if [[ $rc -eq 0 ]]; then
    sha256sum "$WT/out/bb-probe" "$WT/out/gpu/libbbgpu.so" "$WT/out/libbbport_dlss.so" \
        "$WT"/out/libnvidia-ngx-dlss.so.* > "$RUN/artifact-sha256.txt"
    cat "$RUN/artifact-sha256.txt"
else
    tail -80 "$RUN/build.log" >&2
fi
printf 'C0_BUILD_RESULT=%s exit=%d run=%s\n' "$([[ $rc -eq 0 ]] && echo PASS || { [[ $rc -eq 3 ]] && echo BLOCKED || echo FAIL; })" "$rc" "$RUN"
exit "$rc"
