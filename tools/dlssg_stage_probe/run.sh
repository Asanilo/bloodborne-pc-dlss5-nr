#!/usr/bin/env bash
# Offscreen Ada geometry probe for the stock Linux 310.9.1 DLSS-G module.
set -euo pipefail
src=$(cd -- "$(dirname -- "$0")" && pwd -P)
gitdir=$(git -C "$src" rev-parse --path-format=absolute --git-common-dir)
out=$(dirname -- "$gitdir")/out
sdk=${DLSS_SDK_ROOT:-$out/deps/dlss-sdk}
module="$sdk/lib/Linux_x86_64/rel/libnvidia-ngx-dlssg.so.310.9.1"

mkdir -p -- "$out/native-mfg"
scratch=$(mktemp -d -- "$out/native-mfg/geometry.XXXXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
git -C "$src" show 3630bc52f19babf896447c2f3fc7db2b2a5c37c6:tools/nr_probe/vk_test_context.h > "$scratch/vk_test_context.h"
tmpheaders="$out/deps/root/usr/include"
"${CXX:-c++}" -std=c++20 -O2 -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -I"$tmpheaders" -I"$scratch" "$src/blend.cpp" -lvulkan -ldl -o "$scratch/color"
python3 -B "$src/prepare.py" "$module" "$scratch/blend.cubin" --kind blend
python3 -B "$src/prepare.py" "$module" "$scratch/weights.cubin" --kind weights

mkdir -m 700 -- "$scratch/fields" "$scratch/cache"
export CUDA_CACHE_DISABLE=1 XDG_CACHE_HOME="$scratch/cache" __GL_SHADER_DISK_CACHE_PATH="$scratch/cache"
exec {gpu_lock}>/tmp/nrbridge-poc-gpu.lock
flock -x "$gpu_lock"
python3 -B "$src/scatter_fields.py" "$module" "$scratch/fields"
"$scratch/color" --launch "$scratch/blend.cubin" "$scratch/fields" "$scratch/weights.cubin"
