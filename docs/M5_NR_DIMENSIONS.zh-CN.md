> M4/M5 已完整合入 `nrbridge`。当前统一入口、准入矩阵和合流回归见
> [主线集成说明](M4_M5_MAINLINE_INTEGRATION.zh-CN.md)。以下保留独立开发阶段记录。

# M5：可配置 NR 执行尺寸与 SR 输出

本开发线从 `nrbridge@464f4b6` 分出，保留未指定 M5 参数时的 1080p Quality / Old
基线。FG 新尺寸尚未集成，M5 模式明确要求 FG Off。现有 NR 310.9.16 Preset A/B
和旧模型封存配置独立保留；SR 的 Quality 等档位不改变 NR 模型 Preset。

## 构建和运行

Native 使用现有增量入口 `bash scripts/dev-build.sh --test`；Helper 按
[nrbridge 构建说明](../nrbridge/README.md#build) 编译为独立 PE。复用已有 SDK、
头文件、Wine 和运行时，禁止复制整套环境。Helper 构建不依赖外部 OptiScaler 工作树。
用现有 `tools/nr_dev/prepare_runtime_inputs.py` 为新 Helper 建立独立封存配置，
不覆盖旧配置。两端均需带 `NRBRIDGE_DIMENSIONS_V3R2` 能力标记。

```bash
# 自行准备的运行时依赖与存档；这些变量表示用户选择的绝对路径。
export NRBRIDGE_HOME=/absolute/private-nr-home
python3 tools/nr_dev/play.py --daily --native-build "$PWD/out" \
  --local-inputs "$NRBRIDGE_HOME/localinputs/m5-runtime.json" \
  --fg-backend off --nr-sr-preset quality --nr-output 2560x1440 \
  --nr-execution 1280x720 --session m5-1440-quality
```

需要现有存档时使用既有 `--continue-session` 或完整启动器的玩家存档入口。
既有 prefix 的复用仍走 `--reuse-nr-prefix` 的锁定、完整退休校验与借还机制。
进入实际可控制场景后按 F8 开启 NR，之后 F8 切换效果。Insert / L3+R3 图形设置可修改
SR 档位、输出分辨率和宽高比；Delete 打开 NR 参数。NR 不会因修改 SR 档位而自动换模型。

完整启动器新增默认关闭的 **Experimental NR dimensions (M5)**。开启后使用
图形页的 DLSS 档位、输出与宽高比，并可选择 NR 执行尺寸；没有匹配的 Helper
会在启动前明确拒绝。设置只写入本轮隔离配置，保留控制、语言和玩家其他选项。
稳定安装和 `runtime/current` 不由本功能切换。

## 三个尺寸域

- SR render：DLAA/Quality/Balanced/Performance/Ultra Performance 对输出按
  1/1.5/1.7/2/3 缩放，按 Native 实现取最近偶数。例如 1440p Quality 为 1706×960。
- NR execution：默认匹配 SR render，超出 1080p 像素数时等比下调；也可用
  `--nr-execution` 单独选择。8GB 机器优先从 1280×720 开始。
- SR output：使用游戏菜单已有的 4:3、16:10、16:9、21:9、32:9 输出列表，
  CLI 拒绝菜单不存在的尺寸，避免静默替换。当前输出预算上限为 4K 像素数，
  不代表所有允许组合都已验证能在 8GB 上游玩。

例如：NR 1280×720 可分别配合 SR 1706×960→2560×1440、1280×720→2560×1440，
或 DLAA 2560×1440→2560×1440。DLAA 仍调用 SR Feature。
严格 NR-only 使用 `--nr-sr-preset native`；此时 render=output，Native 不创建或
Evaluate SR Feature，NR HDR 结果直接进入游戏后处理和显示路径。
启动器开启 M5 后选择 Upscaler Off 即是这一模式，SR 的隐藏档位设置会保留。
NR-only 可以在图形菜单切回 DLSS SR；已有 SR Feature 在切到 Off 时等待真实 GPU
完成后释放。同一模型执行尺寸继续使用原 NR 会话，切换触发历史 reset。
`working_scale=1` 是模型内部约定；Native 输入适配与 OptiScaler WorkingScale 是两个概念。

M5 显式使用 `BB_LIVE_RES=1` 的动态 host target 路径。旧的启动分辨率补丁会采用
另一组对齐尺寸并限制 live 切换，因此在 M5 中拒绝混用。

## HDR、向量、历史与退休

Native 在游戏 HDR 处理边界取得实际 color/depth/motion，适配到 NR 执行尺寸；
Encode→Evaluate→Resolve 都用该尺寸。将 NR HDR residual 合回当前 render 图像，
保留原始细节和 alpha，再走游戏既有 tone conversion、Native DLSS SR 和 HUD。
GPU 全图有限值/F16 范围检测失败时，整个 HDR 帧保持原样。

Motion 按 previous-minus-current 像素定义，从 render 域转到 execution 域一次；
NGX MVecScale 保持 1/1。source viewport jitter 记录为 execution 像素，SR jitter
仍为 render 像素。NR phase/jitter correction 仍是 UNVERIFIED，不宣称已校正。

协议 v3 revision 2 的 INPUT 显式记录 actual render subrect、execution 和 SR output。
同一会话的模型/共享图像尺寸不可变。SR 切换在帧边界重建自身 context/图像，等待
真实 scheduler 完成后退休旧图像；源 reset 使旧候选与历史失效。更换 NR execution
需要先正常结束会话，完成 drain/CONSUMED/actual last-use/FD/STOP_READY/
NATIVE_RELEASED/Helper shutdown/reap，再创建新会话。未知状态仍 HOLD。

[协议细节与冻结规范勘误](../nrbridge/protocol/dimensions-v3.md)。

## 验证记录

- `6f6230b` / `m5-balanced-1080-r1`：实测 NR 1130×636→SR1920×1080，
  310.9.16 Preset B 请求/读取 2/2，1177 个任务完成（1176 captured、1 obsolete）。
  F8 off/on 和有界转镜通过；实际非复用 Present 约 71.4 FPS、P50 13.36ms，
  包含切换过程，不能视为纯档位性能对比。设备总显存一次采样 3951/8188MiB。
  rc0，`PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`。
- `cd88ea7` / `m5-1440q-live-r2`：1440p SR 显示正常，但 NR admitted=0，
  不算联动成功。正常退休通过。已据此修正启动动态尺寸和 scaled Present ledger。
- `0a616fd` / `m5-1440-live-r3`：下表七组配置在同一会话完成实际 NR→SR→Present；
  NR execution 固定 1280×720，310.9.16 Preset B。每次 layout 变化均记录 reset，
  随后恢复 NR cache_selected 的实际非复用 Present。F8 off/on 通过。
  2298 finalized（2284 captured、14 obsolete、0 skip），Feature create/release 1/1，
  rc0、正常 GPU/FD/model reap、无残留进程，prefix lease 已归还。
- `0a616fd` / `m5-nr1080-dlaa1440-r4`：新会话实际创建 **1920×1080 NR Feature**，
  使用新的执行尺寸/共享图像/历史；输入及输出为 2560×1440 DLAA。
  329 finalized（326 captured、3 obsolete、0 skip），Feature create/release 1/1，
  rc0、正常 GPU/FD/model reap、无残留，prefix lease 已归还。

| NR execution | SR 档位 | SR render → output | 短窗 FPS / P50 | 设备总显存采样最高 MiB |
|---|---|---|---|---|
| 1280×720 | Quality | 1706×960 → 2560×1440 | 57.8 / 16.51 ms | 4529 |
| 1280×720 | Performance | 1280×720 → 2560×1440 | 56.2 / 15.85 ms | 4614 |
| 1280×720 | DLAA | 2560×1440 → 2560×1440 | 44.9 / 22.25 ms | 5128 |
| 1280×720 | Balanced | 1130×636 → 1920×1080 | 66.7 / 13.94 ms | 4501 |
| 1280×720 | Quality（16:10） | 1280×800 → 1920×1200 | 67.2 / 13.94 ms | 4527 |
| 1280×720 | Quality（21:9） | 1706×720 → 2560×1080 | 62.7 / 15.04 ms | 4583 |
| 1280×720 | Ultra Performance | 640×360 → 1920×1080 | 70.8 / 13.24 ms | 4496 |
| 1920×1080 | DLAA | 2560×1440 → 2560×1440 | 38.4 / 24.35 ms | 6083 |

采样窗口为 6–8 秒，含切换、预热和采样开销，地点为同一存档场景。
这些数字用于判断能否游玩，不能据此推导严格的档位性能排名。
NR720 的 Helper GPU 时间中位值约 Encode 0.16–0.18 ms、Evaluate commands 7.48–7.66 ms、
Resolve 1.36–1.40 ms、总计 9.04–9.24 ms；这是同一 Helper 队列命令及依赖时间，
不等于纯模型推理，也不等于整帧 GPU 时间。

实机中 HUD 和场景正常显示，切换后没有黑屏或尺寸错位。保留有限值检测、原帧回退
和 NR 开关；亮尾、细部高光、AO/染光和时域闪烁仍需后续针对性画质验收。
不同 NR execution 尺寸的完整长时间场景/移动覆盖也尚未完成。

CPU 验证包含 NR 工具 309 项、完整 NR 启动器/打包入口 60 项，以及 Helper 的协议、
runtime config、ring64、动态尺寸和退休测试；Native 增量构建与原有 CPU 测试通过。
新增路径最后的资源所有权调整随后由上述 r3/r4 实机验证覆盖。

## 严格 NR-only（补充验证）

`13879fe` / `m5-nr-only-live-r5` 使用相同 R2 Helper / Preset B，NR execution 1280×720。
先运行 NR-only，再修改输出尺寸、启用真正的 SR，最后切回 NR-only：

| 路径 | 实际 render → output | 短窗 FPS / P50 | 设备总显存采样最高 MiB |
|---|---|---|---|
| NR-only | 1920×1080 → 1920×1080 | 67.2 / 14.10 ms | 3780 |
| NR-only | 2560×1440 → 2560×1440 | 53.4 / 18.12 ms | 4376 |
| 恢复 NR→SR Quality | 1706×960 → 2560×1440 | 59.4 / 15.57 ms | 4383 |
| 返回 NR-only | 1920×1080 → 1920×1080 | 65.5 / 14.50 ms | 4383 |

初始两组短窗分别有 414 和 323 个真实 Present，均匹配同源
`NRBRIDGE_NATIVE_WITHOUT_SR_V1` 记录，Native SR Evaluate 调用数为零。
切换中原帧回退后恢复 NR cache_selected；F8 off/on 通过。
2853 finalized（2825 captured、28 obsolete、0 skip），正常 GPU/FD/model reap、rc0、
无残留，prefix lease 已归还。采样窗口含切换，范围与前表相同。

NR-only 没有 DLSS 的时域抗锯齿，细线条锯齿可能明显，不能把它当作与 DLAA 等质。
NR-only 的原 NR→SR→Present 三阶段冷捕获流程不在本轮验收范围；该流程需要 SR
输出，暂不在 NR-only 使用。普通截图、NR 图像/任务记录和游玩/F8 不依赖该流程。

新增 Native 能力标记 `NRBRIDGE_NATIVE_WITHOUT_SR_V1`：旧的尺寸 Native 被明确拒绝，
而 Helper v3r2 和共享协议完全不变。NR-only 的 Native 使用记录包含实际后处理记录 tick，
不伪造 SrReadRecorded；normal last-use、CONSUMED 和终端退休仍使用实际 Native 队列。
CPU 回归更新为 311 项 NR 工具、61 项启动器/打包入口，Native 增量 --test 通过。

## 提交与集成边界

- `6f6230b`：版本化尺寸协议、Helper/Native 固定执行尺寸参数化与配置选择。
- `cd88ea7`：HDR 适配、实际 render subrect、MV 域转换及 live SR 路径。
- `8878c91`：强制 live host 尺寸路径、scaled display 的真实准备记录绑定。
- `de2272d`：默认关闭的启动器选项与 CLI 转发，便于和 M4 启动器独立合流。
- `0a616fd`：适配器与 render Motion 的 HOLD 生命周期、按需采样。
- `13879fe`：严格 NR-only、和 SR 的运行时切换、旧 SR Feature 的实际退休。

`gpu/render/presenter.cpp`、FG mailbox、FG backend 和多帧输出代码未修改。
`vk_temporal_upscaler.cpp` 仅新增 M5 开关保护下的 NR prepass、SR 后的 NR 记录和
scaled display 记录绑定。新协议 INPUT 只增加 source_layout，既有 wire 1/2 的
字段和冻结 effective-spec 保持原身份。共享 NR execution 资源仍为每会话固定尺寸。

运行日志中的 `raw_or_cache720_SR1080`、`encode720_ms` 等是保留的旧诊断字段名，
不能用它们判断新 profile 的尺寸；以 CONFIG/SHARED、Feature 创建结果和实际
`NR_DIMENSIONS_SOURCE_LAYOUT` 为准。代码不以旧字段名替代 extent 校验。

开发构建使用本工作树一份可变 out，约 740 MiB；原 SDK/工具链/Wine/prefix 复用，
没有新增 AppImage 或完整运行环境副本。运行日志/截图/显存采样和私有存档留在
被 Git 忽略的运行目录；正式安装、回退安装和主线不在本阶段切换。

这些记录是功能验证，不是完整画质验收。模型专有分配和驱动占用不能从 Native
图像预算推导；记录的设备总显存也不是 Helper 独占峰值。New memory、FG 尺寸适配、
NR phase correction 和全组合画质矩阵不包含在本次实现中。
