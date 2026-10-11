# C4 active completion polling milestone

Initial source milestone: `3997b73a12a5f108e5d07158438b8cac94130458`. Initial correctness PASS is retained; measured22.006FPS versus matchedSR65.366FPS fails the near60 target. Archived initial binaries/helper/contract/approval remain under `baselines/c4-first-hot-20261011/manifest.json`.

The B revision changes only `c4_game_bridge.cpp` active current-frame completion sleep2ms→1ms. Setup's2ms sleeps, native GPU completion1ms polling, GPU10s/frame30s bounds, full1080 every-frame Evaluate/resolve, shader/guard/ownership/barriers and real previous/final merge last use are unchanged. No shader, slot, pipeline, resolution or SR behaviour change. A independently owns active helper INPUT/CONSUMED polling and QPC; no A file is edited by B.

Native per-frame `completion_poll` adds requested interval1ms, file-read attempts, actual sleep count, monotonic elapsed and scope. This is a **subset** of `cpu_phase_ms.peer_completion_wait`, excluding input publication before the loop; do not add the two durations. No success printf per poll is introduced. Expiration logs poll counts before the existing tombstone/quarantine path.

Build-only `bbport/runs/c4-build-20261011T143927Z/build-result.json` PASS/exit0. All41 sources match pre/post/live/snapshot and all4 artifacts match their manifest. Native bridge SHA71806d8b5e159388c3b1e69156ba408b79c48ea938c1d137a14a15c70d0aa4ef. libbbgpu SHA`63ac6e8f8e73061979189800f46010569e932ca469620594965650f884b5aae5`. Source/artifact pins and exact unchanged scope: `bbport/c4-poll-build-delivery.json`. Commands/raw log/snapshots remain in that unique build directory; old140430 records are preserved.

Build-time runtime status was NOT_RUN. Root subsequently ran the frozen revision through hot120 and sustained1800 below. Existing five-position `run_game_c4.sh` and all GPU/model/acceptance paths remain unchanged. No new GPU or build was performed by B during this final audit.

Final read-only mutual review: A verified B41source pre/post/live/snapshot and4artifacts plus the unique13+1 bridge diff. B verified A97 unique helper source/generated/tool/dependency pins and the QPC/control-only code; no definite static blocker. A final helper `6d0039a3548d6378444db107a22f18d2a4d3a60803f4e3cdc7570497c6a13df5` (1148928B), source `8533017eb3351db8c51b85ad6749b43463b5cc7e9e66c5fa440a46357eb3ceed`, build `10d341e30be75b5f78b1762fc28a6316bc0b05087df63f3636a48f54a08e538a`, source commit `da6a50a4237cf5147d441844185793b797bf9b9d`. B compile inputs/artifacts remain frozen. Root chose no full diagnostic rerun because GPU/model/shader/guard/acceptance inputs are unchanged; hot120 and1800 separately validate actual source/Eval/Present/clock/control/lifecycle.

Root poll120 regression finished; B independently verified all120 real sources2178..2297, exact input/complete/native/consumed identity/history/reset, actual Evaluate/result1/resolve120, no gaps, guard0/4B, and all120 unique merge/bind/prepare/non-reuse selected NR Presents. Native draw input<output<=merge=prepare=ready; Present scheduler ticks are not compared. QPC frame_elapsed is positive on all120 frames (zero values0, previous coarse clock81). Mean native completion attempts17.892/sleeps16.892, active loop17.959ms is a subset of peer wait18.070ms. Helper active poll interval1ms is actual telemetry; consumed wait mean3.743ms. Helper queue14.316ms remains similar to initial14.282ms; no inference work was removed. Four original shared payloads (FD80..83, device12/inode9577..9580) are absent from Wine active-retired/final and native final inventories. Wine83 and native136 complete final inventories each exactly equal their own baselines. Normal helper wait0 and outer controlled return0 with empty owned group/private prefix.

Actual all119 selected NR Present intervals give23.585378FPS, mean42.399150ms/p9544.924892ms versus initial22.006110FPS/mean45.441924ms. Polling reduced measured mean interval by3.042774ms, but **near60 target remains FAIL**. Hot pixel quality/signatures remain NOT_RUN; no full-pixel claim follows the guard. Initial full diagnostic/skip evidence is retained and was not rerun under Root's unchanged-GPU/acceptance pin review policy.

## Sustained1800: scoped functional acceptance, performance target FAIL

Root case `bbport/runs/c4-game-c4-play1800-20261011`; outer `runs/hot-nr-20261011T144743.345911Z`. Root's explicit acceptance is **PASS_C4_1800_FRAME_FUNCTIONAL_RUN_WITH_FD_OBSERVATION**; near60 **FAIL** and whole C4 **INCOMPLETE**. This is not a playable-performance or quality acceptance.

B independently checked all1800 input/complete/native/consumed records with duplicate-key rejection and exact active tuple/profile/mode/history/reset. Sources2186..3985 are consecutive; first NRreset1 then1799 reset0. Actual SR initial reset is recorded at source1711; selected sources retain actualSRreset0. All1800 frames execute Evaluate/result1/resolve1 with matching cumulative counts; hot scopes stay NOT_CHECKED/NOT_RUN, zero helper pixel readback, native fused range guard0 with4B readback. Every accepted frame has one unique merge/prepared/non-reuse Present; draw input tick<output tick<=merge<=prepare<=ready, observed present frame=source+1 and swapchain_present1. Present scheduler tick numbers are independent and not compared to draw ticks. No Quarantine/expiry/association rejection occurred. This audit does not replace full-pixel validation.

Terminal frame1800/source3985 is consistent through release/retired/audit; last merge completion is logged. `wine-result.json` records1800/1800 calls and normal release; `native-helper-exit.json` records normal wait0. Outer controlled return0, owned-group members and private-prefix members are empty. Root viewed three actual Clinic screenshots showing camera/character movement; B relies on Root's visual review, not a quality acceptance.

**FD caveat:** native baseline/final complete136-entry inventories are identical. Wine83→84 retains all83 baseline entries but adds readable fd80, device55/inode1511805, pointing at this run directory. Snapshot occurred while helper waited for final audit. Origin/cache/leak causation is not proved; Wine whole inventory equality is false, and long-term leak freedom is not claimed. Four exported `/dmabuf:` payloads device12/inodes9624..9627 (original numeric FDs80..83) are absent from Wine active-retired/final and native final inventories. The reused final numeric slot80 is a distinct directory identity, not the original GPU allocation.

Predetermined warmup is f0..119; static scoring f120..899 preserves all779 intervals for780 outputs. Static20.893907FPS, mean47.860844ms/p9552.553190ms. Whole1800 outputs yield1799 intervals over89.511712s:20.097929FPS, mean49.756371ms/p9556.824992ms. No slow interior intervals were removed. Whole includes movement and is not substituted for the static window. All are CPU-observed selected NR Present-call completions, not input-to-photon or a pure model benchmark.

Readonly independently computed trend (per-window interval count is outputs−1; bin boundaries do not replace the full-window score):

| NR frame window | FPS | Mean interval ms | Input wait ms | Peer wait ms | Helper GPU ms | Publish ms | Consumed wait ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0..119 | 23.151 | 43.196 | 12.193 | 18.227 | 14.328 | 0.661 | 3.869 |
| 120..299 | 22.448 | 44.548 | 12.176 | 19.866 | 14.347 | 1.075 | 4.244 |
| 300..599 | 21.179 | 47.217 | 12.379 | 22.303 | 14.355 | 1.778 | 4.342 |
| 600..899 | 19.802 | 50.499 | 12.553 | 25.443 | 14.353 | 2.686 | 5.127 |
| 900..1199 | 20.272 | 49.329 | 9.870 | 28.508 | 14.324 | 3.498 | 6.625 |
| 1200..1499 | 19.171 | 52.162 | 9.691 | 31.678 | 14.360 | 4.425 | 8.263 |
| 1500..1799 | 18.088 | 55.285 | 9.641 | 35.752 | 14.338 | 5.356 | 9.401 |

Helper GPU stays about14.35ms while peer wait and JSON atomic publication rise across frame index. Movement changes game/input-wait costs, so do not call this a controlled directory-growth experiment. `read_active_control` checks missing native.abort and opens frame-specific files; completion publication serializes JSON and performs atomic write. Wine case-insensitive missing-path scans are a source-supported **hypothesis**, not established by these timings. `consumed_wait` and helper input wait include actual producer/consumer work and overlap native phases; do not sum them as independent serial latency. Requested1ms poll intervals are not measured1ms total file-operation cost. A separate no-GPU directory-size control would be needed to prove causation; none was run by B.

Small machine-readable index and exact evidence hashes: `docs/game-nr-poc-c4-poll-evidence.json`. No new source optimization, pipeline/slot change, GPU test or build followed this audit. This milestone commits only the already-built13+1 polling diff and these two text documents. Stable repository remains clean; existing C1 logs and large/binary/private artifacts are excluded.
