# 私有试玩与开发入口

从 `/home/arinp/Code/nrbridge-dev` 启动：

```bash
python3 native/tools/nr_dev/play.py
```

游戏出现后，Space/Cross 继续到可操作场景，再回终端按 Enter 开启真实 NR。WASD/鼠标或手柄操作，正常关闭游戏窗口结束。默认使用已锁定的新模型 Helper `build/helper-runtime-model/06ac7e6b7260172d/wine_runtime_model.exe`，启动时核对其 `build.json` 与 PE SHA，并在本次 manifest 记录实际身份。

- **Insert**：打开 NR 菜单。模型参数加载或调整后需要显式 **Apply**；加载配置不会自动重建模型。
- **F8**：即时切换 NR 旁路。两种输入共享 SR 历史；快速切换不构成独立双 SR 历史的同帧 A/B。
- **F9**：请求同源 raw720、edited720、Depth、MV、实际 SR1080 与后处理游戏 Present 的诊断捕获。结果需等 GPU 完成并核验 source、revision、SHA；游戏 Present 是 NR overlay 之前的像素。旁路时 edited 仍可计算，但实际 SR 输入是 raw，不可把候选图称为已显示 NR。

默认计划时长为480秒（含准备），到期主动正常关窗并执行退役审计；独立 watchdog 默认600秒，正常关闭还有受预算限制的 grace。Helper 服务容量仍为600秒。未真实退休仍为 HOLD，不能把计划到期本身解释为 GPU 故障，也不能靠 kill 声称成功。

默认每次从私有 NRTest seed 开始；正式存档不会被使用或修改。若要接着玩，只能明确选择一个已正常退休的新周期 session：

```bash
python3 native/tools/nr_dev/play.py --continue-session SESSION
```

它将该 session 的 `profile/user/savedata` 和 `nrbridge.json` 校验后复制到全新 run，保留原件；不复制 Wine prefix、缓存或正式存档。HOLD（即使另有 fresh-context resolution）不能作为续玩来源。私有进度在 `runs/SESSION/profile/user/savedata`。

主协调已在 `dev-player-01` 获得用户反馈“明显改善，操作流畅”，并核验正常退出。 用户后续补充：仍有轻微鬼影、闪烁及大幅度动作时的闪烁，整体稳定性尚可；这些是尚未解决的画质问题，不能将当前试玩通过解读为无伪影或全场景完成。120秒自动动态窗口：新版60.30 FPS/P95 25.31ms/P99 26.78ms；d9视觉算法隔离参考66.49 FPS/P95 22.19ms/P99 23.58ms。场景为Clinic转镜和小范围移动，未覆盖完整战斗与全游戏。冷诊断不计入热性能。更完整的现行安全和资源边界见开发根 `CURRENT.md`。

## 本阶段验证边界

- `dev-runtime-life-01` 提供了本阶段资源生命周期的实际运行证据；原 watchdog **HOLD 保留**，不改写为正常退出或玩家验收通过。资源作用范围的独立证据不等于完整运行 PASS。
- `dev-runtime-nr-02` 完成 **1841 个真实 job：1821 captured、20 obsolete**，已验证正常退出；同源诊断 capture 实际完成。此项证明该次模型传输、候选处理与退出链实际运行，不代替画质或性能验收。
- 新的连续区域 confidence 与 Style 参数真实 NR Feature 重建仍由主协调者验证，**待验证**。菜单可见或 CPU 测试通过不等于 GPU 行为已验收。

下面保留的旧试玩反馈及性能数字仅对应其标明的旧 session/场景，不是当前 runtime/config 版本的性能或玩家 PASS。本轮源码尚未推送。

## 开发与诊断

Build: `bash native/tools/nr_dev/build.sh`. Outputs live under `nrbridge-dev/build/native`; incremental rebuilds reuse that one directory. The source dependency copies under inputs are private and made once. SDK/sysroot versions remain the stable locked versions. No old build ancestor or execution ledger is imported.

Helper: `python3 native/tools/nr_dev/prepare_helper.py` exports the f527 quoted-include closure and compiles the wire CPU checker and PE. Only includes are relocated. The effective spec is verified against `671c7644c69de5f681b099fb4c3d402ba36f1e4538e65cd5538441e2bd47643b`. Export provenance is in `inputs/helper-f527/manifest.json`. Subsequent cold rebuilds use `prepare_helper.py --rebuild` with only exported source and locked SDK/compiler, without reading historical build ancestry.

The coordinator must first inspect current host GPU holders and driver-hang evidence, then record `inputs/host-safety.json` with current Linux `boot_id`, `unresolved_driver_hang: false`, and `evidence` referring to that real observation. This is a safety observation, not a historical approval or a launch count allowance. Runtime dependency hashes are checked using `inputs/runtime-pins.json`.

From the development root:

```
python3 native/tools/nr_dev/runner.py preflight
python3 native/tools/nr_dev/runner.py start dev-UNIQUE --mode skipEvaluate --sky off
python3 native/tools/nr_dev/runner.py scene-ready dev-UNIQUE
python3 native/tools/nr_dev/runner.py start-helper dev-UNIQUE
python3 native/tools/nr_dev/runner.py arm dev-UNIQUE
python3 native/tools/nr_dev/runner.py stop dev-UNIQUE
```

`scene-ready` records an actual coordinator/player observation of a controllable private scene; it must not be issued speculatively. `arm` checks the exact session's real Helper GPU/FD retired readiness. The same runner supports resident NR after lifecycle validation; `--sky persistent` enables `BB_NATIVE_PERSISTENT_HISTORY=1` in the new Native implementation, while the old Masked Sky mode remains `off`. GPU runs are coordinator-owned and globally flock-exclusive.

Each session has independent save copy, profile, cache, Wine prefix, generated guest, provenance and controls. Game assets remain read-only inputs; no formal installation or real save is used. Native retains tuple/UUID/PID/starttime/FD/ownership/last-use checks. Normal close is sent to a window only after its X11 PID matches the exact private game identity. Timeout creates HOLD and requests normal close, retains the GPU lock while the process remains alive, and never kills to claim completion. New-context HOLDs block subsequent sessions until independently resolved with appended evidence by the coordinator. The runner never modifies old FAIL/HOLD/STOPPED records.

`present-metrics.json` measures actual unique nonreuse swapchain Present CPU intervals (FPS/P50/P95/P99/max); it is not pixel quality acceptance. `gpu-memory.jsonl` records PID-associated NVIDIA memory samples and device-total used/total/UUID. Device totals include other applications and shared allocations; PID samples must not be summed as physical GPU total. Unavailable PID sampling is NOT_MEASURED, not zero. Review the actual terminal wire/FD audits and player quality in addition to the runner's normal-teardown result. A runtime failure remains HOLD, not BUILD_ONLY success.

`run.sh` has no `BB_OUT_DIR` knob: generated guest output is `$BB_DATA_DIR/out`, so each run builds it under its private profile. `BB_PROBE` explicitly selects `build/native/bb-probe`; its ELF rpath selects `build/native/gpu/libbbgpu.so`. The DLSS loader resolves `/proc/self/exe` to load the bridge and NGX beside that executable. The runner hashes all four native runtime products and records/checks loaded library paths on each scene/control action. `frames.csv` is a per-flip diagnostic CSV (not an authoritative nonreuse Present ledger). P95/P99 use every `NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT` event, whose producer is guarded by `is_game_frame && !is_reusing_frame` and records monotonic ns; duplicates and nonmonotonic windows are rejected.

Scene operation is separate from the NR model controls. The private seed has 15 files, 15,002,530 bytes, copied to `runs/SESSION/profile/user/savedata` (including `1/CUSA00207/SPRJ0005`). Source manifest identifies Iosefka Clinic; the CUSA03173 asset root and internal save title ID are intentionally those of the verified baseline. Launch adds a private backup via save_manager; it does not redirect to the formal save. Readiness must be judged on the live scene, not merely a loader line or seed existence.

With the focused game window, controller Cross or keyboard Space/keypad Enter confirms menus; Enter/Escape is Options (not Cross). Continue the private Clinic save from the title menu. WASD moves, arrow keys/mouse look; Shift is Circle, E is Square, Q is Triangle; I/K/J/L are D-pad. Use explicit observation because menu count/timing can vary. The coordinator can use bounded private pad requests instead:

```
python3 native/tools/nr_dev/runner.py input dev-UNIQUE --buttons cross --hold 0.2
python3 native/tools/nr_dev/runner.py dump dev-UNIQUE
```

`input` releases the pad in a finally block; holds are limited to five seconds and all buttons/axis ranges are validated. `dump` requests one actual completed Present into `runs/SESSION/dump`; inspect the resulting PNG and confirm a controllable Clinic character before `scene-ready`. A capture is diagnostic and should be outside the measured performance window. `BB_PRESENT_DUMP_COUNT=0` means one frame in Native's `max(1, atoi(...))`, not zero capture.

After Helper startup, normal retirement additionally audits exact terminal counts/session and each input→complete→consumed immutable origin tuple, true Native/Helper completion ticks, Native zero-owned resources, Helper zero shared/export-FD ownership, targeted FD inventory, model release/shutdown and normal waitpid. The 16384 cap is the unchanged effective wire resource budget, not an inherited four-job rule. No-peer exits require the explicit current-session absent-owner/snapshot/pending-use/preparation-join proof and absence of config/arm, rather than inferring safety from zero jobs.
