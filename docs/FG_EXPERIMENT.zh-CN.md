# Linux Vulkan FG 实验线

## 基线与启动

FG 开发在 `fg` worktree；起点 `d2216f6`，构建设施 `59ed7cb`。不覆盖 NR worktree、玩家
`runtime/current`、正式配置或存档。使用同一个 `out/` 和共享依赖/ccache。

```bash
# 常规构建和原有 CPU 测试
bash scripts/dev-build.sh --test
# 实测前的增量构建：同时记录 Runner 所需源码身份与四产物 SHA，不复制二进制
python3 tools/fg_dev/build.py
# 设置 NRBRIDGE_HOME 为已有、已密封运行输入所属的专用开发目录。
python3 tools/nr_dev/play.py --daily --sr-only --experimental-fg fsr31 \
  --native-build "$PWD/out" --local-inputs "$NRBRIDGE_HOME/localinputs/CONFIG.json" \
  --session fg-UNIQUE-SESSION
```

运行配置显式选择 SR-only，采用私有 seed/存档、DLSS Quality 720→1080、真实帧上限 60、
Immediate Present、FSR x2。使用原有 `/tmp/nrbridge-poc-gpu.lock`、当前 boot/内核/HOLD
审核和全局会话 receipt；NR Agent 占用时不会启动。Helper 不启动、不 arm，仍保留原有
PreSR 无 peer 退出审核。启动前后都须保持被选中的 `out` 产物不变。

`--experimental-fg` 可选：

| 参数 | 后端 | HUD 路径 | 实测状态 |
|---|---|---|---|
| `fsr31` | 已有 SDK 1.1.4 portable FI/OF | 整帧插值 | 已在 RTX 4060 Laptop 诊所运行、实际成对 Present、正常退休 |
| `fsr31-hudless` | 同一基线 | 首个 UI draw 前捕获，交给 FSR HUD 分离 | 独立画质对照；不能预先认为能解决全部双影 |
| `fsr316` | 已有 FSR-Vulkan SDK 2.3 / analytical FG 3.1.6 | 整帧插值 | 独立实验；不替换默认基线 |

Delete 菜单在显式 `BB_FRAME_GEN` 实验会话显示 FG 开关。也可向本轮 `RUN/fg-control`
原子重命名一个内容为 `0` 或 `1` 的完整文件。请求在 scene/display 边界消费；关闭不
再拿新输出，重开重置历史。普通启动仍不自动启用已保存的 `frame_generation`。

## 真实调用路径

`RunScaled → CaptureFrameGenScene → CaptureScene(depth copy + MV/camera) →`
`RedirectColor/CaptureHudless(optional) → OnDisplayPassStart/Interpolate →`
`OnFrameStart → display copy → Presenter::PrepareFrame/TakeOutput →`
`EncodeInterpolated → generated Present → half interval → real Present`。

关键发现：Rasterizer 的 `OnFrameStart` 在刚完成场景的显示拷贝开始时调用，位于
`Interpolate` 与 `PrepareFrame` 之间。不能在这里清掉刚生成的 ready 输出。首轮实测
发现 `ready>0/taken=0`，修复提交 `a816805` 保留 handoff 到实际 flip 消费。

默认 1920×1080 原来使用 Native HDR SR 路径，没有 FG CaptureScene/TakeOutput 路径。
实验开启时逐真实帧快照 FG 设置，将其引到已有 Scaled scene/UI/display 路径，即使
输出仍是 1080p。这是 LDR scene SR 路径，与 NR 当前 Native HDR SR 基线有区别。
共享文件的这项接入独立提交在 `4d584fc`，供最终合流审查。

## 可观察性、时序与退休

- `FG_PRESENT` 在 `swapchain.Present()` 返回后记录真实/生成/其他/失败分类、真实 source
  编号、FG 编号、draw-ready tick、present-submit tick、单调时钟和累计数量。
- `Real / FG / Present` HUD 读取成功 Present 分类计数，不把全部 Present 速率标成游戏 FPS。
  真实帧 Prepare 编号和 NR source/history 仍只推进一次。生成帧不会触发 NR Present 通知。
- `recorded` 只表示 Prepare/Dispatch API 成功，不能证明 GPU 完成；Presenter 先等 draw
  timeline，再决定是否显示。失败时已有回退复制仍保证 encode 输入有效，但不把副本呈现为 FG。
- 生成帧与真实帧各走一次 Acquire/Submit/Present。中点 pacing 在 draw GPU 完成后计时；
  估计间隔来自真实帧 Prepare 时间，blank 不污染估计。没有显示扫描或 input-to-photon 测量。
- 每个 Frame 还保留最后一次 present scheduler tick。即使生成帧已提交、最终真实帧因
  最小化等原因跳过，下次复用前也会等实际 reader 完成。
- Context/尺寸变化等待 draw 完成，HUD-less 图像随 context 重建。正常退出先冻结 FG，
  CP owner 完成 draw/encode、释放 provider/目标/depth/HUD-less；Present owner 再完成
  最后 reader 并释放所有 encoded 插帧图像。UI 线程仅请求/等待。
- 程序正常出口使用 `_Exit`，所以析构不能充当退出证明。Runner 额外要求唯一
  `FG_EXIT_PROOF draw_GPU_complete=1 present_GPU_complete=1 resources_released=1`；
  NR 的无 peer 证明不能代替 FG 退休。未知状态保留资源/锁，不 force-free。
- 3.1.6 的临时 image views 通过 draw scheduler 的实际完成回调调用 `RetireFrame(id)`；
  provider Destroy 仍先等待全部相关 GPU 工作，不以 recorded 或 Present 返回替代完成。

分析：`python3 tools/fg_dev/analyze.py RUN/game.log.1 RUN/game.log`。
只传实际存在的文件。日志轮转时报告是保留窗口；累计值应从最后一条记录读取。
创建 `RUN/fg-sequence-trigger` 可捕获一次 16 张交替输出到 `RUN/dump`。读回发生在
presenter 输入图像，并非 compositor scanout；捕获会扰动性能，不能混作热性能数据。

## 首个可玩 GPU 验证（2026-10-22）

源码/产物、CPU 构建通过和 GPU 验证分别记录；不是用后续源码替代实测身份。

- `16c35fd` / 首轮 `fg-fsr31-first`：进入诊所，发现 ready/taken handoff 问题，生成
  Present 为 0。此轮不是 x2 通过；实际 FG 两队列退出退休通过。
- `a816805` / `fg-fsr31-handoff`：成功 Present 累计真实 **8481**、生成 **6157**；
  开启区间可配对 **6155** 对 generated→real，保留日志中零身份/顺序错误。
  全局首个 WSI resize 返回失败 1 次，不算生成成功。
- 末段保留日志样本：真实间隔 median **16.68 ms**，合并 Present median **8.71 ms**，
  p95 **9.40 ms**。这不是扫描输出测量，不能宣称固定的物理 120 Hz 或输入延迟改善。
- 相机旋转、短移动、菜单打开/关闭、FG 关闭/重开有实测；关闭区间只产生真实 Present，
  重开恢复生成。Bloodborne 游戏菜单不等同于真正暂停；最小化/恢复和分辨率变更仍需专项实测。
- 两轮都是 `PASS_NORMAL_TEARDOWN`、returncode 0、实际 Native last-use proof、无残留、
  无 watchdog/监督信号；FG_DRAW_RETIRED、FG_PRESENT_RETIRED、FG_EXIT_PROOF 全部存在。
- 16 张交替 readback 保留为无损 PNG，逐张还原原 BGRA 字节验证通过；可比较的 7 张
  generated 均不等于任一相邻 real。不过该组主要是静态画面，不能拿微小像素差异证明运动质量。

当前问题：运动时 HUD 数字/图标明显重影；高速边缘/遮挡、闪烁和额外输入延迟尚未
验收。标题/加载场景不足以判断 FG 工作。首个可玩版允许这些视觉瑕疵，不把它称作
完成画质优化的正式发布版本。

## 与 Native NR + DLSS SR 的合流接口

目前不删除 `!frame_generation` 限制、不改 NR 协议或时域算法。三处限制的含义：

1. `PollNativeFrameBoundary/PollReady`：只准入被验证的 Native HDR DLSS Quality+jitter
   source 域；切出支持路径时排空，不允许向 Helper 发布另一套分辨率/颜色域。
2. legacy snapshot 的同类限制是冻结实验入口，不是普遍 FG 硬件限制。
3. `RecordDlss` 中 `hdr && !Scaled() && ... && !frame_generation && !pending`：保护
   原始720 HDR→活动编辑→SR1080 的资源/历史假设，不能因为菜单同时开启就直接移除。

最小后续接入：

- 保留当前 Native HDR NR→DLSS SR 调度；将 FG 输入采集拆为“当前真实 scene 的 depth/MV/
  jitter/camera/reset tuple”与“同一 tuple 的完成显示颜色”两个边界。在 NR/SR 真实读取时
  先复制会被游戏复用的输入，等 tonemap/postprocess/HUD 完成才插值。
- 最后显示颜色必须来自本帧实际 VideoOut；不能把 scaled-LDR `ui_image` 当成 Native HDR
  SR 的真实输出。Presenter 原来 `upscaled && TakeOutput` 的 gate 也需扩展到 native tuple。
- 每对输出携带 real source、epoch/reset、render/output extent、provider、draw-ready tick。
  FG id 与 source id 分开；只有 real 分支能推进 NR 历史、通知 NR 当前 Present 或采集 NR 输入。
- 菜单/加载无有效 scene、provider/分辨率改变、长暂停、NR/SR 输入域改变应向 FG 请求 reset；
  资源变化仍等待真实末用，不靠清 ready 或释放 FD 代替排空。
- 共享 NR image/FD 的完成仍以 NR 协议为准；FG 延长的是本地输入副本和本地 encoded 输出
  reader 生命周期。当前 FG 两队列退休确认要与 NR owner 退出确认都完成后才允许退出。

## 其他路线的源码研究

2026-10-22 核对官方上游与本机 SDK：

- AMD SDK 2.3 同时列出 analytical FI 3.1.6、swapchain 3.1.7 和 ML FG 4.0.1；版本号
  分属不同组件。当前 FSR-Vulkan 子模块已经包含版本化 3.1.6 原生 Vulkan compute 后端，
  因而直接复用它建立 `fsr316` 实验，不更新/覆盖子模块或 1.1.4 基线。
  [AMD SDK](https://gpuopen.com/amd-fsr-sdk/)、
  [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan)。
- NVIDIA Streamline 2.14.16 官方 DLSS-G 指南有 Vulkan tagging、Acquire/Present hook、
  semaphore 和 inputsProcessingCompletionFence 规则。其“Vulkan 不支持”的条目指
  Dynamic MFG/VSync 等特定能力，不能泛化成 FG 无 Vulkan API。
  [指南](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)。
- Streamline 的 FG 插件仍只有预编译 DLL，公开仓库不能构建该插件；这是当前原生 Linux
  部署的具体缺口，不能用 Windows Vulkan 支持直接推断 Linux 已可用。
  [官方 README](https://github.com/NVIDIA-RTX/Streamline/blob/main/README.md)。
- 本机原有 DLSS SDK 提供 `nvsdk_ngx_helpers_dlssg_vk.h`、FrameGeneration feature=11、
  原生 `NVSDK_NGX_VULKAN_GetFeatureRequirements`。新增 `ngx_fg_probe.cpp` 直接查询 Linux
  loader 的 SR/FG requirements；不创建 VkDevice、feature 或提交 GPU 工作。只在同一 GPU 锁
  和当前 boot admission 下运行，不凭头文件存在或 RTX40 型号声称 FG 可以执行。
- Wine 技术收益候选是执行本机现有 PE `nvngx_dlssg.dll`，而非重新实现 FSR。NRBridge 的
  OPAQUE_FD/UUID/tuple/EXTERNAL ownership 可作共享输送参考；FG 还需要独立的两帧历史、
  输入结束使用 fence 和输出 reader 退休，不能直接复用 NR 的 CONSUMED 作为 FG 完成证明。
  只有 native loader 能力查询和具体 PE Vulkan create/evaluate PoC 明确后，才决定是否承担
  额外跨进程中点调度延迟；本轮没有把 D3D12/Wine 桥接当成已实现功能。
