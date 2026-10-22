# FG 模糊受控对照（2026-10-22）

结论：**未复现同一 SR 路径下 FG 导致静态真实帧整体变软；已确认 1080p 到 2K 的线性
显示放大，并复现 whole-frame 生成帧的运动 HUD 错位。默认继续使用 HUDless31。**
这不证明所有运动区域无软化，也不把局部梯度当作完整画质分数。

## 条件与采样位置

使用同一 GPU 构建 `0dff031`、RTX 4060 Laptop、驱动 615.78.08、Old memory、私有诊所
种子。第二阶段均 SR-only，不启动 NR Helper；未扩大 NR 的分辨率支持。
笔记本实际面板 2560×1440 @165 Hz、桌面 scale 1.5。对照固定全屏，Presenter 序列
均为 2560×1440；采样位于 encode 后、swapchain blit/overlay/compositor 前，不能据此
评价面板响应时间或 compositor 的全部行为。

1080p 的 FG 开关对照明确使用 `--fg-native`，保持原生 HDR DLSS SR 路径不变。
旧独立 FG 路由在打开 FG 时强制进入 LDR SR，关闭时可能回到 HDR SR，因此旧开关
对照混入了 SR 路径变化。集成版已消除 1080p 的这项混杂。

采样覆盖 FG 开/关静态、转镜、快速转镜、生成/真实帧、HUDless/whole-frame，以及
现有 1440p SR-only FG。每批16张实际 Present 源，按顺序区分 real/interp；运动触发
等待私有 pad 请求生效，另检查真实帧间像素位移。低速样本变化不足时不用于强运动
结论，快速样本真实邻帧 MAE 分别约5.70（HUDless）和2.21（whole-frame）/255；它们
速度和相机轨迹不完全相同，不能用全画面 MAE 排出两种算法的整体质量名次。

## 观察

| 对照 | 本次结果 | 可支持的结论 |
|---|---|---|
| 1080p FG on/off 静态真实帧 | 固定场景 ROI 边缘强度约1.0281 / 1.0254 | 变化约0.27%，未见整体软化 |
| 1080p HUDless 静态生成/真实帧 | 同 ROI 约1.0272 / 1.0281 | 静态生成帧接近真实帧 |
| 1080p→1440p 全屏 | EncodePass 使用 linear sampler，源为1920×1080 | 真实/生成帧均经过4/3放大，已有重采样软化来源 |
| 同相机位置改为1440p SR-only | 场景 ROI 边缘约+15.8%，HUD约+23.4% | 此组合更清楚；包含输入分辨率与 HDR/LDR 路径变化，不能全归因于显示缩放 |
| 快速转镜 HUDless | 固定 HUD 亮像素对当前真实帧误差约0.0001/255 | HUD 基本保持当前真实帧，未见 HUDless 导致整体 HUD 模糊 |
| 快速转镜 whole-frame | 同 HUD 测量约126.8/255，截图可见严重错位/多影 | whole-frame 明显损坏该运动样本的 HUD，不作为默认回退方向 |

ROI 边缘强度是灰度横纵相邻差的均值之和，用于这个固定场景的受控比较，不是 MTF、
视觉清晰度或泛化画质分数。运动错位可能增加边缘，不能把较高梯度解释成更清晰。

1440p 实际上下文为1706×960→2560×1440，进入原有 scaled LDR SR 路由；1080p 原生
上下文为1280×720→1920×1080。NR 联动继续固定1080p Quality，避免借本对照扩大 NR
采集、历史和共享资源域。

## 工程决定与剩余限制

- 保留 HUDless31 默认，保留 whole-frame / 3.1.6 实验选择，不为这次主观软化盲改 FSR。
- 集成版 FG 开关不再切换1080p的 HDR SR；这是本次对可比性和联动所做的实际修正。
- 希望避免1080p全屏放大的玩家可先用1:1窗口作判断；1440p+FG只在SR-only实验中验证。
  不把锐化滑条或修改插值算法作为未经验证的默认解决办法。
- 快速运动仍有遮挡边缘、局部重影；单帧截图不能证明运动观感与输入到光子延迟。
- Present 间隔仍有波动及生成→真实/真实→生成的不均匀间隔，未宣称锁定120Hz或
  获得物理scanout证明。它可能影响运动观感，需要与空间清晰度分开处理。

两轮模糊会话均正常退出、无残留，拥有 FG draw/Present 与 Pre-SR 的实际退休证明。
最终新 boot preflight 通过，历史失败/HOLD未删除。
第二阶段新增约0.65 GiB私有目录/PNG；无损往返验证后去除约2.6GB等价raw重复件。
未创建新构建目录、依赖树或AppImage，未修改玩家安装。

本机证据保存在 `out/fg-blur-*-metrics.json`、`out/fg-blur-*-verification.json`、
`out/integration-source-and-blur-audit.json`；对照图为 `out/fg-blur-static-comparison.png`
和 `out/fg-blur-hud-comparison.png`。原始顺序、身份与无损SHA位于各私有run的
`dump/*/capture.json`、`dump/sequence-png-manifest.json`。


2026-10-22 玩家补充：在修复构建 `2a31955` 的联动游玩中，“这次没有变模糊了”，
手感可接受、HUD 正常。仍有植被闪烁/贴图瞬移，单列为时域局部问题继续定位，
不因这一反馈宣称所有运动伪影均已解决。来源为 `play-20261022-141640` 与玩家录屏。
