# 串联两遍候选

从 `nrbridge b13df1f` 建立 `research/multipass-serial`，不包含旧 A/B 实现。
旧实验只保留 [一页结论](MULTIPASS_ENSEMBLE_NEGATIVE.zh-CN.md)。本候选尚未 GPU 验收，不能视作画质或性能成果。

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
bash tools/nr_dev/play-serial.sh "$NRBRIDGE_HOME/localinputs/serial.json"
```

入口沿用 daily 准入、GPU flock 和正常退休，只允许配对 Native/Helper；首版不启用可变分辨率。
旧 Helper 仍可走普通单遍入口；此实验入口要求新 Helper。没有低 B、融合、更新频率变化或更多遍数。

## 实机验收未完成

需同场景比较一遍/两遍最终截图或短视频、真实 Present FPS、scoped NR 时间和设备显存；
验证 1→2→1、两遍首帧 reset、断档 reset 及正常退出。新增 trace 要与实际 GPU 输出/截图一起检查，
单遍还需与原可玩产物对照。帧率应同时报告 origin 更新/接纳情况，不能只报 Evaluate 毫秒。
不绕过现有 HOLD 或设备保护；本机结果交用户判断，通过后才另行合入 nrbridge。
