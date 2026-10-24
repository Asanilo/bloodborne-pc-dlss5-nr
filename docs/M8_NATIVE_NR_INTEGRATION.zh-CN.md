# M8 公共资源与 M6 Native NR 接线契约草案 v1

状态：CPU 接线准备，尚未接入游戏、启用后端或修改公共 Scheduler。
基础实现为 `/home/arinp/Code/dlss_vulkan` 的 `9761aa571c52b1c909d2fdc4d8108759b59f4731`，
`src/native_adapter.h` ABI1 / COMPOSITE_REVISION3；详细输入和生命周期以该版本
`docs/M8_NATIVE_NR_HANDOFF.md` 为准。现有 Wine NRBridge 继续承担已实测路径。

## 归属和接线位置

M8 负责 `gpu/vulkan/instance.*` 的设备请求合并、`gpu/render/scheduler.*` 的真实提交排序、
资源租约和异常退休。NR 线负责游戏 HDR/深度/运动/相机到 ABI1 的转换，先实现独立 prepare pass。
FG 线继续独占 FG 后端、Presenter 和 MFG generation 调度；不要求 FG 等待 Native NR 后端完成。
M9 GPU 阶段不修改其专属文件，也不重建其正在使用的公共 Native。

现有 `NrDimensionAdapter` 是 Wine M5 尺寸/资源转换器，不是 M6 推理器；不能直接替换类名。
后续独立实验后端可使用 `NativeNrAdapterSession`（拟新增）封装 C ABI，明确配置后才启用。
设备要求不满足、NR_BUSY 或输入未就绪时选择既定 source fallback；未知 GPU 完成不能进入正常回退清理。

## 最小 Frame Context

先统一只读身份与完成依赖，保留各后端实际拥有的图像。每份资源描述至少包含：

| 字段 | 生产方必须提供的事实 |
|---|---|
| identity | 实际 source frame、resource generation、history epoch、parameter epoch |
| phase | EarlyHDR / NativeNrEncoded / NativeNrOutput / LateSR / Display；同一 allocation 不等于同一内容 |
| geometry | guest render subrect（含偏移）、NR execution extent、SR/display output extent，三者分开 |
| image | image/view/format/usage/aspect/mip/layer、当前 layout/stage/access、queue family |
| producer | 实际记录产生内容的 semaphore/value，加上 host submission admission 状态 |
| consumers | 每个实际消费者的 completion semaphore/value；在完成前保留 owner、view 和生产 semaphore |

图像身份由 owner 给出，不能用 PS4 地址或 VkImage 句柄单独代替 generation。内容身份不证明 GPU 完成。
现有 `FgContract::DisplayStamp` 保留独立 source/address/tick/generation/extent 的一次消费规则，
不能将 NR ledger 是否存在作为 FG 开关，也不能用新 context 放松 source+1 匹配。

同 queue、同内容阶段且生产/消费顺序已证明的 Motion view 可以借用；跨帧 NR 输入继续保持不可变。
Depth、Color、Motion 的一次转换仅在 format、subrect、phase、epoch 和使用期限完全相同时才复用。
8GB 预算分开统计 guest mirror、texture cache、owned snapshot、NR workspace、FG generations 与 retired bytes，
不能只因 allocation 地址相同而少算，或因 retired bytes 尚存而直接判定泄漏。

## NR 线可立即实现的 prepare 契约

输入为同一真实 source/phase 的不可变 HDR、Motion、Depth 与相机参数；输出为调用方拥有的四张独立图像：

| 图像 | ABI1 数据契约 |
|---|---|
| Color | execution extent，RGBA8_UNORM/RGBA16F/RGBA32F，display-sRGB 编码数值[0,1]，禁止 sRGB view 隐式解码 |
| Motion | execution extent，RG16F/RG32F，current→previous，execution pixels，排除 jitter；只做一次像素域缩放 |
| Depth | execution extent，RG32F，x=当前正向 view-Z，y=同表面在前一相机的预期 view-Z；y≤0拒绝历史 |
| Output | execution extent，RGBA32F storage，不能 alias 任一输入；resolve 后再交给现有 SR/HDR 路径 |

M6 单轴范围为2..1920 × 2..1080；现有 M5 的“面积≤1080p”契约不够，例如超宽 execution 即使面积合法，
宽度超过1920仍不能交给 ABI1。高分辨率 SR output 独立于该范围，不能让 output 随意扩张 NR workspace。
不得将 D32 原始值、reverse-Z 或当前 view-Z 复制到 Depth.y 冒充前一相机结果。

相机矩阵也有明确边界：`previousView * inverseCurrentView` 只能得到当前世界点在前一相机的 Z，
不能恢复沿 Z 运动或骨骼变形表面真实的前帧 Z。当前 object motion RGBA 只有位移、valid 和当前 written depth，
没有 previous-object Z。首版使用 `CameraMotion::ObjectMotionView/Image` 中与最终深度匹配的有效 object 标记，
对这些像素设 Depth.y≤0，保守拒绝历史；只有新增真实上一帧物体位置/深度来源后才能扩大接纳范围。
无效、被覆盖或不能确定归属的表面同样保守处理。`depth>=1` 的 sky 是无穷方向，
不能作为有限几何直接反投影求 Z；首版显式 reactive，不借用 camera-motion shader 内用于方向计算的 z=1 当真实深度。

`jitterDelta = currentSampleOffset - previousSampleOffset`（execution pixels）。bbport 的既有
previousViewport-currentViewport 约定先还原为真实 sample/UV 定义，再决定符号；禁止仅凭变量名取反。
现有 `camera_motion.comp` 明确 `scene_uv=uv-jitter/size`、`MV=(prev_uv-scene_uv)*size`；
相同 viewport 定义及 execution 像素缩放下，历史 raster 坐标应为：

```text
previousRaster = currentPixelCenter + MVexec + previousViewportExec - currentViewportExec
M6 temporal pos = currentPixelCenter + guide.xy + jitterDelta
```

因此接线传入 `jitterDelta=previousViewportExec-currentViewportExec`；它与 ABI 文档的
currentSample-previousSample 仅在 sampleOffset=-viewport 的定义下等价。不能另取 SR Halton 值，
也不能先把同一偏移加进 Motion 再传一次 jitterDelta。
相机矩阵不可得时明确拒绝真实相机历史接线，不能把正交 fixture 当作游戏透视相机证明。
HDR 编码与输出 resolve 共用明确参数 epoch；reset、尺寸/相机断点、采样相位变化必须使历史失效。
M6 输出 alpha=1，resolve 回游戏时保留原游戏 alpha，不能整像素直接覆写。
现有 `nrbridge/source-provenance.json` 记录当前 HLSL 不对应冻结 SPIR-V 的原始源码，重建验证未执行；
不能将直接重编当前 HLSL 当作 Wine Encode 数值规范。应固定真实执行 proxy 的变换映射，
用同一输入核对编码/resolve 数值，再决定共享实现。

Prepare 只记录转换，不创建 Wine/FD、不自行 submit/wait、不释放 borrowed 图像。
返回 owned views、准确 before/after 状态、source/phase/geometry 和 producer tick，供 M8 建立租约。
NR 线可独立开发此 pass，不必修改 Scheduler、Presenter 或 FG 代码。

## 用现有 Scheduler 建立无环提交

`Scheduler::Flush()` 在异步模式只安排录制/提交，返回不证明 producer 已进入 queue。
已有 `WaitSubmitted(tick)` / `IsSubmitted(tick)` 正好提供 host submission admission，
不需要新增每帧 GPU fence 等待，也不需要为接线扩写整个 Frame Graph。

首版在同一 graphics queue/family，单 producer 线程按以下顺序接线：

1. 完成 prepare 记录，保存 `producerTick=CurrentTick()` 与 `GetWorkSemaphore()->Handle()`；Flush。
2. 在不持 `Scheduler::submit_mutex`、不占其提交 worker 的位置确认 `WaitSubmitted(producerTick)`。
   这是 CPU 提交排序，不是 GPU 完成；不能以它退休输入。若从提交 worker 调用会形成自等，禁止。
   若接线位置不能安全阻塞，使用 `IsSubmitted` 保留 pending 租约并按既定策略跳过该帧；
   不能在 GPU completion 未知时以超时销毁资源。现有无期限 WaitSubmitted 不能兼任异常恢复协议。
3. `nrAdapterRecord` 接收 producer wait 和 output 再使用依赖；成功后保留整个 NrBatch 的有效生命周期。
4. 持公共 `submit_mutex` 和既有 Q0 owner scope，使用实际设备 dispatch 对 batch 原样 `vkQueueSubmit2`，
   随后将真实 VkResult 交给 `nrAdapterSubmitted`。不能先交成功、不能 host-signal NR timeline。
5. 仅成功接纳后，后续 renderer consumer batch AddWait(NR completion/value)，执行 resolve/下游使用，
   signal renderer timeline；调用 `nrAdapterRetireAfter` 登记该 completion semaphore 的实际最后使用点。
6. 同 queue 的 consumer1 必须先于会等待它的 NR2 提交；generation 在所有生产与消费完成前保留。

实际 `SubmitInfo::MaxSemaphores=4`，M6 ABI允许最多6个 caller waits。首版通过 prepare 将输入依赖汇聚到
一个 renderer producer timeline，另加 output reuse，NR→renderer 只增加一个 wait；接线前检查总容量，
不能把6个依赖直接塞进4槽数组。无必要不扩大公共数组或重复等待同一 timeline。

当前 renderer consumer waits 使用 AllCommands，覆盖 transfer/compute 最早读取；首版保持该正确性。
减少 stage 范围须另有真实 producer/consumer hazard 证据，不能仅凭推理 pass 是 compute 就缩窄。
跨 queue-family 路径留给后续显式 release/acquire，ABI1 本身不代办所有权转移。

## 设备、动态重建与异常退休

在 `vkCreateDevice` 前取得 `nrAdapterRequirements`，按 sType 合并既有 feature 位与扩展名，
不重复 Vulkan11/12/13 节点、不改写 Adapter 返回的链。owner 保留到创建返回再释放。
保存实际 enabled 清单；`requirementsEnabled`、`cudaLaunchEnabled` 只从实际创建结果赋值。
物理设备支持枚举不证明当前 VkDevice 已启用，不能把 PTX 路由能力默认为 true。
首版不把 M6 依赖强加到未选择该实验后端的默认 Wine/FG 路径。

冷 Configure/resize 先停止新租约，完成已录制批次处置，排空自身提交和已登记消费者，再事务性准备下一代。
按新旧代可能同时存活计入峰值预算；未提交 batch 可 Cancel，提交后只能按真实完成退休。
Destroy 返回 Busy 时持有句柄及租约，禁止用进程缺失或 Helper 停止替代 GPU 完成。
Create 返回 DEVICE_LOST 且句柄非空时保留隔离句柄，不能当作普通空指针失败丢掉。
真实 device loss、未知提交结果、项目 HOLD 分别记账；设备/队列 waitIdle 不作为逐帧调度方案。

## 分批落地及验收顺序

先落设备 feature 协商和独立 prepare 数值/身份验证，再接同 queue 的 Record→Submitted→consumer→RetireAfter。
只有真实游戏 HDR/相机转换与 completion 通过后才开放实验开关；Wine 路径保持可显式选择。
M6 独立 PoC 的约12.8ms不是游戏性能或替换通过证明。

M9 完成 Old 多尺寸/MFG 后，M8 先用同一已封存 Native/Helper 测 New1440Q x2 的独立 display source 修复，
再分离 SR-only 与 NR→SR，按实际边界推进3X、4X–6X及16:10/超宽动态重建。
每次记录 actual memory model、三种 extent、generation、NR选择率、实际 generated Present/顺序、
完整 validation 累计与正常退出证明。8GB峰值/区域切换和 Old/New 热对照使用同一构建与相同场景。
本草案没有将上述待接入/待实测事项写成已完成。
