# M9：Old Memory 多尺寸 DLSS FG / MFG 实机验收

2026-10-24，RTX 4060 Laptop / CachyOS / 原生 Vulkan。
本阶段使用公共 Native `nrbridge@2f566db`，FG Helper 为已核对源闭包及产物的
`42bc6f7` 构建；FG 工作树 `m9/dlss-fg-dynamic@a7d7c35`。
动态资源及独立真实源帧修复已经合入公共主线，本阶段没有替换 FG/NR 算法。

## 实际通过的范围

| Old Memory 配置 | 实际 guides → FG 输出 | 倍率 | 验收 |
|---|---|---|---|
| 1080p Quality | 1280×720 → 1920×1080 | 2X、3X | 独立 SR+FG，旧路径通过；2X 关闭/恢复通过 |
| 1440p Quality | 1706×960 → 2560×1440 | 2X | SR+FG、NR→SR+FG 均通过 |
| 1440p Performance | 1280×720 → 2560×1440 | 3X | SR+FG、NR→SR+FG 均通过 |
| 1440p Balanced | 1506×848 → 2560×1440 | 4X、5X、6X | SR+FG 通过；6X 另有 NR→SR+FG 通过 |
| 1920×1200 Quality，16:10 | 1280×800 → 1920×1200 | 2X | 运行中从 1440Q 切入、FG 关闭/恢复及切回通过 |
| 1440p DLAA | 2560×1440 → 2560×1440 | 2X | 2f566db 的 FG 重建/生成通过、NR 恢复失败；后续公共修复及回归见下文 |

这是代表性组合验收，不是所有档位、尺寸、倍率的笛卡尔积验收。未实测的超宽屏、
4K、HDR 及 New Memory 不由此表推定通过。NR 开启样本使用独立 1280×720 NR execution；
不是将 NR 模型执行尺寸改成 SR render 或 FG output。

所有通过项均核对实际 Helper layout、真实生成 Present、正确 source/index/count/epoch，
以及正常 GPU/FD/Helper/Present 退休。没有用 Evaluate 次数或内部 FPS 计数替代输出证明。

**显示范围边界：** 本轮实际 `BB_DISPLAY` 选择 HDMI-A-1 主显示器 1920×1080。
1440p 项的 FG mailbox/output 确为 2560×1440，但最终 encoded Present 读回为
1920×1080。它们证明内部 1440p 渲染/生成及后续缩放 Present 可用，尚不是 eDP
2560×1440 最终显示尺寸验收；后续应另选 eDP 核对 encoded extent。16:10 同样不能
从内部输出尺寸推断显示器物理模式已改变。成功 VkPresent 返回也不等于扫描显示帧数。

## 有界性能样本

统一 Old、DLSS SR、`consumer-queue-v1`、GPU DMA-BUF、`BB_FPS_LIMIT=60`。
实际日志为 `FG_SWAPCHAIN_MODE selected=Immediate requested=Immediate FPS_LIMIT=60`。
下表为无 validation layer 的热运行，每项 8–10 秒；性能在读回之前采样。
NR OFF 项为尚未启动 NR Helper 的 SR+FG，而非仅将 NR 效果隐藏。

| 配置 | NR | 真实 FPS | 生成 FPS | Present/s | 真实间隔 P50/P95/P99 ms | 设备总显存采样峰值 MiB |
|---|---|---:|---:|---:|---|---:|
| 1080Q 3X | OFF | 47.0 | 93.8 | 140.8 | 21.64 / 26.16 / 27.06 | 3757 |
| 1440Q 2X | OFF | 43.3 | 43.3 | 86.5 | 23.29 / 26.23 / 29.93 | 4321 |
| 1440Q 2X | ON | 39.6 | 39.6 | 79.3 | 24.65 / 29.37 / 30.76 | 5123 |
| 1440P 3X | OFF | 38.7 | 77.5 | 116.2 | 26.28 / 32.52 / 34.44 | 4107 |
| 1440P 3X | ON | 35.4 | 70.8 | 106.2 | 28.60 / 36.32 / 37.24 | 5072 |
| 1440B 4X | OFF | 33.0 | 99.1 | 132.1 | 30.13 / 36.37 / 37.13 | 4308 |
| 1440B 5X | OFF | 29.1 | 117.0 | 146.1 | 33.95 / 38.59 / 39.03 | 4536 |
| 1440B 6X | OFF | 26.5 | 132.6 | 159.1 | 37.63 / 39.93 / 40.88 | 4524 |
| 1440B 6X | ON | 24.4 | 121.9 | 146.2 | 40.88 / 46.26 / 47.38 | 5419 |

NR ON 三个窗口实际 cache_selected 分别为 **317/317、354/354、195/195**。
表内完整生成批次顺序错误均为 0，窗口内失败 Present 为 0；窗口截断允许生成/真实
计数相差少量帧。显存为整块 8188 MiB 设备的离散总使用量，含桌面及各进程，
不是 MFG 独占分配或全过程瞬时峰值。

1080Q 2X 与 1440Q→16:10→1440Q 的功能测试开启 core+sync validation，时间开销明显，
不混入热运行性能表。其读回外样本分别约 24 与 23 真实 FPS，不能与旧热运行基准
直接比较。测试设有 60 限帧，并未重新进行无限帧吞吐量基准。相机及热状态未严格
锁定，因此这张表也不是不同档位间 SR 成本的受控比较。

### GPU 与延迟相关区间

以下为 NR ON、按实际源帧匹配的采样中位数，单位 ms：

| 区间 | 1440Q 2X | 1440P 3X | 1440B 6X |
|---|---:|---:|---:|
| Native guides GPU copy | 0.379 | 0.211 | 0.292 |
| Native final color GPU copy | 0.078 | 0.070 | 0.071 |
| Native producer completion CPU wait | 10.00 | 6.21 | 2.35 |
| Wine Evaluate Vulkan queue interval | 10.99 | 15.75 | 32.93 |
| Native 等 Helper CPU wait | 14.07 | 19.47 | 36.52 |
| 独立 consumer copy GPU interval | 0.096 | 0.189 | 0.476 |
| capture→真实 Present 返回 CPU age | 56.50 | 66.95 | 112.30 |

每列分别约 10、11–12、6 个区间样本。上述区间相互重叠，**不得相加**；Wine queue
区间没有独立分离 CUDA/OFA。capture age 是既有软件时间戳区间，不是按键到光子的
硬件延迟。高倍率提高 Present 数量，同时降低真实 FPS 并增加真实图像到达时间，
不能将 6X 的 Present/s 用作响应更快的证据。本轮未做硬件输入延迟测量。

## 图像、顺序与资源重建

每次捕获限定 16 张实际 encoded Present 图像，保留真实 source、index、count、epoch
映射，逐字节验证 PNG 解码重建等于原 raw SHA 后归档。1080Q 3X、1440P 3X、
1440B 4X/5X/6X 均有完整帧组、不同图像及按索引递增的世界 patch 位移。

- 3X：示例约 1.02 / 2.23 / 3.29 px。
- 4X：示例约 0.90 / 1.77 / 2.46 / 3.34 px。
- 5X：示例约 0.79 / 1.47 / 2.01 / 2.60 / 3.39 px。
- 6X 初始慢转镜仅约 3 px 总位移，亚像素估计不适合确认精确六等分；另开短会话
  增大转镜位移复核，完整批次五张中间帧均唯一，示例约
  **2.77 / 4.35 / 6.77 / 9.01 / 11.00 / 13.25 px**，按索引递增。

patch 是局部平移诊断估计，不是全图光流质量评分。截图/连续帧中 HUD 未见明显错位，
有界转镜未见此前 FSR 植被空间漂移；复杂遮挡、快速战斗和主观手感仍需玩家验收。

1440Q→1920×1200Q→1440Q 的三个 FG generation 均已实际退休，最后 active=0；
1440Q→DLAA→1440Q 同样有三个实际生成 generation，最终均正常退休。
FG 关闭窗口 generated=0，恢复后实际重新生成；生成分支不推进 NR 真实源帧。

## 已知问题与边界

1. **2f566db 的历史 NR 切档恢复缺陷，后续已修复。** `m9-old1440q-2x-nr-r10` 在初始 Quality
   正常选中 NR；切到 DLAA 后 NR 以 `profile_change` 正常排空退出
   （80 finalized = 79 captured + 1 obsolete），后续 DLAA 样本 171/171 为 cache_selected=0。
   切回 Quality 时 FG g3 正常生成，NR 仍未恢复。因此该 DLAA 样本只能算 SR+FG，
   不能按样本旧标签 `nr-sr-fg` 宣称 NR 联动通过。M8 接手公共 NR 停止/恢复逻辑；
   本轮没有绕过 unsupported、清除历史或强行恢复旧 Helper。后续修复为 `2f0c3d1`，实测见下节；
   原 r10 记录保持不变，不能将后续成功写回旧失败样本。
2. **公共 WSI sync hazard 尚在。** 两次 core+sync 冷测试各报告 10 次
   `SYNC-HAZARD-WRITE-AFTER-PRESENT`，位于启动菜单/早期失败 Present、FG Helper 建立前。
   VUID 字典为空不代表 sync clean；duplicate-message limit=10 也不允许从后续没有新增
   日志推断问题消失。全屏和非全屏均见，未证明全屏是原因。已交 M8 公共 WSI 线定位。
   热运行没有开启 layer，不能写成 validation PASS。
3. `r7` 的自动菜单进入超时发生在 FG Helper 创建前，正常 NO_PEER 退休；修正有界
   按键探测后 `r7b` 通过。它是捕获/会话错误，不是 MFG 失败或驱动故障，没有触发重启。
4. New Memory 本阶段未测；eDP 最终 1440p、超宽屏、HDR 及更高分辨率尚未验收。
   CPU Bridge 和 FSR 回退代码保留，本轮未对未改算法重跑整套性能测试。

## 配置、交接与本机证据

继续使用统一启动器：DLSS FG 后端、高级倍率 2X–6X、GPU DMA-BUF。
3X–6X 使用 `consumer-queue-v1`；2X 保留 `submit-ready-v1`、consumer queue 及 CPU Bridge
回退。非默认 NR 尺寸需启用已有尺寸选项，NR execution 本轮选 1280×720，SR 档位及输出
独立设置；NR 开关独立，不因选择 FG 后端而自动关闭。倍率改动仍需重新启动。
Old 非默认组合已经可实验使用；NR 切档恢复须包含后续公共修复 `2f0c3d1`，不能使用原2f566db推定通过。

## M8 后续公共修复回归

公共 Native `bd654b2`（包含修复 `2f0c3d1`）、同一 Helper42，core+sync 冷功能验收：

| 会话 | Q→DLAA窗口 NR | 回Q窗口 NR | 回Q稳定5秒 NR / real / generated | 正常退休 |
|---|---:|---:|---|---|
| m8-old1440q-resume-r3 | 224/231 | 223/230 | 165/165 / 165 / 164，order0 | rc0、lastuse=true、residuals=[]、prefix RETURNED |
| m8-new1440q-resume-r1 | 166/173 | 176/183 | 119/119 / 119 / 119，order0 | 同上 |

两场实际 memory model 分别核对 old/new；NR execution1280×720，SR/FG output2560×1440，
最终窗口读回仍为1920×1080，不增加物理1440p扫描显示结论。
退出前没有 profile_change stop，请求只在最后正常退出时出现 reason=exit。
Helper、Native最后使用、逐代 GPU/FD/model 均实际退休。daily preSR 的短暂 domain 不匹配改为保留会话和资源，
已提交任务仍走原真实完成→obsolete→CONSUMED→FinalizedAck；有限诊断和未知状态隔离没有放松。
恢复沿用 source gap/layout_changed/history reset，不复用旧布局 candidate。

Old 本场完整 stdout 仍累计原型相同的10条 WSI WRITE_AFTER_PRESENT，未解决；New 本场累计 VUID0/sync0。
这些冷窗口只验证功能、切换与退休，不作为 Old/New 热性能对照。
New 在启动 NR 前另有5秒 real117/generated117/order0，证明 scaled FG 独立 source stamp 已实际生效，
不是依靠 NR ledger 或 Helper Evaluate 计数推断通过。

本机结果根目录为公共 `nrbridge/out/nr-dev/runs/`，完整成功会话：

```text
m9-old1080q-2x-r3          m9-old1080q-3x-r4
m9-old1440q-2x-r5          m9-old1440p-3x-nr-r6
m9-old1440b-4x-r7b         m9-old1440b-5x-r8
m9-old1440b-6x-nr-r9       m9-old1440q-2x-nr-r10
m9-old1440b-6x-r9b
```

全部最终为 rc=0、`PASS_NORMAL_TEARDOWN`、native_lastuse_proof=true、residuals=[]；
NR prefix 借用均正常 RETURNED，最后 r9b receipt=retired，GPU 独占窗口已交 M8。
失败的菜单探测 r7 同样正常退休，没有保留未证明 GPU 所有权或清除 HOLD。
GPU/FD/模型正常退休和 validation 图像同步正确性是两项独立结论。

FG 工作树 `out/m9-old-acceptance-results.json` 汇总 Present/退休记录，
`out/m9-*-checks.json` 保留采样及重叠区间，`out/m9-evidence/` 保留 PNG/映射/分析。
本轮 Old 会话常规文件及无损归档合计约 **880 MiB**；不是全工作区精确净增量，
未包含共享增量构建缓存、原有 prefix、私有存档备份或工具运行时。
检查 `artifact-gc.py` dry-run 后未执行广泛 GC；按原 SHA 逐字节核对 PNG 后，仅删除
自己已退休会话的重复 raw，共约 **759 MiB**，保留所有图像、映射及退休证明。
未新建 Wine prefix、SDK/依赖副本、AppImage 或完整构建环境；未修改正式安装或正式存档。

Old 动态 FG/源帧代码已在 `nrbridge@2f566db`；本文件为验收记录，不要求再次合入相同代码。
下一步由 M8 安排 New Memory MFG 和 eDP 显示窗口，FG/Helper/Presenter 问题由 FG 线处理，
公共 Frame Context/Scheduler/NR 恢复由 M8 处理。
