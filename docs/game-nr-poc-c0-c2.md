> Historical note: local paths and machine identifiers are redacted for publication.
> Original evidence is preserved in private development data; this is not a new acceptance run.
> Current player instructions are in the project README.

# Bloodborne native DLSS NR game PoC baseline (C0–C2)

This document records the accepted renderer-side baseline on branch `poc/nr-game-20261011`. All changes are isolated to this PoC branch. Game assets, private profiles/saves, captures, build outputs, helper binaries, model files, and run logs are not part of this source commit.

## Accepted stages

- **C0 capture:** the 1080p DLAA HDR `Run()` path is observed after successful SR and before later scene/UI merge. The C0-only motion-image usage includes `TRANSFER_SRC` so the existing deferred dump copy is valid. The usage-fix capture observed finite dumps for the real color/depth/motion/SR resources. Guide spatial phase remains unverified.
- **C1 transport/no-op:** one actual game frame is round-tripped through the four shared images and selected through the existing merge route. The independent return image matches the immutable SR fallback for the accepted no-op frame. A separate timeout case confirmed SR fallback and quarantined outstanding resources; its 10-second diagnostic wait is not a real-time performance result.
- **C2 single-frame NR:** one real 1080p, 1:1, HDR, zero-jitter frame ran the production NR helper with one Evaluate and one Resolve. A separate `skipEvaluate` control was rejected by the same output-acceptance predicate and retained SR fallback. Both cases have source-frame, renderer-tick, presentation, readback, digest, and resource-lifecycle evidence in the external experiment record.

## Reproduction and evidence

Offline build entry points are `tools/nr_probe/build_c0.sh`, `build_c1.sh`, and `build_c2.sh`; private launch entry points are `run_game_c0.sh`, `run_game_c1.sh`, and `run_game_c2.sh`. These scripts use the already provisioned local dependency tree and do not install dependencies. Game execution is intentionally delegated to the integration runner, which provides the GPU lock, private profile, bounded process-group watchdog, and evidence capture.

The authoritative experiment record is maintained outside this source repository at `${HOME}/Code/nrbridge-design/game-poc-20261011/`. At this baseline, `integration/c2-final-audit.json` is PASS and `integration/c2-post-run-pins.json` verifies 104 frozen items. Build evidence is `bbport/runs/c2-build-20261011T092138Z/build-result.json`; the single and skip case audits are referenced by `c2-final-audit.json`.

## Scope limits

This baseline proves a single visible NR frame and its skip control. It does not prove continuous/resident NR, nonzero-jitter guide alignment, live exposure, image quality, frame-time performance, UI-specific quality, or external GPU semaphores. The accepted C2 profile is 1080p DLAA 1:1, HDR, zero jitter, and manual white point 1 with live exposure unknown. C3 must add continuous-frame evidence without changing the stable renderer or treating old/stale outputs as valid.
