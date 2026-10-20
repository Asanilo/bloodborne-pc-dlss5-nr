> Historical note: local paths and machine identifiers are redacted for publication.
> Original evidence is preserved in private development data; this is not a new acceptance run.
> Current player instructions are in the project README.

# 原生 DLSS SR 与 GPU IPC 审计（2026-10-10）

后续状态入口：[NR 开发交接](HANDOFF_DLSS_NR.zh-CN.md)。本页保留审计当时范围。
SDK / validation 已迁入主目录 `out/deps/dlss-sdk` / `out/deps/vulkan-validation`，
原 `out/` 路径保留兼容链接；迁移后 16 项检查重新通过，见 `out/audit/checks-after-dependency-move/`。
集成版后续部署与游戏实测、缓存/存档清理情况以交接文档为准。

结论：`testing` 已有原生 Linux DLSS SR，不需要再做一个 SR 后端。本工作区成功运行了
现有 bridge 的 DLAA / Quality 合成测试，以及两个独立 Linux Vulkan 进程的图像共享、
no-op 和 pass-through。**没有接入 NR，也没有验证 Linux → Wine → OptiScaler 的共享链路。**
原稳定工作区、`${HOME}/bloodborne` 的运行程序与配置、OptiScaler 项目均未修改。
未提交 commit、未推送、未合并或部署。

## 基线与可追溯性

- worktree：`.worktree/dlss-nr-audit`，分支 `dlss-nr-audit`。
- `testing` 与解引用的 `0.5-pre4` 都是 `10baf9df25f106da9013433a2e7348839baf917a`。
- 当前上游 `master`：`daa71654083b42ea0730b3f0fecee2fc20942d02`。
- SDK：NVIDIA/DLSS `374959484e79a640feaba44c93ac8cfb0a03f5b5`，310.9.1；
  NGX API 宏 `0x15`，Linux runtime `libnvidia-ngx-dlss.so.310.9.1`。
  SDK 下载在本 worktree 的 `out/dlss-sdk`，未纳入源码。
  runtime SHA-256：`7561f16cab74e2b7ccf791bf44bb9cc43abb44bcf78f33c5663520ad4e857e02`。
- 实测：RTX 4060 Laptop GPU，NVIDIA 615.71.09；Vulkan headers / validation 1.4.363。
- `out/audit/` 保存 API 查询、构建日志、SDK 指南文本、测试日志与 JSON 汇总。
- 为遵守“不改稳定工作区”，worktree 忽略规则只加入 `.git/info/exclude`，
  当前忽略 `.worktree/`，旧 `.slim` 目录已删除。没有改主工作区的 `.gitignore` / `.ignore`；`.worktree/worktrees.json` 是本地工作区登记信息。

## 原生 SR 代码核对

| 位置 | 代码现状 | 判定与限制 |
|---|---|---|
| `build.sh:51`、`gpu/dlss_bridge/CMakeLists.txt` | 可选 `DLSS_SDK_ROOT`，链接 `libnvsdk_ngx.a`，复制 release DLSS `.so` | 原版未固定 SDK commit；默认发布包不含 SDK/runtime。复现必须记录 SDK SHA、驱动和 runtime SHA-256。 |
| `vk_dlss.cpp`：`Dlss::Get` / 构造 | 从 `/proc/self/exe` 所在目录加载 bridge 和 `libnvidia-ngx-dlss.so.*`；`BB_DLSS=0` 禁用；校验 ABI=1 | 这是进程内 C ABI，不是 IPC。`HasNgx` 只看文件名前缀，不验证版本、ELF 或文件类型；不满足时返回空，菜单才解释原因。 |
| `AppendInstanceExtensions` / `AppendDeviceExtensions` | 查询 NGX 所需扩展，核对 specVersion；只允许 NVIDIA，查询 feature requirement | 扩展发现实际接到 Vulkan instance / device 创建路径。不能只看显卡品牌或菜单选项判断 SR 已运行。 |
| `Initialize` / bridge | ProjectID + Vulkan instance/physical/device/proc 回调；能力参数检查 available / needs_driver | `NVSDK_NGX_Feature_SuperSampling`，没有 NR feature 创建。失败返回并落到 FSR 3.1。 |
| `vk_instance.cpp:506–535` | 启用 storage-image write-without-format、bufferDeviceAddress、float16 等所支持特性 | 独立测试也必须启用对应特性。首次合成 harness 漏启，validation 报错，已修正；不能把该 harness 错误当成游戏渲染器缺陷。 |
| `vk_temporal_upscaler.cpp::Run` | Native AA 使用 HDR scene color（RGBA16F），不是最终 HUD 合成图；`hdr=true` | NGX 设置 IsHDR + AutoExposure。pre-exposure / exposure-scale 固定 1，没有显式 exposure 图；需实景亮度跳变验证。 |
| `RunScaled` | 使用 `ldr_target`，在 UI 阶段前执行；输入可能有对齐边缘，NGX render subrect 传实际 w×h | 传 `hdr=false`，不走 FSR4 的线性解码分支。SDK 明确允许且要求 LDR 路径传 0..1 感知编码颜色，**不能照抄 FSR4 解码并把它当作修复**。若实际 view 是 sRGB 自动解码格式，则需另行检查。 |
| depth | 游戏硬件 depth（D32 / D32S8 的 depth aspect），普通方向；没有线性化 | `CreateFeature` 传 depth_inverted=0，与 camera shader 的 depth 0..1、sky=1 约定相符。合成测试用 R32F 承载同义的设备深度值，不验证真实 D32S8 采样。 |
| motion | camera shader 重建相机运动；有效对象矢量按深度匹配覆盖；RG16F，render size | `(previous_uv-current_unjittered_uv)*render_size`，当前→上一帧，+x右/+y下；scale=1,1，MVLowRes=1，MVJittered=0，与 SDK 输入契约一致。透明物体、粒子、遮挡边缘仍受现有 MV 质量限制。 |
| jitter | Halton(2,3) 的 render-pixel 位移，通常 ±0.5；phase 按缩放面积增长 | Evaluate 传相同单位；调试 toggle bit26 可反转符号，验收时必须禁用该调试覆盖。合成测试只检查 API 接收，静止几何边缘和运动场景才能确认真正的抖动符号。 |
| reset | 初始化、特性重建、预设/输出/算法/jitter 变更、缺帧及 UI-only 返回时置位；成功后清除 | 这些路径存在。没找到专门的 camera-cut 检测；连续 3D 镜头跳切是否可靠重置仍需事件/画面验证。 |
| 资源与同步 | 输入/output 使用 GENERAL，运动写入后 barrier，output 后续再合成；重建前 `WaitSubmittedWork()` | 仅等待先前提交，不提交当前 command buffer；依赖“当前命令中不再引用旧 feature”的调用不变量。不能直接在此处异步跨进程引用原图像，必须另建 exportable 资源和所有权协议。 |

SDK 对颜色、MV、jitter 的要求参见固定版本的
[DLSS Programming Guide](https://github.com/NVIDIA/DLSS/blob/374959484e79a640feaba44c93ac8cfb0a03f5b5/doc/DLSS_Programming_Guide_Release.pdf)
§3.1、3.6、3.7、3.8，以及同版本 `nvsdk_ngx_helpers_vk.h`。

### 发现的问题与后续文件级修改方案

以下是审计发现和后续方案，**本次没有修改 SR 算法或实现新的后端**。

| 优先级 | 文件 / 函数 | 问题或风险 | 建议修改与验收 |
|---|---|---|---|
| P1 | `vk_temporal_upscaler.cpp::OnFrameStart/RecordDlss`、`.h` | `dlss_failed` 只在构造时为 false，失败置 true 后没有清除；切回 DLSS 时 `UseDlss()` 仍拒绝，菜单选择与实际 FSR 路径可能不一致 | 显式重试时等待相关 GPU 工作、释放/重建 feature、清除失败锁存并更新实际算法；用一次可控 Evaluate 失败→切走→切回验证。不要仅无条件清 flag。 |
| P2 | `gpu/dlss_bridge/bbport_dlss_bridge.cpp` | 上游仅创建/失败日志，没有成功帧的完整输入记录，NGX logging level OFF | **本次已加** `BB_DLSS_AUDIT` opt-in 日志，含 API/ABI、create flags、输入/输出尺寸格式、jitter、MV scale、reset、frame time、NGX result；默认渲染行为不变。 |
| P2 | `vk_dlss.cpp::HasNgx/Get`、`build.sh` | 文件名前缀不是运行库身份或可加载性验证；SDK 不固定 | 未来补实际路径、runtime hash/版本和缺失原因日志；构建记录版本，不把多个版本都默认为一致。 |
| P2 | `vk_temporal_upscaler.cpp::RecordDlss`、bridge、overlay | sharpness 仍传给 NGX，但 310.9.1 头文件已标记 sharpening 不支持 | 后续明确 UI 实际行为；需要锐化时单独评估既有后处理，不新增 SR 后端。本次不宣称此滑杆对 DLSS 有效。 |
| P2 | `vk_camera_motion.*`、`vk_temporal_upscaler.*` | 没有找到明确的连续镜头跳切 reset 触发 | 先记录相机/场景变化与 reset，再根据实际跳切补事件；避免用固定帧间隔 reset 掩盖错误 MV。 |
| P2 | `gpu/dlss_bridge/bbport_dlss_bridge.cpp` | capability GetI 返回值未逐项检查；feature 失败后的参数直到下次释放才回收 | 后续在局部错误路径完善诊断/清理，并用失败注入验证不会误报 ready。 |

本次新增的独立文件：

- `tools/nr_probe/nr_frame_desc.h`：实验性 v1 协议和严格校验。
- `nr_probe.cpp`：Linux↔Linux 图像/信号量导出导入与回读验证。
- `vk_test_context.h`：测试专用设备、队列、命令、有限时 fence 和 staging 资源。
- `dlss_sr_smoke.cpp`：只调用现有 bridge 的合成 SR 测试，不是另一套 SR 实现。
- `build.sh` / `CMakeLists.txt`：独立构建，不依赖游戏数据或主渲染器子模块。
- `run_checks.py`：正常/失败路径批量执行、超时、日志和 JSON 汇总。
- `check_dlss_log.py` / `test_log_check.py`：拒绝 fallback、缺帧、错误尺寸/格式/NGX result 等假成功。
- `run_game_case.sh`：可选的真实游戏隔离配置/存档副本与日志流程；本次未运行完整游戏测试。

## 既有 Wine IPC 原型

`tools/bridge_helper/bridge_helper.c` 的实际功能：

1. MinGW Windows helper 从命令行接收继承的 fd、字节数、device UUID。
2. 调 Wine 私有 `wine_server_fd_to_handle`，尝试把 Linux fd 转成 NT handle。
3. 创建 Windows Vulkan device，用 `OPAQUE_WIN32` 导入一个共享 buffer。
4. 拷贝到 host-visible/coherent buffer，检查 `0xB12D6E00 + (index&255)`，
   再把共享 buffer 写成 `0x5EED1234`。

它不是“已有 NR 桥”。在这两个上游分支中没有找到配套的 native producer 或可重复的
完整测试脚本。fd→NT handle 是否代表驱动可导入的 Vulkan 外部对象依赖具体 Wine/Proton
实现；不能从转换成功推导 Vulkan 导入成功。

缺口：不严格校验 argc/UUID/size；物理设备数组固定 8；queue family 固定 0；缺少外部
buffer / semaphore 能力协商；memoryTypeIndex 不是从 exporter 传入并验证；多个 Vulkan
返回值/函数指针未检查；无 semaphore 和 `VK_QUEUE_FAMILY_EXTERNAL` 所有权交换；只有
`vkQueueWaitIdle`，无有界 peer 协议/超时；无图像/布局/帧 metadata；资源句柄退出清理不完整。
本次保留该文件不变，不以替换原型的方式暗中接入 OptiScaler。

## NRFrameDesc v1 与验证边界

这是本项目自定义的**实验性传输协议**，不是 NVIDIA NR 的 API，也不假定 NR 的输入要求。
C++ 定义见 `nr_frame_desc.h`；字段仅固定宽度整数/IEEE float，无指针或 Vulkan handle。
限制为 little-endian、固定 424 字节，image 数组偏移 168，每项 64 字节；编译期断言防止 ABI 漂移。

| 字段组 | 约定 |
|---|---|
| identity | magic NRF1、version=1、bytes、session/generation/sequence；序号逐帧递增 |
| device | deviceUUID 与 driverUUID 必须匹配；不支持跨 GPU |
| geometry | render/output extent、4 个角色 Color/Depth/Motion/Output；v1 pass-through 要求 1:1 |
| semantics | linear scene color；正向设备 Z 0..1（R32F color-aspect proxy）；render-pixel current→previous MV，不含 jitter；preExposure=1；near/far、frame_ms、reset、jitter |
| image | format、extent、usage、tiling、layout、aspect、mip/layer、dedicated allocation size、memoryTypeIndex、offset=0、fd slot |
| sync | 两个 OPAQUE_FD **binary** semaphore，ready / done；单帧在途，资源代数变化须新建会话，不伪装支持 live resize |
| transport | 私有 AF_UNIX SOCK_SEQPACKET socketpair；首包 SCM_RIGHTS 传 4 个 memory fd + 2 个 semaphore fd，后续只传 metadata；不把数字 fd 当作跨进程句柄 |

此探针共享的是 optimal-tiling、single-sample、单 mip/layer 的 RGBA32F / R32F / RG32F /
RGBA32F 图像，usage **仅 TRANSFER_SRC/DST**。这避免把无关的采样/推理能力混进第一道门槛。
真正后端还需要验证 sampled/storage usage、实际游戏格式（RGBA16F、RG16F、D32S8）及对应
导入 view；本次没有验证那些组合。探针也不验证 D3D12 互操作、NR 输入语义或 UI 分离质量。

状态顺序：producer 上传逐像素/逐帧可识别数据 → release 到 EXTERNAL + signal ready →
worker wait ready + acquire → no-op 保持输出哨兵，或 GPU copy color→output →
worker 回读四张图验证、release + signal done → producer wait done + acquire →
回读输出逐值核对。CPU ACK 在 worker fence 后发送，用于诊断，不能替代 GPU semaphore/barrier。
所有图像做 dedicated allocation；import 成功即将 fd 所有权交给 Vulkan，未消费 fd 关闭，
正常退出销毁资源；失败时退出隔离进程，避免在未 signal 的队列上永久等待。
IPC poll 5 秒、GPU fence 3 秒、批量测试外层 45 秒截止。

对外部 fd 所有权的处理依据 [VkImportMemoryFdInfoKHR](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportMemoryFdInfoKHR.html)，
信号量使用 [VkImportSemaphoreFdInfoKHR](https://docs.vulkan.org/refpages/latest/refpages/source/VkImportSemaphoreFdInfoKHR.html)
的 permanent reference import；显式所有权和内存依赖参见
[Vulkan synchronization](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)。

## 可执行复现

下面命令均在本 worktree 根目录运行。源码开发依赖按照 `shell.nix` 准备；独立探针只需
C++20、CMake/Ninja、Vulkan loader/headers、Python，SR smoke 另外需要 NVIDIA SDK 和 RTX。

```bash
# SDK 固定到本次实际测试版本；若已有 out/dlss-sdk，不重复 clone。
git clone https://github.com/NVIDIA/DLSS.git out/dlss-sdk
git -C out/dlss-sdk checkout 374959484e79a640feaba44c93ac8cfb0a03f5b5
DLSS_SDK_ROOT="$PWD/out/dlss-sdk" bash tools/nr_probe/build.sh

# 只有本机缺系统 Vulkan headers 时，使用已存在的本地开发头文件：
VULKAN_INCLUDE=${HOME}/Code/bloodborne_pc/out/deps/root/usr/include \
VULKAN_LIBRARY=/usr/lib/libvulkan.so DLSS_SDK_ROOT="$PWD/out/dlss-sdk" \
bash tools/nr_probe/build.sh

# 本机 validation 仅解包到当前 worktree，未安装系统包。
VK_LAYER_PATH="$PWD/out/validation/usr/share/vulkan/explicit_layer.d" \
LD_LIBRARY_PATH="$PWD/out/validation/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
python3 tools/nr_probe/run_checks.py --validation \
  --bridge out/dlss-bridge/libbbport_dlss.so --ngx out/dlss-sdk/lib/Linux_x86_64/rel

# 没有 validation layer 时可去掉 --validation，但不能据此声称通过 validation。
python3 -m unittest discover -s tools/nr_probe -p 'test_*.py'
python3 tools/nr_probe/check_dlss_log.py out/audit/checks/dlaa.log --mode dlaa --synthetic
python3 tools/nr_probe/check_dlss_log.py out/audit/checks/quality.log --mode quality --synthetic
```

本机结果：16/16 子进程检查通过，包含 3 个 GPU IPC 正例、10 个故障注入、2 个 SR 正例
和 1 个缺 bridge 负例。所有正例退出 0；负例必须退出 2 并含指定错误码。
Khronos core + synchronization validation 已启用，最终正例无 VUID / sync hazard。
日志检查器的 2 项单测（含 9 组错误变体）通过。

| 验证 | 实际结果 |
|---|---|
| no-op | 4 帧；worker 读取输入，output 保持 -1 哨兵；双方回读一致 |
| pass-through | 16 帧；共享 GPU color→output，逐像素值和序号全部一致 |
| 奇数尺寸 | 127×73，4 帧，偏移按 16 字节对齐，验证通过 |
| DLAA | 960×540→960×540，quality=0，HDR input；10 次 NGX result=0x1，输出有限且颜色均值正确 |
| Quality | 640×360→960×540，quality=1，LDR input；10 次 NGX result=0x1，输出有限且颜色均值正确 |
| reset | 两组 SR 的第 1、6 帧 reset=1，其余为 0；jitter 每帧变化 |

SR 合成场景是恒定颜色/深度、零 MV，只验证 NGX SR 的创建/调用/输出/重置参数路径，
**不能验证游戏重投影、运动拖影、粒子、画面清晰度或 reset 对历史的视觉效果**。

### 明确的失败日志

`out/audit/checks/summary.json` 是最终机器可读结果，逐项日志在同目录。
负例中的 FAIL 是预期测试结果，批量 runner 只有看见预期退出码和错误码才记为 PASS。

| 注入 | 日志必须包含 |
|---|---|
| version | `E_ABI_VERSION` |
| uuid | `E_DEVICE_UUID` |
| format | `E_FORMAT_ROLE` |
| drop-fd | `E_FD_COUNT` |
| truncated | `E_PACKET_SIZE` |
| peer-exit | `E_PEER_INJECTED` |
| timeout（故意不 signal ready） | `E_VULKAN vkWaitForFences`、`VkResult=2` |
| corrupt | `E_READBACK role=0 pixel=0 channel=0` |
| sequence | `E_SEQUENCE_OR_RESOURCE_CHANGE` |
| extent | `E_EXTENT` |
| missing-bridge | `E_BRIDGE_LOAD` |

例如：`out/nr-probe/nr-probe --fault timeout`。错误包在 import 前拒绝；导入后的超时和回读
错误退出测试进程。本次没有注入 device-lost，也没有证明驱动挂死时可无损恢复游戏。

为保留修复证据，另存了两份 **harness 初版** 错误：
`out/audit/probe-before-alignment-fix.log` 的 `VUID-...07975`（奇数尺寸 staging offset），
`out/audit/harness-before-feature-fix.log` 的 `...03331/03324/08740`（未启用 BDA / storage write）。
这些不是最终通过日志，也不是上游游戏程序的实测缺陷。

## 真实游戏 DLAA / Quality 验证流程（尚未执行）

先在此 worktree 初始化依赖并构建完整游戏，不能借用稳定版的 `bb-probe/libbbgpu.so`：

```bash
git submodule update --init --recursive
DLSS_SDK_ROOT="$PWD/out/dlss-sdk" bash build.sh --test
# 若使用独立输出的 bridge，保证完整构建 out/bb-probe 同目录有该 bridge 和 release NGX .so。
BB_AUDIT_GAME_DIR=${HOME}/bloodborne/CUSA03173 \
BB_AUDIT_SAVE_BACKUP=/path/to/archived-backup \
bash tools/nr_probe/run_game_case.sh dlaa
# 正常关窗后，再运行同一存档副本、同一路线：
BB_AUDIT_GAME_DIR=${HOME}/bloodborne/CUSA03173 \
BB_AUDIT_SAVE_BACKUP=/path/to/archived-backup \
bash tools/nr_probe/run_game_case.sh quality
```

`BB_AUDIT_SAVE_BACKUP` 可省略（空白测试存档）；若使用则必须指向带 manifest 的归档备份，
脚本只复制它，所有配置、生成数据、缓存、存档写入 worktree 的独立 `out/audit/game-cases`。
脚本不会运行稳定版 `launch.sh` 或安装新运行包。测试时关闭其他游戏，避免占用与输入干扰。

- 固定 1920×1080、60 FPS、同一场景、相同镜头路线；先预热至少 30 秒，再各采样至少 60 秒。
  DLAA 预期 1920×1080→1920×1080，Quality 约 1280×720→1920×1080；GPU backing
  可能高于 render subrect（游戏对齐），不能把 backing extent 当成实际内部渲染尺寸。
- 日志先有 `DLSS: ready`，再有正确模式的 `AUDIT create`；运行 `check_dlss_log.py`，
  不能仅靠 FPS 或菜单里的 DLSS 字样验收。任何 fallback、NGX error、静止 jitter 或缺 reset
  都必须明确失败。审计日志逐帧打印，会干扰性能，性能测量另跑 `BB_DLSS_AUDIT` 未设置的对照。
- 使用脚本打印的 `touch .../dump-trigger` 抓 8 帧。检查 color 不含 HUD、depth 近/远/sky、
  静止 MV 接近 0、镜头平移的符号、人物移动的对象矢量、jitter 与投影的一致性。
  不用合成 smoke 的 R32F depth 替代这一步。
- 触发加载/传送、UI-only 返回、输出/预设切换和连续 3D 镜头跳切；对应首帧应 reset=1，
  随后恢复 0。观察细边缘、透明烟雾、头发、快速转向的拖影与闪烁，并保留原始 dumps/录像。
- 分开记录首次编译和热缓存；比较真实帧时间 P95/P99 与显存，不把 DLSS 调用成功当作质量提升。
- `BB_DLSS=0` 的禁用对照须另用独立 profile 运行，预期菜单不可用/回退；当前 game-case
  脚本刻意强制开启 DLSS，以避免“对照回退”被误算成成功。

## 上游是否有 DLSS NR 规划或代码

本次重新查询了公开的 `master` / `testing`、源码、路线图、issues/PR 搜索（包含 comments
中的 “neural rendering”）。在这个快照范围内未发现明确的 DLSS NR / DLSS 5 NR 功能或规划。
这不是对未公开分支或未来计划的保证。

- [PR #83](https://github.com/deadinside28/bloodborne_pc/pull/83) 是 **DLSS Super Resolution**；
  作者报告 RTX 4090、SDK 310.9.1 的 DLAA/Quality 测试，代码已出现在本次 testing 快照。
- [README 路线图](https://github.com/deadinside28/bloodborne_pc/blob/10baf9df25f106da9013433a2e7348839baf917a/README.md#roadmap)
  提到 XeSS/XeFG 的 Wine Vulkan memory helper、OptiScaler 风格输入映射、FSR FG。
- [ROADMAP](https://github.com/deadinside28/bloodborne_pc/blob/10baf9df25f106da9013433a2e7348839baf917a/docs/ROADMAP.md)
  把 native NGX SR、Wine helper 和 FG 分开讨论；“OptiScaler”字样不是 NR 已接入的证据。
- [Issue #94](https://github.com/deadinside28/bloodborne_pc/issues/94) 指向已有 DLSS/FSR 项目，
  维护者回复路线图链接，没有给出 NR API、模型、资源或执行路径。

下一道门槛应当是独立的 Linux↔Wine 图像和 semaphore 互操作测试，验证同一驱动下的
handle 转换、视图格式、所有权、超时和回收；再考虑真实游戏帧导出。当前没有调用真正 NR，
没有修改 OptiScaler，也没有新增 SR/FG 后端。
