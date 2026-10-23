# M4：RTX 4060 Laptop DLSS MFG 游戏集成

开发分支 `m4/dlss-mfg`，起点 `464f4b6`，恢复标签 `recovery/m3-464f4b6`。
复用 FG 工作树，未修改 `nrbridge` 或 M5 工作树。M4 拥有 FG/Presenter，
M5 的 NR 尺寸接入独立开发；当前 MFG 仍只支持 1280×720 guides → 1920×1080输出。

## 已实现接口

- 统一 DLSS 后端的高级设置 `fg_dlss_multiplier` / CLI `--dlssg-multiplier` 选择2–6。
  3X–6X 使用 `gpu-dmabuf-v1` + `consumer-queue-v1`；x2保留submit-ready和CPU回退。
  改倍率需重启，以新feature/history/资源池建立明确边界。NR仍独立由F8控制。
- 每真实帧对只递增一次request sequence；epoch仅在reset/域变化时改变。
  每组输出共享一次Helper真实batch fence。
  `MultiFrameCount=M-1`，索引1…M-1；同一真实BackbufferFrameID、camera和输入。
- 三张共享输入加1…5张独立DMA-BUF输出。每张输出逐项记录index/count/时间分母、
  Evaluate结果、实际GPU完成和EXTERNAL release。Native consumer复制全部输出，
  实际tick完成后才发布包含全部索引的ACK；不会把CPU录制完成当作GPU完成。
- Native保留最多两组输出资源，不增加游戏帧队列深度。ticket持有整组raw image，
  直到最后一次encoder实际GPU读取完成才释放；每个Present Frame分别保留有界encoded image。
- Present按索引递增，最后呈现真实帧。生成分支不推进NR、真实源编号或真实历史。
  正常按绝对deadline pacing；WSI迟到时重新定时，避免追赶式突发Present。x2保留原间距行为。
- 统计区分成功真实/生成Present，验证完整批次而非只看最终generated→real一对。
  成功Present返回数量不等于物理屏幕显示帧数，也不代表输入延迟。

## OptiScaler 只读迁移来源

实际仓库 `/home/arinp/Code/dlss5_linux/OptiScaler-Linux-NR`，`linux-nr@ca1c0287`。
`/home/arinp/Code/OptiScaler-Linux-NR` 不存在。未复制或修改外部工作树。

读取 `MfgUnlock.cpp`、`hooks/LibraryLoad_Hooks.cpp`、官方本地 NGX Vulkan头文件，
以及 `docs/fg-high-multiplier-test.md` 和 `testlogs/fg-eee98821/` 的已有实测。
沿用已验证的两个门限patch和Ada PTX时间位置修正，没有另造解锁机制。

首版只允许：

- `nvngx_dlssg.dll` FileVersion **310.9.1.0**，7460976字节。
- SHA256 `ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82`。
- advertise与validate各唯一匹配，31个已识别kernel容器；先验证完整patch计划，
  在load后、requirements/init/populate前修改映射内存，磁盘DLL保持原样。
- 每次实机检查实际capability maximum、Create、Evaluate及输出，而非只信unlock日志。

OptiScaler旧实测中的原生运行时计数和用户体验作为迁移依据；不将它们替代
Bloodborne的逐帧图像、Present顺序及GPU退休验证。没有移植Streamline WSI或Reflex层。

## 实机迭代

`ba9a16b` 实现有界多输出与Presenter；`5a5a3d2` 修正帧组级禁用标志。

- r64：3X解锁、capability最大5、两个Evaluate成功，但第二个独立disable buffer
  保持sentinel，因此保护拒绝整组Present。真实GPU/FD正常退休，未伪报MFG成功。
- SDK将 `OutputDisableInterpolation` 定义为本真实帧的 interpolated frame(s) 标志。
  实测表明该运行时在首个索引写它。修正为每个真实batch一个稳定buffer，仅初始化一次，
  所有索引共享，真实fence后读取；不是把未写的sentinel改成成功。
- r65：3X真正Present，NR OFF/ON、慢转镜、快速转镜/移动、FG关闭恢复及退休通过。
  两组16张读回均无重复图像，各有4个完整真实帧区间。示例patch位移4.51/8.94/13.41px，
  对应约1/3、2/3、当前真实帧；HUD截图检查正常。
- r66：4X真正Present，NR OFF/ON、慢转镜、快速转镜/移动及退休通过。
  慢转镜读回3个完整区间均唯一；示例位移3.05/6.78/10.02/13.31px，符合1/4递增顺序。

图像证据通过 `tools/fg_dev/analyze_mfg_capture.py` 无损PNG归档，逐字节核对原raw SHA，
并保留Presenter打印的真实source/index/count/epoch映射。世界区域patch位移是诊断估计，
不是完整光流质量评分；快速转镜边缘和遮挡仍需玩家长时间观察。

- r67：5X的四张生成图像不同，两个完整慢转镜区间和两个快速转镜区间均验证；
  示例世界区域位移2.98/5.11/8.09/10.39/14.00px，按索引递增；NR联动与正常退休通过。
- r68：6X的五张生成图像不同，两个完整慢转镜区间和两个快速转镜区间均验证；
  示例位移2.66/4.74/6.72/9.02/11.08/13.31px，按索引递增；F8关闭后cache_selected为0，
  恢复后重新选中NR；完整GPU/FD/模型退休通过。

## 有界采样数据

1080p DLSS Quality、Old、`BB_FPS_LIMIT=60`、Immediate、Q1；每项约10秒，均在读回前采样。
NR OFF为尚未启动NR Helper，NR ON在一次慢转镜后采样。采用同一私有场景，
但相机、热状态及背景CPU工作没有严格锁定，尤其不能从3X NR ON较快推断NR会提速。
这些是功能验收附带的数据，不是M2基准复跑或受控的NR成本比较。

| 倍率 | NR | 真实FPS | 生成FPS | Present/s | 真实间隔P50/P95/P99 ms | 设备总显存采样峰值MiB |
|---|---|---:|---:|---:|---|---:|
| 3X | OFF | 40.0 | 79.9 | 119.9 | 24.36/33.73/39.01 | 3407 |
| 3X | ON | 41.8 | 83.5 | 125.3 | 24.41/31.31/34.40 | 4548 |
| 4X | OFF | 40.5 | 121.7 | 162.2 | 24.25/30.12/33.59 | 3484 |
| 4X | ON | 38.8 | 116.3 | 155.1 | 25.06/32.82/37.14 | 4625 |
| 5X | OFF | 37.9 | 151.5 | 189.4 | 25.59/32.45/34.61 | 3781 |
| 5X | ON | 37.7 | 150.6 | 188.3 | 26.43/31.41/32.81 | 4852 |
| 6X | OFF | 35.9 | 179.6 | 215.5 | 27.80/31.72/33.21 | 3970 |
| 6X | ON | 32.4 | 162.1 | 194.5 | 30.54/35.45/36.47 | 4911 |

全部有界性能采样的输出配对错误为0。完整批次数和Present P95/P99另见本机
`out/m4-results.json`，原始有界记录见 `out/m4-*-checks.json`。
显存是整块设备总使用量，含Native、Helper和桌面，不是MFG的独占分配或整轮瞬时峰值。

当前eDP 2560×1440和HDMI 1920×1080均约165Hz。高倍率的Present/s不能等同于实际扫描输出；
5X/6X超过165的部分不能按计数宣称全部显示。MFG没有提高真实游戏FPS，6X NR ON在本场景
约32.4真实FPS。虽然没有新增完整游戏帧排队，整批生成完成和分时Present仍会延后真实图像
显示；未进行按键到光子的硬件延迟测量，操作手感需玩家比较，不能用更高Present计数替代。

## 设置与回退

现有统一启动器中选择“DLSS帧生成／多帧生成”，在高级FG设置选择2X–6X。
3X–6X需要GPU DMA-BUF和Experimental Q1；组合不兼容时明确拒绝启动，
仍能重新打开设置纠正，不会静默改倍率、切换算法或关闭NR。
`56f89e3` 包含该可恢复设置处理；实际GTK已逐项验证2X–6X保存、CLI转发和NR独立性。

本机开发启动命令（FG工作树）：

```bash
bash out/launch-m4.sh
```

使用现有启动器、现有FG/out构建和独立M4设置，复用M3测试存档副本。
`out/` 入口是本机路径绑定，不是新的发行包；正式安装和正式玩家存档未替换。
2X可以选择submit-ready-v1或Q1，也保留CPU mapped/files回退；FSR后端仍独立保留。
M3 Helper小型二进制回退保存在 `out/m3-helper-stable`（约1.9MiB），原M3启动入口继续使用
nrbridge/out与该Helper。未复制Wine prefix、SDK、依赖树或AppImage。

M4当前只支持1280×720 guides→1920×1080输出。M5尺寸接入及其launcher补丁未合入，
本轮也不扩大NR执行尺寸或改变NR时域历史算法。分支可供nrbridge后续合并，未自动合主线。

## 收尾回归与交付状态

- r69因M5持有独占锁而在启动前被拒绝，没有M4 GPU会话；该次不计为验收成功。
- r70 x2 Q1：NR OFF 5秒271真实/271生成；NR ON 5秒241真实/242生成，配对错误0。
- r71 x2 submit-ready：NR OFF 3秒127/127；NR ON 3秒119真实/120生成，配对错误0。
  两个x2 Helper日志均没有MFG unlock，确认x2保持未打补丁的模型路径。
- r72 FSR 3.1 HUDless：NR OFF 3秒180/180；NR ON 3秒166真实/167生成，配对错误0。
  样本边界允许真实/生成相差1；这些短样本用于回退功能验收，不用于宣称新的性能优化。
- r64、r65、r66、r67、r68、r70、r71、r72全部返回0、`PASS_NORMAL_TEARDOWN`，
  实际FG/Present退休证据齐全；NR开启会话有NR GPU/FD/模型正常退休证明。
  无残留进程、无watchdog；NR prefix借用均归还，最终r72 receipt为retired。
- CPU：54项FG/协议/批次顺序检查、40项启动器公共接口检查通过；Native增量`--test`
  和PE Helper构建通过。实际GTK验证2X–6X保存、精确CLI倍率、不可用组合可重新编辑、NR不变。
- 没有重新开启全量Vulkan validation；本轮没有观察到DeviceLost或新GPU错误，
  但不宣称M3已知通用VUID已消除。CPU Bridge接口保留并通过CPU检查，本轮未重做其低性能实机测试。
- NVIDIA磁盘DLL最终SHA与初始allowlist完全相同。唯一M4代码执行者未修改OptiScaler仓库。

本机 `out/launch-m4.sh` 预选4X Q1；新配置的代码默认仍为2X。
切换倍率/流水线后重新启动。快速回退可选DLSS 2X、submit-ready或FSR；不自动关闭NR。

本轮已知新增测试/图像/小型回退/私有存档备份占用约 **0.97 GiB**（1015716 KiB，
`out/m4-disk.json`；扣除M3私有启动器175932 KiB基线）。含约351 MiB无损图像归档。
已在逐字节验证后删除约1.0 GiB重复raw，保留source/index/epoch映射、原始SHA和完整PNG。
小型日志/脚本及共享增量编译缓存净变化另计，未将此数字冒充整个工作区精确净增量。
没有新增Wine prefix、完整构建目录、SDK/依赖副本或AppImage。

M4可供`nrbridge@464f4b6`后续合并，本轮未自动合流或推送。本轮M5协作交接点为
`m5/nr-dimensions@9983a9d`，其新尺寸仍FG Off；跨线统一合流需处理双方独立launcher补丁，
不得把M5的NR尺寸成功当作本轮MFG已支持可变尺寸。

玩家仍需验收：长时间战斗、快速转镜/遮挡边缘与HUD动态变化，以及2X/3X/4X/5X/6X
实际操作手感。没有硬件测量输入到屏幕延迟，也没有独立测量物理扫描帧数。
