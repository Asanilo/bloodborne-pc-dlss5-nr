# 私有试玩与开发入口

**当前GPU试玩仍暂停（2026-10-16 Xid31）。** 当前实际build为a65e230，含TSC并发初始化修复及默认关闭的资源/寄存器诊断。重启后dev-postreboot-sr-01与dev-loading-trace-01均SR-only正常退出，后者已确认加载、转镜和角色位移，无新驱动错误；尚未恢复Helper/NR验收，间歇性故障和亮尾保持OPEN。原HOLD保留，下面的普通试玩入口仍被安全检查阻断。

从 `/home/arinp/Code/nrbridge-dev` 启动：

```bash
python3 native/tools/nr_dev/play.py
```

可选 `--native-build /home/arinp/Code/nrbridge-dev/build/baselines/<完整提交SHA>` 使用已校验的固定 Alpha；默认仍为 `build/native`。入口预校验四产物与构建来源，Helper 独立选择不变。

游戏出现后，Space/Cross 继续到可操作场景，再回终端按 Enter 开启真实 NR。WASD/鼠标或手柄操作，正常关闭游戏窗口结束。默认使用已锁定的新模型 Helper `build/helper-runtime-model/06ac7e6b7260172d/wine_runtime_model.exe`，启动时核对其 `build.json` 与 PE SHA，并在本次 manifest 记录实际身份。

- **Delete**：打开 NR 菜单。模型参数加载或调整后需要显式 **Apply**；加载配置不会自动重建模型。
- **F8**：即时切换 NR 旁路。两种输入共享 SR 历史；快速切换不构成独立双 SR 历史的同帧 A/B。
- **F9**：请求同源 raw720、edited720、Depth、MV、实际 SR1080 与后处理游戏 Present 的诊断捕获。结果需等 GPU 完成并核验 source、revision、SHA；游戏 Present 是 NR overlay 之前的像素。旁路时 edited 仍可计算，但实际 SR 输入是 raw，不可把候选图称为已显示 NR。

默认计划时长为480秒（含准备），到期主动正常关窗并执行退役审计；独立 watchdog 默认600秒，正常关闭还有受预算限制的 grace。Helper 服务容量仍为600秒。未真实退休仍为 HOLD，不能把计划到期本身解释为 GPU 故障，也不能靠 kill 声称成功。

默认每次从私有 NRTest seed 开始；正式存档不会被使用或修改。若要接着玩，只能明确选择一个已正常退休的新周期 session：

```bash
python3 native/tools/nr_dev/play.py --continue-session SESSION
```

它将该 session 的 `profile/user/savedata` 和 `nrbridge.json` 校验后复制到全新 run，保留原件；不复制 Wine prefix、缓存或正式存档。HOLD（即使另有 fresh-context resolution）不能作为续玩来源。私有进度在 `runs/SESSION/profile/user/savedata`。

早期Alpha的主协调已在 `dev-player-01` 获得用户反馈“明显改善，操作流畅”，并核验正常退出。 用户后续补充：仍有轻微鬼影、闪烁及大幅度动作时的闪烁，整体稳定性尚可；这些是尚未解决的画质问题，不能将当前试玩通过解读为无伪影或全场景完成。120秒自动动态窗口：新版60.30 FPS/P95 25.31ms/P99 26.78ms；d9视觉算法隔离参考66.49 FPS/P95 22.19ms/P99 23.58ms。场景为Clinic转镜和小范围移动，未覆盖完整战斗与全游戏。冷诊断不计入热性能。更完整的现行安全和资源边界见开发根 `CURRENT.md`。

当前默认Native已更新为`c359a29`加载可靠性修复：修正提交锁/旧idle通知竞态及任务先入队后计数的发布窗口，画质算法仍为已认可的c8fb9df。两次同来源加载均成功并正常退出，其中真实NR动态验证578个job正常退休。旧HOLD保留，未绕过断言；有限回归尚不能保证所有偶发加载错误已消失。下面的热性能表对应c8fb9df，修复验证使用冷命令诊断，不作新热性能声明。原启动命令保持不变。

## 2026-10-16 可玩参考（亮部轮廓拖影待修）

已认可的画质参考Native构建为 `c8fb9df`，本地标签 `nrbridge-stability-candidate-20261016`。新增独立old/target区域候选过渡和完整时域支持的保守边缘保留；保留真实贡献年龄、四bank、原TTL与完整NR强度。Nearest/Cubic菜单只切换采样方法，**选择Nearest不会撤销这两项新算法**；完整旧行为通过下面的固定baseline启动。

```bash
# 新版：与本轮对照相同的私有Clinic进度/默认配置
python3 native/tools/nr_dev/play.py --continue-session dev-reprojection-switch-01
# 固定Nearest旧版：相同私有来源，独立进程及SR历史
python3 native/tools/nr_dev/play.py --continue-session dev-reprojection-switch-01 --native-build /home/arinp/Code/nrbridge-dev/build/baselines/9fcd4ccd443c1bb9ec133c31c539dedbf7a6e004
```

当前已认可的实际Native产物另有只读固定副本：`build/baselines/c8fb9dfdb7d80f444ddfdcf72c6483dba140963f`；后续默认build变化时，可将该绝对路径传给`--native-build`继续使用此版。

进入实际场景后在终端按Enter，入口核验Helper readiness并开启NR后，会核对窗口PID并切回私人游戏。Delete菜单、F8旁路、F9捕获和Restore defaults保持可用；恢复默认不会撤销新算法。游戏在后台可能被限到约1FPS，性能比较要保持前台。

相同存档、起始场景、预热和120秒动态脚本的一轮独立对照：

| 版本 | FPS | P95 / P99 ms | 最长帧 ms | 整卡峰值 MiB |
|---|---:|---:|---:|---:|
| SR-only | 69.35 | 15.85 / 16.62 | 30.30 | 2875 |
| 固定Nearest旧版 | 58.15 | 24.72 / 26.43 | 117.35 | 4232 |
| 新区域过渡＋边缘支持 | 57.86 | 24.49 / 26.19 | 28.62 | 4104 |

三轮均正常退休，焦点每秒采样均为前台；两轮NR零source缺口，SR-only一次缺口已保留在耗时内。新旧NR平均帧率差约−0.5%，单轮最长帧/显存差异不能证明优化。显存是整卡用量，含其他应用；没有输入延迟测量。新旧NR分别1187/1189个真实job结清，Feature创建/释放各1次。

两次新算法冷采集共18帧通过finite/alpha/TTL及实际SR/Present关联。独立重构最终half误差最多1 ULP；真实1/3候选过渡有52万像素参与双方混合；完整边缘验证有实际GPU像素保留证据。过渡old-only/fatal fallback没有被该组动态样本直接覆盖；CPU/Shader及源码审查覆盖不冒充该项GPU观测。玩家反馈“我觉得可以了”，认可当前可玩参考；仍有受光角色轮廓的发光拖影，未达到无伪影或正式稳定发布。

开发根 `comparisons/stability-20261016/results.json` 汇集原run和派生证据；`next-view/compare720.png` 是同源raw/NR720共享EV4映射，`next-view/actual-Present1080.png` 是实际后处理1080画面。独立SR-only截图在 `runs/dev-stability-sronly-hot-01/scene.png`，包含HUD、不同采样时刻，不能称严格同帧双SR历史A/B。

已知加载问题仍OPEN：旧基线与新版本都曾pre-NR PM4断言退出；guest巨大float不收敛卡死的生产者尚未定位。原HOLD与诊断保留。无新帧退出owner推进已通过no-peer和真实NR正常退休，但不是异常输入根因修复。此次后续前台加载连续成功并不证明这些问题消失。正式存档不写入，失败profile不可续玩。

## 重投影选项与固定Alpha对照

完整后续规划见 [NRBRIDGE_PLAN](../../docs/NRBRIDGE_PLAN.md)。Delete打开菜单后，在 **History reprojection** 选择：

- **Nearest (Alpha/default)**：默认，保留Alpha的最近邻历史采样。
- **Clamped cubic (optional)**：带同表面/同来源验证与限幅的三次重建，减少亚像素采样跳变；仍可能损失细节，且有GPU成本。玩家反馈体感变化不大，因此不替换默认方式。

切换在安全帧边界生效，清空视觉历史，保留Helper/物理资源；不需要模型Apply。菜单显示实际Applied模式及pending状态。配置格式v2记录reprojection=0/1；旧v1配置保留其他参数并加载为Nearest，不主动改写旧文件。恢复默认也恢复Nearest。两种immutable Shader pipeline在初始化时分别编译，Nearest不包含cubic采样分支。

此前固定cubic候选的120秒同存档同输入模式热窗口为54.91FPS/P95 25.81/P99 27.19ms，固定Alpha57.63FPS/P95 24.81/P99 26.38ms；这约4.7%的代价是当时两份binary的观测，不作为当前菜单版本的重新测量。两者正常退休；鬼影、大动作闪烁和候选亮度跳变仍未宣称解决。F8共享SR历史，不替代独立Alpha比较。

固定Alpha的源码和实际binary仍可单独运行：

```bash
python3 native/tools/nr_dev/play.py --continue-session dev-runtime-controls-01 --native-build /home/arinp/Code/nrbridge-dev/build/baselines/e08eacfed42b162a54f0d370f8ecdb28bf40f725
```

固定源码标签 `nrbridge-alpha-runtime-20261015`；历史对照数据在开发根 `comparisons/stability-20261015/results.json`。此命令使用旧v1默认配置的同一私人存档来源；旧Alpha不支持新v2配置。

## 本阶段验证边界

- `dev-runtime-life-01` 提供了本阶段资源生命周期的实际运行证据；原 watchdog **HOLD 保留**，不改写为正常退出或玩家验收通过。资源作用范围的独立证据不等于完整运行 PASS。
- `dev-runtime-nr-02` 完成 **1841 个真实 job：1821 captured、20 obsolete**，已验证正常退出；同源诊断 capture 实际完成。此项证明该次模型传输、候选处理与退出链实际运行，不代替画质或性能验收。
- `dev-runtime-controls-01` 正常退出，2715个真实NR job全部结清。3次重建验证Natural、Cinematic及恢复Default，参数身份1→4；Feature创建/释放各4次。连续confidence约千种真实值；强度0.501及修改后的TTL/深度/颜色/minConfidence均通过同源GPU读回与Compose重构（最多1 half ULP）。细节见该run的runtime-model-validation、runtime-controls-offline-check及runtime-thresholds-offline-check；不是主观消鬼影证明。
- `dev-runtime-hot-01` 无图像捕获，正常退休1374个真实job（1372 captured/2 obsolete）。纯SR静止70.54 FPS/P95 14.64/P99 15.68ms；NR静止57.59 FPS/P95 24.26/P99 25.48ms；120秒NR动态58.34 FPS/P95 24.25/P99 25.80ms，零source缺口、最大28.63ms。整卡峰值3995MiB/8188MiB（含其他应用）。尚未达到稳定60FPS，也未证明默认鬼影/闪烁已改善。
- `dev-runtime-nr-01` 曾在Helper和NR资源创建前因加载期PM4坏包退出23，原HOLD保持；追加说明仅允许经锁内当前宿主检查后的独立新上下文，不声称整个旧GPU上下文正常退役。新增包语义校验可提前停止错误寄存器写入，源数据损坏根因仍未证明解决。

上面的旧试玩反馈及早期Alpha性能数字仅对应其标明的旧 session/场景，不是当前 runtime/config 版本的性能或玩家 PASS。本轮源码尚未推送。

## 开发与诊断

Build: `bash native/tools/nr_dev/build.sh`. Outputs default to `nrbridge-dev/build/native`; use `BB_BUILD_DIR` for an isolated build, for example:

```bash
BB_BUILD_DIR=/home/arinp/Code/nrbridge-dev/build/convergence/native \
  bash worktrees/convergence/tools/nr_dev/build.sh
```

`NR_SOURCE_DEPS_ROOT` selects the prepared read-only source root (default `$NR_DEV_ROOT/inputs/source-deps`, with `NR_DEV_ROOT` defaulting to `/home/arinp/Code/nrbridge-dev`). It contains `fsr-vulkan`, `imgui`, `LibAtrac9` and their existing sibling `*.content.sha256` inventories. The wrapper verifies every file and rejects missing, extra or changed files before creating links, writing build provenance or compiling. It never copies dependencies, regenerates manifests, overwrites source checkouts or applies patches to these inputs. FSR must already contain the repository patches; the underlying build/submodule recipe remains unchanged. An empty gitlink directory can become a local symlink; an existing checkout or a link selecting another root is preserved and rejected. These local gitlink symlinks must never be staged.

`NR_BUILD_ENV_ROOT` independently selects the SDK/sysroot environment directory (default `/home/arinp/Code/bloodborne_pc/out/deps`). It must contain `bbport-env.sh`, `root/usr` and `dlss-sdk`. The selected script's real `DLSS_SDK_ROOT`, `VULKAN_INCLUDE` and first `CMAKE_PREFIX_PATH` entry must select that environment's SDK/sysroot; stale absolute exports are rejected. Moving a script alone does not make its dependencies portable. Default dependency versions and paths stay unchanged; explicit roots must be prepared from the same locked inputs. All configured paths must be absolute, and build output must not overlap a source/input root.

`build-inputs.json` records the selected roots, manifest/env hashes and actual environment exports alongside source provenance and the four-product manifest. `NR_DEV_ROOT` here only selects build defaults; Runner paths and GPU confinement are configured separately and are not relocated by these variables. No historical execution ledger is imported.

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

冷加载诊断沿用主协调现有 `--pm4-cold-trace`，同时启用 `BB_NR_RESOURCE_TRACE=1`；整run排除热性能评分。日志最多4096个资源事件加一条截断提示。离线地址关联使用 `python3 native/tools/nr_dev/resource_trace.py --game-log PATH --xid-address ADDRESS --fault-monotonic-ns NS --output NEW_JSON`，输出仅新建。不同进程的地址不可交叉归因；未命中、未记录、未知tick或截断都不能证明安全，当前hooks不覆盖image/SDK内部资源及所有GPU VA。普通运行不启用这些冷日志。


**当前恢复诊断状态（2026-10-16）。** `--context-recovery PATH`仅供主协调按`context_recovery.py`的精确schema执行有界新boot诊断；不得改旧host-safety为false来替代核验。它重新验证实际前序退休/逐job清单、全boot内核、锁/进程、build/依赖及原HOLD摘要，不能作为普通play的默认豁免。build9a32779的SR/off-skip/persistent-skip/小量resident链正常退休，但后继dev-bright-outline-player-01在NR前PM4断言HOLD，**目前所有新GPU启动暂停**。当前无残留/新增Xid不构成该run正常退休证明；恢复记录因新HOLD自动失效。c1a702e只补齐冷PM4生产者追踪，未修复未知生产者根因。

F9现可在同一捕获中保留下一合法候选的origin及acquired Resolve720，再采当前3帧；等待10秒无候选明确INCOMPLETE。约232.03MiB临时读回，非16帧连续历史证明，Resolve不是SDK tensor；实际GPU配对验证仍待完成。不要将编译通过视为亮尾修复或玩家验收。
