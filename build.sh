#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
BUILD=${BB_BUILD_DIR:-$PWD/out}
mkdir -p "$BUILD"
if [[ -z ${CC:-} ]]; then
    CC=$(command -v cc || command -v gcc || true)
    if [[ -z $CC ]]; then
        for candidate in /nix/store/*-gcc-wrapper-*/bin/gcc; do
            if [[ -x $candidate ]]; then CC=$candidate; break; fi
        done
    fi
fi
if [[ -z ${CC:-} ]]; then echo 'Install GCC/Clang or set CC.' >&2; exit 1; fi
# Dependencies come from pkg-config (Vulkan loader/headers, SDL3). On NixOS the
# environment is provided by shell.nix; re-enter it automatically if needed.
if ! { command -v pkg-config >/dev/null && pkg-config --exists vulkan sdl3 && command -v cmake >/dev/null && command -v ninja >/dev/null; }; then
    if [[ -z ${BB_IN_NIX_SHELL:-} ]] && command -v nix-shell >/dev/null; then
        exec env BB_IN_NIX_SHELL=1 nix-shell shell.nix --run "bash build.sh $*"
    fi
    echo 'Need pkg-config with vulkan and sdl3, cmake and ninja (see shell.nix).' >&2; exit 1
fi
read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3)"
read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3)"
# GPU library (shadPS4 video core + drivers), built by CMake into "$BUILD"/gpu/libbbgpu.so.
# BB_PGO: generate (instrumented build that writes pgo/ while the game runs), use, off.
# Default: use the profile in pgo/ when there is one. BB_LTO=OFF disables link-time optimization.
pgo=${BB_PGO:-}
if [[ -z $pgo ]]; then
    if [[ -n $(find "$BUILD/pgo" -name '*.gcda' -print -quit 2>/dev/null) ]]; then pgo=use; else pgo=off; fi
fi
mkdir -p "$BUILD/pgo"
# Submodules (git clone --recursive, or: git submodule update --init) and this port's changes
# to FSR-Vulkan (gpu/patches/fsr-vulkan), applied to its working tree once.
prepared=${NR_SOURCE_DEPS_ROOT:-}
if [[ -n $prepared ]]; then
    [[ $prepared == /* && -f $prepared/fsr-vulkan/CMakeLists.txt && -f $prepared/imgui/imgui.h && -d $prepared/LibAtrac9/C/src ]] || { echo 'Incomplete absolute prepared source root.' >&2; exit 3; }
    # The NR wrapper verifies every file before reaching this point. Never
    # initialize or patch source-tree submodules when prepared inputs are selected.
    for patch in gpu/patches/fsr-vulkan/*.patch; do
        git -C "$prepared/fsr-vulkan" apply --reverse --check "$PWD/$patch" 2>/dev/null || { echo 'Prepared FSR patch missing.' >&2; exit 3; }
    done
    atrac_source="$prepared/LibAtrac9"
else
    if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h ]]; then
        git submodule update --init --recursive
    fi
    # Issue #124: a checkout updated from an earlier version keeps that version's patch
    # applied, and the new one applies neither way. The submodule tree holds only these
    # patches, so reset it and apply the current set again.
    fsr_patches_apply() {
        for patch in gpu/patches/fsr-vulkan/*.patch; do
            if ! git -C gpu/third_party/fsr-vulkan apply --reverse --check "$PWD/$patch" 2>/dev/null; then
                git -C gpu/third_party/fsr-vulkan apply "$@" "$PWD/$patch" || return 1
            fi
        done
    }
    if ! fsr_patches_apply --check 2>/dev/null; then
        echo 'FSR-Vulkan: an earlier version of the port changed it; resetting it and patching again'
        git -C gpu/third_party/fsr-vulkan checkout -- .
    fi
    fsr_patches_apply
    atrac_source=third_party/LibAtrac9
fi
includes+=("-I$atrac_source/C/src")
cmake -S gpu -B "$BUILD"/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBB_PGO="$pgo" \
    -DBB_ONLINE_DEPS_ROOT="${BB_ONLINE_DEPS_ROOT:-}" -DBB_LTO="${BB_LTO:-ON}" -DBB_PGO_DIR="$BUILD/pgo" -DBB_PREPARED_SOURCE_DEPS_ROOT="$prepared" > "$BUILD"/gpu-cmake.log
grep '^-- Online module' "$BUILD"/gpu-cmake.log || true
echo "GPU library: PGO $pgo, LTO ${BB_LTO:-ON}"
# A failed GPU build must stop here: an older libbbgpu.so would otherwise be used silently.
if ! ninja -j "${BB_BUILD_JOBS:-8}" -C "$BUILD"/gpu bbgpu \
        $(grep -q "^build bbnet:" "$BUILD"/gpu/build.ninja && echo bbnet) \
        $(grep -q "^build bbnet20:" "$BUILD"/gpu/build.ninja && echo bbnet20) \
        > "$BUILD"/gpu-build.log 2>&1; then
    grep -v '^\[' "$BUILD"/gpu-build.log | tail -40 >&2
    echo "GPU library build failed (full log: \"$BUILD/gpu-build.log\")" >&2; exit 1
fi
# Optional DLSS (NVIDIA RTX): DLSS_SDK_ROOT=<github.com/NVIDIA/DLSS checkout> builds the bridge
# (gpu/dlss_bridge, the only code using NVIDIA's SDK) and puts it next to bb-probe with NVIDIA's
# libnvidia-ngx-dlss.so. Without them the DLSS upscaler is listed as unavailable.
if [[ -n ${DLSS_SDK_ROOT:-} ]]; then
    cmake -S gpu/dlss_bridge -B "$BUILD"/dlss-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DDLSS_SDK_ROOT="$DLSS_SDK_ROOT" >/dev/null
    ninja -j "${BB_BUILD_JOBS:-8}" -C "$BUILD"/dlss-bridge >/dev/null
    rm -f "$BUILD"/libnvidia-ngx-dlss.so.*
    cp "$BUILD"/dlss-bridge/libbbport_dlss.so "$DLSS_SDK_ROOT"/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.* "$BUILD"/
    cp "$DLSS_SDK_ROOT/LICENSE.txt" "$BUILD"/NVIDIA-DLSS-LICENSE.txt
    echo "DLSS bridge: "$BUILD"/libbbport_dlss.so"
fi
# $ORIGIN/gpu: packaged copies keep the library next to the binary without patching it.
gpu=(-L"$BUILD"/gpu -lbbgpu -Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$BUILD/gpu" -rdynamic)
runtime=(src/runtime*.c)
# Third-party decoders: compiled once, without this project's -Werror policy.
atrac9=("$atrac_source"/C/src/*.c)
if [[ ! -f "$BUILD"/libatrac9.a || -n $(find "$atrac_source/C/src" -newer "$BUILD"/libatrac9.a -name '*.c') ]]; then
    rm -rf "$BUILD"/atrac9 && mkdir -p "$BUILD"/atrac9
    for source in "${atrac9[@]}"; do "$CC" -std=c99 -O2 -g -w -c "$source" -o "$BUILD/atrac9/$(basename "${source%.c}").o"; done
    ar rcs "$BUILD"/libatrac9.a "$BUILD"/atrac9/*.o
fi
"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -no-pie "${includes[@]}" -I. -Isrc src/probe.c "${runtime[@]}" src/vulkan_smoke.c "$BUILD"/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o "$BUILD"/bb-probe
echo "Built $BUILD/bb-probe"
# GPU check for run.sh (live_resolution=auto) and the launcher's gamepad list (--gamepads).
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "${includes[@]}" tools/gpu_capabilities.c "${libraries[@]}" -o "$BUILD"/bb-gpu-capabilities
if [[ ${1:-} == --test ]]; then
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_pad.c "${libraries[@]}" -lm -o "$BUILD"/pad-test
    "$BUILD"/pad-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_runtime.c "${runtime[@]}" "$BUILD"/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o "$BUILD"/runtime-test
    "$BUILD"/runtime-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -Isrc tests/test_file_mods.c -o "$BUILD"/file-mods-test
    "$BUILD"/file-mods-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_sema.c "${runtime[@]}" "$BUILD"/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o "$BUILD"/sema-test
    "$BUILD"/sema-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_online.c "${runtime[@]}" "$BUILD"/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o "$BUILD"/online-test
    if [[ -f "$BUILD/gpu/libbbnet.so" ]]; then
        "$BUILD"/online-test "$BUILD/gpu/libbbnet.so"
        "$BUILD"/online-test "$BUILD/gpu/libbbnet20.so"
    else
        "$BUILD"/online-test
    fi
    "$CC" -std=c11 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -I. -Isrc tests/test_content.c src/runtime_content.c -o "$BUILD"/content-test
    "$BUILD"/content-test
    "$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I. -Isrc tests/test_dlc_mount.c src/runtime_content.c -o "$BUILD"/dlc-mount-test
    "$BUILD"/dlc-mount-test
fi
