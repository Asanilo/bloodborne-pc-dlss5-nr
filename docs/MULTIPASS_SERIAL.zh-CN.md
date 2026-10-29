# 串联两遍候选

从 `nrbridge b13df1f` 建立 `research/multipass-serial`，不包含旧 A/B 实现。
旧实验只保留 [一页结论](MULTIPASS_ENSEMBLE_NEGATIVE.zh-CN.md)。独立一遍/两遍已实机运行并正常退休，
运行中切换也已通过正常退休。用户现场反馈“看到收益，继续做合入评估”；这是用户主观验收，
不作为客观画质改进证明，尚未合入 nrbridge。

## 数据流与参考

参考本地 OptiScaler-Linux-NR `bd76e9d3` 的
`OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp:3290` 串联及 `:3507` 最终 Resolve，
`NrGpuLifetime.cpp` 的创建提交、持有及退休，`Config.h` / `DlssNr_Menu.cpp` 的 Passes。
这里将该拓扑接入现有 Wine Helper 的 Vulkan NGX forwarder；没有新增 D3D12 后端。

`原始 HDR → 冻结 Encode 一次 → P → Feature1/raw0 → Feature2/raw1 → 冻结 Resolve 一次 → Native temporal/SR`

P 和 Encode 保存的原始 HDR 保持不变，Resolve 对照 P 与最终 raw1。没有中间 Resolve/Encode。
raw0/raw1 两张私有工作纹理交替，第二遍的输入 wrapper 只读；写后读有实际 image/memory barrier。
首遍仍读 RGBA32F Encode 输出、写 RGBA16F；第二遍读首遍 RGBA16F，写独立 RGBA16F。
Encode/Resolve SPIR-V 字节不变，不宣称 HLSL 已验证等价。

两遍同分辨率 1280×720、同模型文件、同 preset / Style / Intensity，当前帧深度/MV相同。
独立 Feature、独立参数块及模型历史。首次/切换/重建/epoch或history变化/源帧断档重置两遍；
只有相邻源帧才延续历史。单遍保持原有 Reset=1。当前异步稀疏调度下，实际可能每作业都因断档重置，不能宣称获得连续时域累积。

## 切换和所有权

默认一遍。在 NR 面板的 Model parameters 中选择 NR passes=1/2，再按现有 Apply。
切换先排空实际 Native/GPU leases；Feature 在单独提交并观察 fence 后才能 Evaluate；ACK 后重置视觉历史。
第二 Feature 仅双遍创建、独立释放/销毁参数，scratch 保留到安全退出；一遍不分配该 scratch。
Encode/Resolve 常量独立，每 command buffer 各写一次；描述符仅在前次 fence 与 CONSUMED 后复用。
两遍完成记录实际调用的输入/输出/Feature handle，Native 检查 raw0 output == pass2 input、
输入输出不别名及两个 Feature 不同，再接纳一次 origin 结果。资源身份日志不是像素内容验证的替代。
终端严格核对 Native 已逐作业验证的 Evaluate 累计、每作业一次 Resolve 以及第二 Feature 退休。

## 配对和入口

Helper 为独立 OptiScaler 仓库 `research/multipass-serial`，具体提交见 Native 的
`gpu/render/native_runtime_model.export.json`。现有明确模型预设 runtime-v2 继续支持，两遍使用同一预设。
构建输出、runtime inputs、prefix、存档和 runs 必须位于私有 candidate home；不覆盖正式安装。

```bash
export NRBRIDGE_HOME=/tmp/nrbridge-serial-candidate
bash tools/nr_dev/play-serial.sh "$NRBRIDGE_HOME/localinputs/serial-r2.json" --nr-passes 1
# 显式双遍起始选择通过相同的 Apply 流程生效；运行中仍可用面板切回一遍
bash tools/nr_dev/play-serial.sh "$NRBRIDGE_HOME/localinputs/serial-r2.json" --nr-passes 2
```

入口沿用 daily 准入、GPU flock 和正常退休，只允许配对 Native/Helper；首版不启用可变分辨率。
旧 Helper 仍可走普通单遍入口；此实验入口要求新 Helper。没有低 B、融合、更新频率变化或更多遍数。
原生鼠标沿用 nrbridge 的 daily 默认，不在入口中覆盖。此前三个实测 run 被工程师误加了
`--joystick-mouse`，因此这组计时对应兼容鼠标模式。后续 `serial-native-mouse-r1` 去掉覆盖，
实际环境 `BB_NATIVE_MOUSE=1`，日志确认 `NATIVE_MOUSE status=installed requested=1 installed=1 route=native`。
用户不要求追加验收，该轮在启动 NR 前正常关闭，不能将其当作额外 NR 性能实验。

## 2026-10-30 实机阶段结果

实测二进制 Native 源码 `45ab4cb9d879cfdc4a6d2004bcc55f8f759dbe8f`；配套 Helper
`6765901b200998849921843962d831ecd718a6a0`，PE SHA256
`e6ae9ca03388111b1cb006bf5d051bd18b9b999ccf430a027248dee740c314c4`。
两遍均使用现有 `310.9.16-SF` 模型、preset B、Style 0、Intensity 1。
1280×720 NR → 1920×1080 DLSS Quality，90fps 上限，FG/SM 关闭。
私有存档实际加载在雅南露台；两轮同 seed，静止 6 秒，再执行相同 4.7 秒转镜。

| 同一场景观测 | 一遍 | 串联两遍 |
|---|---:|---:|
| 全轮作业 / Evaluate / Resolve | 1510 / 1510 / 1510 | 1335 / 2670 / 1335 |
| captured / obsolete | 1509 / 1 | 1333 / 2 |
| 静止真实 Present FPS | 79.58 | 76.74 |
| 转镜真实 Present FPS | 77.07 | 73.83 |
| 静止 Present p95 间隔 | 16.360 ms | 19.443 ms |
| 静止 Helper 作业 GPU 中位数 | 10.336 ms | 17.611 ms |
| 静止第二遍 Evaluate 区间中位数 | 不执行 | 8.577 ms |
| 静止 NR 作业数 / 全部 reset | 80 / 80 | 70 / 70 |
| 静止 origin 源帧间隔中位数 | 6 | 7 |
| 静止设备显存采样峰值 | 3864 MiB（3 样本） | 4098 MiB（6 样本） |
| 正常退出 / GPU、FD、模型退休 | PASS / exit 0 | PASS / exit 0 |

GPU 时间是同队列命令及依赖区间，不是纯推理成本。两遍静止作业约增加 70.4%，
真实 FPS 约降低 3.6%；异步重叠隐藏部分耗时，NR 更新间隔也增大。单遍/双遍 Resolve
scoped 中位数分别为 1.375/0.181 ms，同一冻结 shader 的排队区间不同，不能解释为 shader 加速。
显存为整设备粗采样，约 234 MiB 差额不能全部归属第二 Feature；显式 raw1 约 7.03 MiB。

两轮全部静止 Present 都记录 cache_selected=1。两遍的 1335 条实际调用绑定记录满足
第一遍 output == 第二遍 input，两个 Feature/输出不别名，最终 Resolve proxy == 初始 Encode 输入。
第二 Feature 创建/释放各一次、参数销毁、Native 精确累计和最终退出均通过。
最后内核 GPU 故障检查通过，项目锁可取，boot receipt 为正常退休；没有清理 HOLD。

[一遍原始窗口截图](assets/multipass-serial-1pass.png) ·
[两遍原始窗口截图](assets/multipass-serial-2pass.png) ·
[数字、时间窗和证据 SHA](multipass-serial-20261030.json)。
截图保留原始像素，云、光照及 idle 动画不同，没有同源真值或重复交换顺序实验，
不能用差分分数宣称细节、材质或稳定性改善。效果交用户查看。

运行目录在 `/tmp/nrbridge-serial-candidate/runs/{serial-one-r1,serial-two-r1}`，
包含最终图、Native Present/Helper 时间戳、退休日志及明确场景时间窗。
实际 FPS 从按顺序拼接的 `.log.1` 与 `.log` 中裁剪；没有因轮转漏掉静止窗口。
独立构建输出在该 private home 的 `build/native` 和 `build/helper-runtime-model/c5c0ee9327a72721`。

## 用户现场切换与评估边界

独立两遍启动已通过相同的 Apply 路径完成 1→2，首次和源帧断档 reset 已观察到；
用户在 `serial-switch-r2` 实际完成 2→1→2，并继续在两遍模式下切换 Style 2→1→2。
六个 generation 的 GPU ACK 全部完成；首次 Evaluate 均 reset=1。整轮 **6946** 个作业，
其中双遍 **5254**，Evaluate **12200 = 6946 + 5254**，Resolve **6946**。
第二 Feature 累计创建/释放 **5/5**、参数销毁、Native 精确计数和退出码 0 全部通过。
Intensity 全程仍为 1；后三次 Style 切换由用户操作，不参与前面固定 Style 0 的性能比较。

连续源帧 Reset=0 历史未在实机覆盖。
单遍冻结 shader 和原调用行为保留，但尚未与旧 Helper 做逐像素 GPU 等价对照。
三个运行均正常退休；两次准备阶段失败（缺少新 home 的 runs 父目录、借用前缀已归还原会话）
发生在游戏/GPU 启动前，分别修复入口目录创建及选择实际持有的已退休前缀，未清理 HOLD。
用户已指示继续 PR 合入评估；默认仍一遍，不自动合流或覆盖正式安装。
