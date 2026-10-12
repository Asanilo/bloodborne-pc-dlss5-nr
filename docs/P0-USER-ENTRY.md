# 隔离 NR 试玩入口（待 Root 新 case 审批）

入口：`python3 tools/nr_probe/play_p0.py`。它不创建审批、不清 ledger、不启动
裸 launcher、不更改 renderer/Helper。默认只打印命令。普通入口仍只承载 4/120
帧。唯一例外是 Root 新审批的 p2-quality-user1800：resident/hot、硬 cap1800、
outer300、Helper1..2；还必须 approval.experience_entry_gate 的 canonical 私有
path/SHA256/bytes 实际匹配，文件状态 PASS_BOUNDED_EXPERIMENTAL_ENTRY_GATE。
一般1800及18000仍禁止，旧批次额度不重置。

Root 已完成101054短测：同会话SR120/NR120真实帧/Present/退役和同源SR查询通过，
走动/转镜的实际画面已观察；稀疏截图不是 ghost/flicker 画质 PASS。本入口扩展
只有 CPU 离线检查，不意味着 user1800 已跑。Root 限定该 case 至多60秒有效NR
交互窗口，1800仅是硬上限，不是1800性能/稳定验收；窗口期限由 Root 新 gate/
受限 runtime 实施，入口自己不计时、不写 gate 或 ledger。

Root 先审新独立 batch/case、以 freeze_playtest.py 生成 approval，并用
prepare_playtest_profile.py 复制私有 NRTest seed。approval 必须覆盖当前
native build 和 Helper，以及新增入口工具/文档的独立操作审核 pin。
run_playtest.py 原样负责 GPU flock、全部 hash/preflight、预算、私有 XDG/存档/
资产副本、ownPG watchdog、残留与退役检查。旧预算不重置，已用 case 不重启。
脚本本身不会替你复制 profile 或批准运行。示例路径由 Root 给定：

```bash
python3 tools/nr_probe/play_p0.py plan NEW-SESSION --approval /ABS/integration/playtest/APPROVAL.json
python3 tools/nr_probe/play_p0.py check NEW-SESSION --approval /ABS/integration/playtest/APPROVAL.json --execute-approved
python3 tools/nr_probe/play_p0.py launch NEW-SESSION --approval /ABS/integration/playtest/APPROVAL.json --execute-approved
```

`check` 委托 Root 的 --preflight-only，不运行 GPU；`launch` 才可能运行已审批
case。不要直接执行 run_game_p0.sh。不要添加环境变量来覆盖 mode、帧数、构建、
资产路径或期限。未指定 --execute-approved 时 check/launch/input 都只预览。
当前主机 GPU 回归由 Root 独占；亲手入口需等 Root 明确交付该 case。

## 亲手操作和实际状态

私有配置固定正常 DLSS Quality：实际 HDR Run，1280x720 渲染/Depth/MV，
1920x1080 SR/NR 输出，jitter 开，90FPS cap（不是承诺达到90）。进入私有
NRTest Clinic 场景后操作。鼠标点击/聚焦这个游戏窗口；此前后台窗口曾出现
约1秒 Present 节流，切前台恢复只是环境对照，不是 renderer 修复。

- Space / 小键盘 Enter：确认；Shift：返回/闪避；Esc / Enter：游戏菜单。
- WASD：移动；方向键：视角；Ctrl：慢走。
- 鼠标捕获默认开，移动鼠标转视角；左键 R1、右键 L2、中键 R3。
- Insert 或手柄 L3+R3 打开已有 ImGui 设置。Experimental NR 勾选提交 Enable；
  关闭设置返回游戏。设置打开会影响 idle/状态，静态性能窗口须在游戏画面内。

Overlay 显示真实 native 状态、accepted/fallback、原因、命令区间/含等待时间。
Starting/未 ready 时走原 SR；RequestedEnable 不是本帧 NR 成功。
Active 也不是逐像素质量证书，accepted_for_merge 不等于唯一 Present。
Quarantined/Failed 时原 SR 回退，不自动反复重启 Helper。
CLI 同一请求入口（只委托 input_playtest.py 的实际 game PID/startticks 检查）：

```bash
python3 tools/nr_probe/play_p0.py status --invocation /ABS/runs/INVOCATION
python3 tools/nr_probe/play_p0.py input --invocation /ABS/runs/INVOCATION --enable on --execute-approved
python3 tools/nr_probe/play_p0.py input --invocation /ABS/runs/INVOCATION --dump --execute-approved
python3 tools/nr_probe/play_p0.py input --invocation /ABS/runs/INVOCATION --enable off --execute-approved
```

status 是可能陈旧的快照，不是进程存活/退役证明。input 请求不会人工写 Helper
ACK，也不会在 UI 做 Vulkan。Dump 是异步实际 Present 请求，不是精准源帧 A/B；
Root 的 diagnostic 同源证据另外审计。可选 --button cross 等仅复用已有私有 pad。

此次亲手窗口仅在私有 Clinic 中开/关 Experimental NR、查看真实 state/fallback、
走动和转镜。没有验证菜单、load/读档、热参数变更，不能借此次开放宣称它们可用。
不要改分辨率/jitter/Style/Intensity；场景之外仍按原 SR fallback 和 Root门禁处理。

## 冷启动与退出

减少重复冷启动的现成方法是在一个批准 resident 会话内保持 Enable，复用同一
设备/feature/slot；初始化后台推进，界面先显示 Starting 并保留 SR。Root 可以
批准 initial_enable=true，第一帧支持的真实 SR 自动启动，无逐帧 trigger；若
initial_enable=false，进实景后第一次 Enable 才开始冷初始化。脚本不能提前模型
warmup、沿用未知 history、跳推理、复制 DLL 或扩大 service/控制期限。

Off 必须真实 stop/drain/末次 GPU use/reap，下一 On 是新 session/feature/reset1，
会再次有冷启动，不能为减少等待偷偷保留 off 的 feature。先关 NR 并观察 Draining
转 Disabled 是容易看懂的退出过程；然后在游戏菜单正常退出以保存**私有**进度，
关闭游戏窗口。活动窗口关闭走已有 atomic RequestExit/renderer managed drain；
UI 最多等待原120s，超时只报 abnormal held，不能视为正常 retire。不要将 Ctrl-C、
kill、Root watchdog 或 inv/stop 当正常退出。Root 仍核 release/audit/reap/残留。

## Root 的计时/同 base-session 统计摘要

- `P0_SR_GPU_TIMING`：source_game_frame/tick/status/duration_ms/result/
  availability/timestamp_valid_bits/timestamp_period_ns/query_readback_bytes32B。只计 MEASURED；NOT_MEASURED 不是0。
  区间只包原生 Dlss::Evaluate 发出的命令及依赖，排除 feature create、motion、
  sharpen、NR、merge、Present；不是 full game GPU 或 pure SR。
- native-frame 原 input_published_cpu_monotonic_ns 仍在 AtomicWrite **之前**；
  input_publish_completed_cpu_monotonic_ns 在 successful rename 返回之后。
  complete_observed/selection_ready 都是同一 native steady_clock；后者在 validation
  与 CONSUMED 完成后、caller selection/merge 前，不是光子/Present。新增
  input_publication_cpu_ms 与 published_to_complete_observed_cpu_ms 不应再叠加旧
  peer_completion_wait，后者还包含 Helper GPU/CPU、发布、唤醒/轮询，并非纯 IPC。
- 保留原 `P0_SR_PRESENT`（只有 mode=srOnly）及 `P0_PRESENT_SUBMITTED` 不变。
  新 `P0_BASE_SR_PRESENT` 只在实际 game、非 reuse、resident、实际 PrepareFrame
  selection 已知且**未选 P0 NR**时记 post-Present mono/该 Frame 的 present编号/
  swapchain结果/base_session。Blank/replay/unknown 排除，不看 requested_enable。
  它不证明 SR shader 确实执行/Helper=0/IPC=0；Root 另以实际 Clinic/Quality/SR
  活动证据界定窗口，不能把标题/菜单算进实景性能。
- 同一个 base-session：base 事件以 (base_session,present_frame_number) 去重；NR
  只计 P0_PRESENT_SUBMITTED selected_output=1、swapchain_present=1，并保实际
  (derived session,frame,source,present编号)；off/on 后 f0 重置，不能只按 frame 聚合。
  两类互斥选择，fallback 属 base；不把 accepted_for_merge_count 当 Present。
  Root 在两个预先标记静态窗口各取120个成功唯一 Present，计算真实时间间隔，
  所有 stall 保留；首冷启动独立列。动态前后画面仅体验观察，不是同源 matched图。

原062913 Quality120/lifecycle是已通过的**功能**基线，约21.7FPS NR vs68.45 SR；
近60目标尚未达到，计时收益不作性能 PASS。真实 NR phase、MVecScale1、k_output、
未知曝光/manualWP1、加载 PM4 偶发失败等边界保留，不宣称通用画质或稳定试玩。
