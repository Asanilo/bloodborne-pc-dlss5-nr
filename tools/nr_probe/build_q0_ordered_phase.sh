#!/usr/bin/env bash
# Opt-in Q0 slow-wait diagnostic BUILD_ONLY; original Q0 source/artifact evidence is preserved.
# Reuses the audited offline copier; never starts the game or GPU.
set -euo pipefail
WT=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
POC_ROOT=${BB_GAME_POC_ROOT:-/home/arinp/Code/nrbridge-design/game-poc-20261011}
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
RUN="$POC_ROOT/bbport/runs/q0-ordered-phase-build-$STAMP"
mkdir -m 700 -p "$RUN"
SOURCE_LIST=(
    gpu/CMakeLists.txt
    gpu/shadps4/nlohmann/json.hpp
    gpu/shadps4/nlohmann/LICENSE.MIT
    gpu/shadps4/video_core/host_shaders/c1_game_bridge.comp
    gpu/shadps4/video_core/host_shaders/c2_game_bridge.comp
    gpu/shadps4/video_core/renderer_vulkan/c1_game_bridge.cpp
    gpu/shadps4/video_core/renderer_vulkan/c1_game_bridge.h
    gpu/shadps4/video_core/renderer_vulkan/c1_noop_process.cpp
    gpu/shadps4/video_core/renderer_vulkan/c1_noop_process.h
    gpu/shadps4/video_core/renderer_vulkan/c2_game_bridge.cpp
    gpu/shadps4/video_core/renderer_vulkan/c2_game_bridge.h
    gpu/shadps4/video_core/renderer_vulkan/c2_nr_process.cpp
    gpu/shadps4/video_core/renderer_vulkan/c2_nr_process.h
    gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.h
    gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp
    gpu/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.h
    gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp
    gpu/shadps4/video_core/renderer_vulkan/vk_presenter.h
    tools/nr_probe/build_c0.sh
    tools/nr_probe/build_c1.sh
    tools/nr_probe/build_c2.sh
    tools/nr_probe/build_c3.sh
    tools/nr_probe/run_game_c3.sh
    gpu/shadps4/video_core/host_shaders/c3_game_bridge.comp
    gpu/shadps4/video_core/host_shaders/c3_frame_validate.comp
    gpu/shadps4/video_core/renderer_vulkan/c3_nr_process.cpp
    gpu/shadps4/video_core/renderer_vulkan/c3_nr_process.h
    gpu/shadps4/video_core/renderer_vulkan/c3_game_bridge.cpp
    gpu/shadps4/video_core/renderer_vulkan/c3_game_bridge.h
    gpu/shadps4/video_core/renderer_vulkan/c3_frame_validate.h
    tools/nr_probe/run_game_c0.sh
    tools/nr_probe/run_game_c1.sh
    tools/nr_probe/run_game_c2.sh
    tools/nr_probe/build_c4.sh
    tools/nr_probe/build_q0.sh
    tools/nr_probe/build_q0_wait_diag.sh
    tools/nr_probe/run_game_q0_wait_diag.sh
    gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.h
    tools/nr_probe/build_q0_wait_state.sh
    tools/nr_probe/build_q0_ordered_phase.sh
    tools/nr_probe/run_game_q0_wait_state.sh
    gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.cpp
    tools/nr_probe/run_game_q0.sh
    gpu/shadps4/video_core/renderer_vulkan/vk_camera_motion.cpp
    gpu/shadps4/video_core/renderer_vulkan/vk_scene_resolution.cpp
    gpu/shadps4/video_core/renderer_vulkan/vk_scene_resolution.h
    tools/nr_probe/run_game_c4.sh
    gpu/shadps4/video_core/host_shaders/c4_game_bridge.comp
    gpu/shadps4/video_core/renderer_vulkan/c4_nr_process.cpp
    gpu/shadps4/video_core/renderer_vulkan/c4_nr_process.h
    gpu/shadps4/video_core/renderer_vulkan/c4_game_bridge.cpp
    gpu/shadps4/video_core/renderer_vulkan/c4_game_bridge.h
    gpu/shadps4/video_core/renderer_vulkan/c4_frame_validate.h
)
# Include all actual modified tracked inputs, not just the new bridge shortlist.
while IFS= read -r -d '' rel; do
    [[ -f "$WT/$rel" ]] && SOURCE_LIST+=("$rel")
done < <(git -C "$WT" diff --name-only -z HEAD)
mapfile -t SOURCE_LIST < <(printf '%s\n' "${SOURCE_LIST[@]}" | sort -u)
for rel in "${SOURCE_LIST[@]}"; do
    [[ -f "$WT/$rel" ]] || { echo "Q0_BUILD BLOCKED missing source: $rel" >&2; exit 3; }
done
JSON_SHA=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
[[ $(sha256sum "$WT/gpu/shadps4/nlohmann/json.hpp" | awk '{print $1}') == "$JSON_SHA" ]] || {
    echo 'Q0_BUILD BLOCKED vendored nlohmann header pin mismatch' >&2; exit 3;
}
source_manifest() {
    local output=$1
    : > "$output"
    for rel in "${SOURCE_LIST[@]}"; do sha256sum "$WT/$rel" >> "$output"; done
}
source_manifest "$RUN/source-sha256.txt"
mkdir -p "$RUN/source-snapshot"
for rel in "${SOURCE_LIST[@]}"; do
    mkdir -p "$RUN/source-snapshot/$(dirname "$rel")"
    cp -a "$WT/$rel" "$RUN/source-snapshot/$rel"
done
mkdir -p "$RUN/pre-q0-ordered-phase-out"
for artifact in "$WT/out/bb-probe" "$WT/out/gpu/libbbgpu.so" "$WT/out/libbbport_dlss.so" "$WT"/out/libnvidia-ngx-dlss.so.*; do
    rel=${artifact#"$WT/"}
    if [[ -f "$WT/$rel" ]]; then
        mkdir -p "$RUN/pre-q0-ordered-phase-out/$(dirname "$rel")"
        cp -a "$WT/$rel" "$RUN/pre-q0-ordered-phase-out/$rel"
        sha256sum "$WT/$rel" >> "$RUN/pre-q0-ordered-phase-artifact-sha256.txt"
    fi
done
{
    printf 'scope=Q0_ORDERED_PHASE_BUILD_ONLY_NO_GPU\nworktree=%s\nhead=%s\n' "$WT" "$(git -C "$WT" rev-parse HEAD)"
    printf 'command=bash %s/tools/nr_probe/build_c0.sh (audited offline dependency copier; no install/network)\n' "$WT"
    printf 'canonical_contract=%s\ncanonical_contract_sha256=%s\n' \
        "$POC_ROOT/integration/QUALITY-CAPTURE.md" "$(sha256sum "$POC_ROOT/integration/QUALITY-CAPTURE.md" | awk '{print $1}')"
    printf 'diagnostic_opt_in=BB_Q0_WAIT_STATE_DIAG=1; BB_Q0_SLOW_WAIT_DIAG unset (explicit diagnostic wrapper/Root child only; original launcher does not set it)\n'
    printf 'helper_runtime=NOT_STARTED; Q0 capture-only/no Wine or model\n'
    printf 'helper_runtime=NOT_STARTED\ngame=NOT_RUN\nGPU=NOT_RUN\nstable_repository_write=none\n'
} > "$RUN/commands.txt"
set +e
bash "$WT/tools/nr_probe/build_c0.sh" > "$RUN/build.log" 2>&1
rc=$?
set -e
source_manifest "$RUN/source-sha256.post.txt"
if ! cmp -s "$RUN/source-sha256.txt" "$RUN/source-sha256.post.txt"; then
    echo 'Q0_BUILD FAIL source input changed during build' >> "$RUN/build.log"
    rc=2
fi
if [[ $rc -eq 0 ]]; then
    sha256sum "$WT/out/bb-probe" "$WT/out/gpu/libbbgpu.so" \
        "$WT/out/libbbport_dlss.so" "$WT"/out/libnvidia-ngx-dlss.so.* > "$RUN/artifact-sha256.txt"
fi
python3 - "$RUN" "$WT" "$rc" <<'PY'
import json, pathlib, subprocess, sys
run, wt, code = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), int(sys.argv[3])
underlying = None
for path in sorted(run.parent.glob("c0-build-*/build-result.json"),
                   key=lambda p: p.stat().st_mtime, reverse=True):
    data = json.loads(path.read_text())
    if pathlib.Path(data.get("worktree", "")) == wt and data.get("status") in ("PASS", "FAIL", "BLOCKED"):
        underlying = str(path)
        break
result = {
    "status": "PASS" if code == 0 else "BLOCKED" if code == 3 else "FAIL",
    "scope": "Q0_ORDERED_PHASE_BUILD_ONLY_NO_GPU", "C4_poll_baseline_commit": "527b827077a411cc9d8442ddfa9a772136cca064", "exit_code": code,
    "worktree": str(wt),
    "head": subprocess.check_output(["git", "-C", str(wt), "rev-parse", "HEAD"], text=True).strip(),
    "build_log": str(run / "build.log"), "offline_builder_evidence": underlying,
    "source_manifest": str(run / "source-sha256.txt"),
    "source_snapshot": str(run / "source-snapshot"),
    "pre_q0_wait_diag_artifacts_preserved": str(run / "pre-q0-ordered-phase-out"),
    "artifact_manifest": str(run / "artifact-sha256.txt") if (run / "artifact-sha256.txt").exists() else None,
    "gpu_status": "NOT_RUN", "game_status": "NOT_RUN",
}
(run / "build-result.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
PY
exit "$rc"
