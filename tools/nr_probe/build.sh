#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
args=()
[[ -z ${VULKAN_INCLUDE:-} ]] || args+=("-DVulkan_INCLUDE_DIR=$VULKAN_INCLUDE")
[[ -z ${VULKAN_LIBRARY:-} ]] || args+=("-DVulkan_LIBRARY=$VULKAN_LIBRARY")
cmake -S tools/nr_probe -B out/nr-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug "${args[@]}"
cmake --build out/nr-probe
if [[ -n ${DLSS_SDK_ROOT:-} ]]; then
    cmake -S gpu/dlss_bridge -B out/dlss-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "-DDLSS_SDK_ROOT=$DLSS_SDK_ROOT" "-DVULKAN_INCLUDE=${VULKAN_INCLUDE:-}"
    cmake --build out/dlss-bridge
fi
