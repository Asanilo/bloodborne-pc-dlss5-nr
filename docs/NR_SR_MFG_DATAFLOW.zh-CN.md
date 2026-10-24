# 玩家基线的数据流核对与后续验收边界

核对对象为 `d47d5f6` 的 M4/M9 实现，不以 M1 设计说明替代源码。
本记录是源码审计；尚未完成静态高亮、慢转镜、快转镜/植被的四态同步 ROI 对照，
因此不宣布闪烁根因或画质修复。M6 仍暂停，未调用其他 Agent，未增加亮度或历史启发式。

## 1. NR 的模型调用与真实帧历史

NR Helper 是异步稀疏更新，不是每个真实游戏帧一次 Evaluate。
`NativeAsyncPreSr::TryCaptureOrigin` 先为当前真实帧调用持久历史 `BeginCurrent`，
随后才处理有界请求链；请求链忙或不能复用，不等于当前视觉历史停止推进。
`RecordPersistentAdvanceAndApply` 仅在 peer Ready 且 origin、租约满足条件时预约模型任务；
Complete 后核验 session/resource generation/job/source/history/params、Native 输入完成及
Helper GPU serial。正常有效候选必须满足 Evaluate=1、Resolve=1、model reset=1。
候选沿真实帧链追赶后才可进入历史；预期 skip 或过期候选不会被当成当前有效结果。

每个受支持真实帧的 `Apply` 组合当前颜色及重投影后的持久 edit/meta；GPU 按像素判断
有效性、年龄和回退。`cache_selected=1` 只表示主机端有历史可用，不证明整帧像素都被改动。
不连续源帧、游戏历史/reset、参数变化和明确视觉 reset 会失效历史。
NR 模型重建必须先收束实际 GPU 使用并取得对应 ACK，期间冻结新读。

源码入口：[请求及应用](../gpu/render/native_async_pre_sr.cpp)、
[真实帧持久历史](../gpu/render/native_persistent_history.cpp)、
[重投影着色器](../tools/persistent_history/history.comp)。

## 2. SR 实际读什么

存在两条真实路径，不能统称为“SR 直接读 NR 模型输出”：

| 路径 | NR 与 SR 的连接 |
|---|---|
| 固定 1080p 原生 HDR 路径 | `RecordDlss` 默认原始 color；当前 `RecordAdvanceAndApply` 有效且 EffectSelected 时，改绑当前 ReturnImage。它是本真实帧组合后的结果，不是旧 origin 的模型输出直接送入 SR。默认 switching/domain_reset；实验 fixed/game_only 另有显式开关。|
| 多尺寸适配路径 | `RecordNrHdrPrepass` 在 HDR 场景中生成 NR 当前结果，由 dimension adapter 写回本帧 HDR scene/proxy。后续游戏 Tonemap；若走 RunScaled，DLSS SR 实际读本帧 LDR color。NR 未选择时保留该帧原始场景。|

1080p Native FG 选择固定渲染路线，FG 开关本身不应切换 NR 的 HDR 域。
尺寸变化另走资源代、布局及历史 reset；多尺寸 NR 会在新 FG 源域尚未建立时暂时 drain，
不是继续使用旧尺寸 guide。NR-only 不调用 SR；当前 NR-only+FG handoff 尚未支持。

源码入口：`vk_temporal_upscaler.cpp` 的 `RecordNrHdrPrepass`、`RecordDlss`、
`RunScaled`、`NativeFrameGeneration`，以及
[默认 SR 历史策略](../gpu/render/native_async_pre_sr_sr_policy.h)。

## 3. MFG 的颜色与 UI 位置

DLSS FG/MFG 的颜色是显示 pass 实际采样的**完成场景 LDR**，包括游戏后处理与 UI；
不是 NR HDR 输出，也不是未经 Tonemap 的 SR HDR 输出。
Native 路线从实际被显示 shader 采样的 native FG color 取图；scaled 路线从完成的
output-size ui_image 取图。仅尺寸一致不足以通过显示源关联。

`CaptureFrameGenScene` 提前固定同帧 depth/motion/camera；`OnDisplayPassStart` 再取最终颜色。
Helper 使用 RGBA8 UNORM，`ColorBuffersHDR=0`；HUDLess/UI/UIAlpha 为 null，
没有单独的 DLSS HUDless 或最后重新贴 UI 的阶段。FSR 存在独立 HUDless 捕获与选项，
不能用其行为解释 DLSS MFG。不能从“参数已设置”推导曝光/色彩及遮挡画质已经验收。

## 4. 2X–6X 的源帧与 guide 契约

同一真实源帧对 `(previous_source, source)` 产生 1–5 张 G。
Native 强制 guide_source 等于 color 的 source；协议核验 session、sequence、epoch、
源帧对、display、layout/resource_epoch 和 multiplier。源帧跳跃、首帧或超过250ms间隔请求 reset；
尺寸重建使用新资源代。跨 NR 候选 job 的身份不替代 FG 的真实源帧身份。

Helper 在同一真实帧对上依次设置 `MultiFrameIndex=1..N`、`MultiFrameCount=N`，
BackbufferFrameID 保持真实 source；深度与 motion 的 subrect 使用 render 尺寸，颜色使用 output 尺寸。
motion 为 RG16F，scale 为 `1/render_width, 1/render_height`，CameraMotionIncluded=1，
MvecJittered=0，DepthInverted=0；相机矩阵与当前 jitter 经协议逐帧传入。
这些是实际设置及身份检查，不等于已经逐像素证明 motion 符号、物体运动或 jitter delta 正确。
多帧 reset batch 不作为有效生成帧显示。

源码：[Native GPU bridge](../gpu/upscale/dlssg_gpu_bridge.h)、
[协议](../gpu/upscale/dlssg_gpu_contract.h)、
[Helper 调用](../tools/fg_dev/dlssg_stream.h)。

## 5. 历史与 Present 顺序

G 在 Helper Evaluate 和 Presenter 路径生成/编码/显示，不再次执行游戏的 NR/SR 渲染入口，
不推进 NR/SR 的真实帧历史。Presenter 只接受与已显示上一真实源匹配的 ticket，
等待实际 Helper/复制完成及 Native 编码最后使用；引用保留到实际编码 GPU 读完成。
顺序为 `R(previous), G1..GN, R(current)`；源帧不匹配、复用帧或输出不合格时拒绝 G。
节奏从图像实际 ready 后开始，使用真实帧间隔估计，晚提交不突发追赶。
这包含等待与显示延迟，Present 返回时间不能当成 scanout 或输入到光子延迟。

源码：[Presenter::Present](../gpu/render/presenter.cpp)。

## 闪烁的候选原因及最小取证

| 候选 | 必须取得的区分证据 |
|---|---|
| NR 稀疏候选采纳/历史逐像素失效造成亮度跳变 | 同一 source 的 raw、NR composed ROI，candidate job/age/reset 与采纳时刻；先确认变化已出现在真实帧。|
| NR 输入域切换触发 SR reset，或重投影/jitter/物体 MV 不一致 | NR 输出稳定而 SR-output ROI 变化；记录实际输入域、SR reset、尺寸、MV 与当前/前一 jitter。|
| FG 的遮挡、UI、植被运动或 LDR 高亮产生 G 专有闪烁 | 同帧 finished-scene/FG input 与 R/G1..GN 的 ROI；保持 source pair、epoch、index 身份，不能只看最终屏幕平均亮度。|
| 动态尺寸/reset 或延迟造成 Present 节奏问题 | R/G 身份、reject/reset 与 ready/Present 时间；区分缺 G、亮度跳变及物理显示抖动。|

四态为 NR Off/FG Off、NR On/FG Off、NR Off/FG On、NR On/FG On。
真实 NR Off 的基准用 `--nr off`；F8 只是效果 bypass，不能把它当作零模型成本。
同一私有存档/相机下只保留短序列与 ROI，先定位变化最早出现的阶段，再决定修复。
当前没有证据支持通过额外高亮保护、历史指纹或恢复 M10 来掩盖现象。

## B/C 当前证据边界

既有 `295dd7c` 同构建、同私有存档的 Old–New–Old、NR→SR+x2 数据，
1080p Old 为51.60/51.45真实FPS，New49.85；1440p Old41.45/41.65，New40.05。
New median 较慢，但 P95 没有一致恶化；每尺寸只有一次20秒 New 窗口，
不能将序列相关的逐帧样本当成独立重复试验，尚不足以宣称有统计显著的稳定回归。
设备级显存 New5731/5814MiB，Old1080为4417–4527MiB、1440为4997–5186MiB；
1Hz 采样不是瞬时峰值，也不是进程独占显存。尚无本轮性能优化收益可报告。
后续须补 native/SR-only、NR→SR FG Off、x2 的重复配对窗口及 GPU/CPU 等待分层。

SM 旧故障是 `m3-sm-r62` 普通关窗后实际 ErrorDeviceLost/rc23，未取得 GPU 正常退休证明；
不仅是会话隔离 HOLD，也未证明唯一原因在 NVIDIA 驱动。
旧 HOLD 保留，不能通过归还 prefix 伪造旧会话完成。本轮尚未重新启用 SM；
后续只在合法独占会话中，从应用 FG 全关的 SR-only 开始有限复验，继续与玩家入口隔离。

参见 [M3 原故障](FG_BACKENDS_M3.zh-CN.md)及[玩家入口与实测范围](PLAYER_PREVIEW.zh-CN.md)。
