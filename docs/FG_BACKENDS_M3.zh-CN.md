# M3：统一 FG 后端与 M2 主线合流

本轮以 `m2/dlss-fg@f4f1f65` 为基线，恢复标签为 `recovery/m2.3-f4f1f65`。
不再优化 FPS，不更改 NR 时域/抗拖影算法，也不更新玩家正式安装。

## 入口与配置

现有 NRBridge GTK 启动器的「帧生成后端」保存 `fg_backend`：

| 值 | 功能 | 状态 |
|---|---|---|
| `off` | 不启用应用级或驱动级 FG | 可用，NR 独立 |
| `fsr31` | FSR 3.1 HUDless x2 | 可用；高速转镜植被漂移仍是已知缺陷 |
| `dlssg` | NVIDIA DLSS FG x2 | 可用；现有 DMA-BUF GPU bridge |
| `smooth-motion` | NVIDIA present Layer | 暂不可用，见下文实机失败 |
| `fsr316` | 既有 FSR 3.1.6 实验 | 保留；本轮未重新验收其画质 |

高级设置保存 `fg_dlss_pipeline`：`submit-ready-v1` 为稳定回退，
`consumer-queue-v1` 为 M2.3 Experimental Q1。它们属于同一个 DLSS FG 后端。
`fg_dlss_transport` 默认 `gpu-dmabuf-v1`；选择 `mapped-v1` 或 `files-v1`
保留 CPU Bridge 回退，实际自动使用 `sync-v1`。切换后端/流水线需要重新启动游戏。
FSR/DLSS 的游戏内开关仍可关闭和恢复插帧；F8 只控制 NR。

DLSS 高级输入复用现有 `fg_dlss_model`、`fg_dlss_runtime` 和
`fg_dlss_prefix_session`。缺失模型、Helper、退休 prefix 或不兼容构建会明确拒绝启动，
不会自动替换为 FSR。当前支持范围仍为 1080p DLSS Quality、Old memory、jitter/object motion。
既有 `nrbridge_fsr_fg` 配置在没有新键时迁移为 FSR；新键优先，保存时移除旧键。
鼠标、存档、在线池配置均继续走原有接口。

CLI 的统一入口为 `tools/nr_dev/play.py --fg-backend ...`，以及现有
`launcher/nrbridge_entry.py --play --fg-backend ...`。DLSS 子参数：
`--dlssg-pipeline`、`--dlssg-transport`、`--dlssg-model`、`--dlssg-runtime`、
`--dlssg-prefix-session`。旧实验参数保留用于历史诊断；冲突选择会报错。
最终 manifest 固定唯一后端，并检查应用级 FG 与 SM 环境互斥。

源码开发启动器支持 `NRBRIDGE_NATIVE_BUILD` 与 `NRBRIDGE_RUNTIME_INPUTS` 指向
已有受管理构建和版本化配置；仍由 runner 校验完整来源和 GPU 安全条件。
`--dlssg-runtime` 允许主线复用 FG 工作树已有的 Helper；比较内容哈希而非绝对路径。
本轮没有重打 AppImage，旧安装不会因源码合流自动升级。

可选 `--reuse-nr-prefix` 只在独占锁内借用已正常退休、相同 Wine/NGX 的 NR prefix：
移动到本次 canonical 路径，实际 GPU 正常退休且 wineserver 结束后归还；失败则保留。
不复制 prefix，不用文件移动代替 GPU 完成。重启后如需归还保留的磁盘目录，必须单独
记录新旧 boot 和无旧进程证据，原 HOLD 结果不改为成功。

## M3 实机验收

本轮使用现有玩家进度的私有副本、1080p Quality、Old、保存的 60 FPS/Immediate 设置。
场景为当前亚楠进度，不是 M2 的统一性能基准；以下计数只验证路径，不能与 M2 FPS横比。

| 会话 | 实际路径与抽样 | 退出 |
|---|---|---|
| `m3-off-r58` | 应用 FG=0、SM=0；F8 后 NR cache/job 推进 | 正常退休 |
| `m3-fsr-r59` | NR OFF 300/300；NR ON 279/279（生成/真实 Present）；FG 关闭180真实/0生成，恢复后重新配对 | 正常退休 |
| `m3-dlss-submit-r60` | submit-ready：NR ON 204/204；FG 关闭180真实/0生成，恢复后重新配对 | 正常退休 |
| `m3-dlss-q1-r61` | consumer-queue：NR ON 243/243；FG 关闭181真实/0生成，恢复后重新配对 | 正常退休 |
| `m3-sm-r62` | Native maps 确认加载 libnvidia-present；驱动报告 enabled=1/support=1；应用FG=0，NR运行 | **HOLD：退出 ErrorDeviceLost** |

r59–r61 的有界抽样生成帧配对错误均为0；采样边界允许生成/真实相差1。
DLSS 活跃截图 HUD 正常。人工高速转镜、长时间体验和两条 DLSS 流水线的操作延迟比较仍交给玩家。
本轮未重新做全量 Vulkan validation；不宣称既有通用 VUID 已全部修复。

SM r62 在普通关闭窗口后出现 swapchain device-idle / timeline submission 的
`ErrorDeviceLost`，返回23，未取得正常 GPU 退休证明。不能仅凭已加载 Layer 报告 SM 可用，
也不能把内部约60真实 Present 当作驱动最终显示120帧的证据。统一玩家入口暂拒绝 SM，
显示具体原因；原诊断路径和失败证据保留，后续专项再定位，未认定唯一根因在驱动。
旧 boot `9daae99f-0bbc-427a-a2bc-30b8aed85b76` 的 HOLD 原样保留。
用户重启后确认新 boot `46eda2e3-f9e4-4560-a82a-195b6cee28dd`，只归还借用磁盘 prefix，
独立记录在 r62 的 `nr-prefix-reboot-recovery.json`，不伪造旧会话退休。

CPU 回归：FG/runner/SM/统一后端52项、launcher/设置/鼠标及在线入口相关41项通过；
另有前序 player存档/续跑31项通过。GTK 实际控件完成五项选择持久化且 NR 状态不变。
Native 增量 `--test` 使用现有缓存；包含既有 DLC/运行时测试。

## 主线结果与交付入口

- 功能提交 `112004b`，合流提交 `5445cd2`，Native 冲突环境保护 `f558dc8`。
  M1 恢复标签 `recovery/m1-pre-m3-7489493`，M2 恢复标签 `recovery/m2.3-f4f1f65`。
  `nrbridge` 保留完整 M2 历史，FG 工作树同步到同一合流历史；未覆盖脏 FSR 子模块。
- 主线 `f558dc8` 的 `m3-mainline-q1-r63` 完成 NR+SR+DLSS Q1 实机验收。
  NR OFF 5秒：270真实/269生成；NR ON 5秒：242真实/243生成。
  FG关闭3秒：180真实/0生成，NR job继续推进；恢复3秒：144真实/144生成。
  抽样配对错误0；HUD截图检查正常。这里是功能验收场景和有界采样，非 M2 性能横比。
- r63 返回0，`PASS_NORMAL_TEARDOWN`，实际 NR GPU/模型/FD proof、
  `FG_DRAW_RETIRED`、`FG_PRESENT_RETIRED` 均通过；无残留进程、无watchdog，
  guard为retired，借用NR prefix已归还，私有存档正常同步。
- 合流后的 Native 增量 `--test`、52项FG和41项公共接口再次通过。
  最终 GTK 实测保存所有后端、保持NR不变，并显示SM的实际失败原因。
- 本机开发启动入口：在 FG 工作树执行 `bash out/launch-m3.sh`。
  它使用**已有启动器**和 `nrbridge/out` 主线构建，复用 FG/out 的 Wine Helper，
  设置与存档位于 FG/out/m3-launcher，未替换正式玩家安装或配置。
  当前预选 DLSS GPU / consumer-queue-v1 / NR Preset B；高级选项可回退 submit-ready-v1。
  `out/` 脚本是本机开发入口，不是可移植的发行包。
- 本轮新建私有配置、存档副本和六个验收目录合计约 **0.89 GiB** 分配空间
  （`du -sk`，含约0.42GiB的SM HOLD目录，保留GPU故障证据）。少量脚本/日志另计。
  既有 out 的增量编译和共享缓存净变化没有独立起始计量，不把上述值冒充整个工作区净增量。
  没有新 Wine prefix、完整构建副本、依赖副本或 AppImage。

尚未完成：SM正常退出与独立显示生成帧验收；FSR3.1.6新画质验收；新AppImage发布；
玩家长时间转镜/战斗、F8与FG开关体验、DLSS两条流水线的主观延迟比较。
本轮没有改动联机/DLC算法；其公共入口/既有CPU测试通过，不声称完成新联机实机测试。
