# Smooth Motion 原型：驱动层插帧与正常退出

分支：`sm/present-lifecycle`（实验分支，未合流）。这一轮只做 SM 驱动的 Present/退出生命周期，
不改 NR、SR、FSR/DLSS FG 与正式玩家安装。

## 结论

- NVIDIA Smooth Motion（`VK_LAYER_NV_present`）在当前 NvPresent 驱动（615.78.08）上可以进入
  真实 Present 路径，并在正常关闭窗口后完成驱动层退休：`SM_EXIT_PROOF`、`FG_EXIT_PROOF`，
  没有 `ErrorDeviceLost`，进程 `exit=0`。
- 只有驱动层 SM 时（应用侧 FG/NR 关闭）、以及 SM + NR（Helper 真正载入模型）两种配置都通过。
- 窗口缩放触发 swapchain 重建后再退出也通过。
- 尚未证明“驱动最终显示了插帧”：应用侧计数只有真实 Present（`FG_PRESENT kind=real`），
  SM 的合成帧由层内部呈现，不经过应用的 Present。见下文“未完成”。

M3 的 `m3-sm-r62` 在退出时出现 `ErrorDeviceLost`（HOLD）。本轮没有复现出同一个失败：
相同提交、相同驱动、NR 关闭或开启、含窗口重建的会话都是正常退出。因此不能声称已找到
r62 的唯一根因；本轮修的是退出路径上确定存在的并发与错误处理缺陷（见下），并给出可重复的
正常退出证据。

## 定位

`Presenter::Present` 的退出分支在 **present 线程**执行驱动层退休（`Swapchain::RetireDriverLayer`），
而 **command processor 线程**仍在 `Presenter::PrepareFrame` 里准备下一帧；两者共用同一个
`Presenter` 与 swapchain 视图/信号量。原实现还有两个问题：

1. 退休前没有等 CP 线程的在飞准备结束就销毁 swapchain 视图与信号量；
2. `DestroyResources` 在自己的 `vkDeviceWaitIdle` 失败后仍然销毁资源，失败状态被吞掉。

另外 `RetireDriverLayer` 的第一次 `waitIdle` 失败是静默 `return false`，调用方只能看到
“设备空闲未知”，没有区分 drain 失败与 destroy 失败。

## 修复

- `Presenter::PrepareFrame` 在入口取一个受 `prepare_gate_mutex` 保护的租约；退出闸门关闭后
  直接返回空白帧，不再触碰 swapchain 相关状态。
- `Presenter::Present` 的 SM 退出分支先关闸门，再 `release()` 自己持有的帧（让已经在等空闲帧的
  `PrepareFrame` 能结束，避免互相等待），然后最多等 15 秒让在飞租约归零；超时记
  `SM_EXIT_HOLD reason=prepare_in_flight_timeout`，不销毁、不伪造成功。
- `Swapchain::Drain()` 与实际销毁分离：`RetireDriverLayer()` = drain → 销毁视图/信号量 →
  销毁 swapchain → 再 drain；任一次 drain 失败都直接返回失败，不再销毁资源。
- 帧释放改为一次性（`released` 标志），退出分支的提前释放不会把同一帧放回空闲队列两次。

这些改动只在驱动层 SM 的退出路径生效，不改变同队列提交顺序、生成帧先于真实帧、fence 只在
最后一次真实 present 上 signal 的既有语义。

## 实测（本机 RTX 4060 Laptop，165Hz 输出，`BB_FPS_LIMIT=60`，`BB_PRESENT_MODE=Immediate`）

隔离方式：`BB_PREBUILT=1`、独立 `BB_DATA_DIR`/`BB_USER_DIR`/XDG 目录，只读引用
`/home/arinp/bloodborne/CUSA03173`，每轮独立 `nrhome/runs/<session>`，GPU 用
`/tmp/nrbridge-poc-gpu.lock` 与其它专项互斥。实验脚本见 `tools/sm_lab/`。

| 会话 | 配置 | 结果 |
|---|---|---|
| `sm-a-baseline` | 仅 SM 驱动层，NR/FG 关 | exit 0；`SM_EXIT_PROOF`；1 个 NVP swapchain；3492 真实 Present |
| `sm-c-resize` | 仅 SM，窗口 1280x720→1920x1080 重建 | exit 0；`SM_EXIT_PROOF`；2 个 NVP swapchain；3729 真实 Present |
| `sm-d-ingame` | 仅 SM，私有存档副本（当时按键时机不对，停在主菜单） | exit 0；`SM_EXIT_PROOF`；2 个 NVP swapchain |
| `sm-e-nr` | SM + NR（菜单场景，Helper `MODEL_VK_CREATE`/`PRESET B`/`GPU_complete=1`/`TEARDOWN NORMAL_RELEASE`） | exit 0；`SM_EXIT_PROOF` + `NATIVE_ASYNC_PRE_SR_EXIT_PROOF`；2 个 NVP swapchain；6697 真实 Present |
| `sm-i-game` | 仅 SM，载入存档后**实际进游戏**（亚楠中央，HUD 正常，60fps） | exit 0；`SM_EXIT_PROOF`；5220 真实 Present；无 DeviceLost |
| `sm-j-game-nr` | SM + NR，**实际进游戏**，NR 活跃（`NATIVE_ASYNC_PRE_SR_CURRENT_PRESENT` 5439 帧、NR job 230、`NATIVE_ORIGIN_GPU_USE_READY`） | exit 0；`SM_EXIT_PROOF` + `FG_EXIT_PROOF` + `NATIVE_ASYNC_PRE_SR_EXIT_PROOF`；无 DeviceLost |

`sm-j` 就是 M3 r62 的同构配置（SM + NR + 进游戏 + 正常关窗），本轮正常退休。

四轮 `nvpresent.log` 都报告 `Smooth Motion enabled [1]`、`System support: 1. Device support: 1`、
`NVP_Swapchain created`，没有 `passthrough swapchain`，即层确实接管了呈现（不是回退直通）。
四轮日志都没有 `DeviceLost`/断言。

`sm-c`/`sm-d` 的窗口重建说明 r62 里的“两个 NVP_Swapchain”本身不导致退出失败；`sm-e` 在没有
缩放的情况下也出现重建，说明 NR 启动时也会触发一次 swapchain 更新。

## 可选后端恢复（分支内）

`tools/nr_dev/fg_config.py` 的 `SM_UNAVAILABLE` 硬性拒绝已移除，`--fg-backend smooth-motion`
与 `--smooth-motion` 都可以选中驱动层后端；启动器标签与文案同步更新。防线保留在两处：

- `smooth_motion.check_capability` 仍要求二进制里有 `SM_EXIT_PROOF`；
- daily runner 的 boot 收据（`/run/user/<uid>/bbport-nrbridge-session.json`）在 pending/HOLD
  时拒绝任何新会话。

`tools/fg_dev` 60 项测试通过，其中 `test_sm_is_selectable_without_application_fg`
替换了原来的隔离断言。

## 正式 runner 准备

`tools/sm_lab/official_inputs.py` 生成一个私有 `NRBRIDGE_HOME`（默认
`~/Code/nr-home/build/sm-official`）：只复制 Helper 构建目录并改写其 manifest 的 artifact
路径，assets/seed/GE/sysroot/NGX/model/forwarder 全部按绝对路径引用共享输入，不复制游戏或
SDK。配置与 pins（10715 个文件）通过 `runtime_inputs.load` 校验。

构建与验收命令（GPU 可用时）：

```bash
BB_BUILD_DIR=~/Code/nr-home/build/sm-official/build/native bash tools/sm_lab/build.sh
NRBRIDGE_HOME=~/Code/nr-home/build/sm-official \
  python3 tools/nr_dev/play.py --daily --smooth-motion \
  --local-inputs ~/Code/nr-home/build/sm-official/localinputs/sm-B-v1.json \
  --native-build ~/Code/nr-home/build/sm-official/build/native --session sm-official-01
```

## 新 boot 官方验收（2026-10-29 17:45–17:58）

新 boot `6df86d77`，Astra 短探针 PASS 后按 `sm → off → sm` 执行，全部通过：

| 会话 | 配置 | 结果 |
|---|---|---|
| `sm-acc-1029-174527-sm1` | 官方 daily，SM，新建 prefix | `PASS_NORMAL_TEARDOWN`，exit0，`SM_EXIT_PROOF` + `FG_EXIT_PROOF` + `NATIVE_ASYNC_PRE_SR_EXIT_PROOF`，无 DeviceLost |
| `sm-acc-1029-174813-off2` | 官方 daily，层关闭对照 | `PASS_NORMAL_TEARDOWN`，exit0 |
| `sm-acc-1029-1752-sm3` | 官方 daily，SM，复用 R1 已退休 prefix | `PASS_NORMAL_TEARDOWN`，exit0，三份证明齐全 |
| `disp-off-01` | 实验启动器，层关闭 | 正常退出（无 SM 证明，符合预期） |

批后核对：锁空闲、无 bb-probe/Helper 残留、receipt=retired、本 boot 无 NVIDIA Xid。

**显示侧指标失败（重要负面结果）**：同场景同 60fps 上限下，SM 开：内容变化 114.1 Hz / 应用
真实 Present 55.7 Hz（2.05×）；SM 关：118.0 Hz / 59.9 Hz（1.97×）。两者都约 2 倍，说明
`zwlr_screencopy` 的抽样哈希被合成器自身重绘（约 140 Hz 的中位间隔）污染，**不能**用来
区分驱动层是否插帧。上一节的“2 倍判据”作废，需要换成能排除合成器重绘的方法（例如按区域
像素差阈值、或直接读驱动侧计数），列为下一阶段问题。

## 当前阻塞

2026-10-29 01:36 同机的 Native NR shadow 会话（`native-nr-shadow3109-001`，wE:p1）冷初始化
`VK_ERROR_DEVICE_LOST`，kernel Xid13；boot 收据被置为 `hold`，指向
`/home/arinp/Code/dlss_vulkan/private/native-nr-home/runs/native-nr-shadow3109-001`。
按规则这属于 boot 级 HOLD：新 boot 之前的 daily 入口会拒绝所有会话。MultiPass 复核结论
（2026-10-29）确认这是真实设备故障（fence=-4、exit23、无退休证明），不适用 DLSSG 的
exit0 重审，手改 receipt 也会因本 boot 的 Xid 再次拒绝——只能**用户手动重启到新 boot**，
旧 HOLD 与证据保留、不改 PASS、不删 receipt。该失败晚于本分支最后一轮正常退出（01:00 前），
不归因于 SM 会话。

重启后一条命令：

```bash
bash tools/sm_lab/acceptance.sh preflight        # 新 boot、无 Xid、无残留、锁空闲才继续
bash tools/sm_lab/acceptance.sh "sm off sm sm"   # SM 验收 + 同场景 off 对照 + 2 次稳态重复
```

`preflight` 只读判定：receipt 属于旧 boot 时视为过期（runner 也会忽略），属于本 boot 且
pending/HOLD 时直接拒绝。`acceptance.sh` 每轮独立 session 目录，第一轮建 prefix、后续
`--reuse-nr-prefix` 复用已正常退休的前一轮；任一轮 HOLD 立即停止计划。

## 未完成

- **插帧的显示侧证明**：应用只看到 60 次真实 Present；SM 的合成帧由驱动层呈现。当前证据只到
  “层启用且接管呈现”。工具已就绪但尚未在游戏里采样：`tools/sm_lab/frame_count.c` 用
  `zwlr_screencopy_v1` 以合成器渲染速率抓取输出并给每帧时间戳/抽样哈希，
  `frame_report.py` 统计内容变化率。判据：同一场景、同一限帧下，SM 开启时内容变化率应接近
  关闭时的 2 倍（例如 60→120），而不是与应用侧真实 Present 数相同。

  证据边界：这是**合成器侧内容变化率**，不是扫描输出或光子时间戳。实测该指标在 SM 开/关
  两种情况下都约为真实 Present 的 2 倍（见下节），说明它被合成器重绘污染，**当前版本不能
  作为插帧证据**；工具保留，判据需重新设计。
- **进游戏实机**：已在私有 profile 里完成（见 `sm-i`/`sm-j`）。实验启动器原先没有在复制存档前
  导出 `SAVEDATA_SRC`，已修复；存档按端口实际使用的 `savedata/1/CUSA00207` 只读挂载。正式
  runner 的存档同步路径仍需独立验收。
- **官方 runner 验收**：本轮用独立 profile 直启，没有走 `tools/nr_dev/play.py --daily` 的
  retirement audit / host-safety / prefix lease。要恢复为玩家可选后端，还需要用正式入口跑
  一次 `PASS_NORMAL_TEARDOWN`。
- 多次重复（同一配置 3 次以上）、长时间会话、以及在正式 runner 下的稳定性还没有做；
  `sm-j` 目前只跑了一次。

## 合流前的前置

1. 用正式入口跑通一次 SM 会话（含 NR、进游戏、正常退休），得到 `PASS_NORMAL_TEARDOWN`；
2. 用真实路径补上显示侧插帧证据；
3. 同一配置重复 3 次以上。

第 3 项里"移除后端拒绝"已在实验分支完成（见上），但仍保留二进制 `SM_EXIT_PROOF` 检查与
boot 收据门禁；在前两项通过前不合流 `nrbridge`。

## 复现

```bash
# 构建（独立 BB_BUILD_DIR，预置依赖，无 LTO）
bash tools/sm_lab/build.sh

# 一轮：进菜单、窗口重建、正常关闭
bash tools/sm_lab/play.sh sm-lab 30 SAVEDATA_SRC=<private-savedata> \
    PRESS_AT=12 RESIZE=1 BB_PC_MODEL=0

# SM + NR（会创建私有 Wine prefix；首次启动较慢）
NR=1 bash tools/sm_lab/play.sh sm-lab-nr 30 \
    PRESS_AT=12 NR_SETTLE=25 NR_HELPER_WAIT=20 NR_ARM_SETTLE=20 BB_PC_MODEL=0

# 汇总一轮的证据
python3 tools/sm_lab/summarize.py out/sm-lab/nrhome/runs/sm-lab
```
