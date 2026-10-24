# M9：New Memory 多倍率与动态档位实机验收

2026-10-24。续 [Old Memory 代表性验收](DLSS_MFG_M9.zh-CN.md)，
本阶段使用统一公共 **`nrbridge@295dd7c` 封存 Native**、已有 **`42bc6f7` FG Helper**，
没有重建 prefix、复制依赖或修改正式安装/存档。公共 NR 切档暂停/恢复修复为
`2f0c3d1`；Old 报告中 `2f566db` 的 NR 切档不恢复是修复前历史记录。

## 通过范围

| New Memory 配置 | 实际 guides → FG 输出 | 倍率 | 实测能力 |
|---|---|---|---|
| 1080p Quality | 1280×720 → 1920×1080 | 3X | SR+FG、NR→SR+FG、完整多图与顺序、正常退出 |
| 1440p Performance | 1280×720 → 2560×1440 | 4X | SR+FG、NR→SR+FG、多图、运行时切档及退休 |
| 1440p Balanced | 1506×848 → 2560×1440 | 4X | 从 Performance 实际重建、NR 恢复及持续生成 |
| 1440p Quality | 1706×960 → 2560×1440 | 4X | 从 Balanced 实际重建、NR 恢复；再切回 Performance |
| 1440p Balanced | 1506×848 → 2560×1440 | 5X | 独立 SR+FG、多图与顺序、正常退出；本会话未启动 NR |
| 1440p Balanced | 1506×848 → 2560×1440 | 6X | SR+FG、NR→SR+FG、多图、快速转镜、FG 关闭/恢复及退休 |

每场实际 Native 日志均为 `MEMORY_MODEL_ACTIVE_V1 model=new in_place=1 layer=1
host_import=1 write_tracking=0 constants_in_place=0`，不是仅检查请求配置。
NR ON 使用 1280×720 独立 execution，SR render 与 FG output 按上表动态变化。
2X 的公共 New1440Q 及 NR 切档短测由 M8 独立完成，不作为本四场重复验收。

**显示边界保持：** 本阶段使用 HDMI-A-1 1920×1080，实际 FG 内部输出为上表尺寸，
最终 encoded Present 读回为 1920×1080。不能将这些结果写成 eDP 原生 1440p scanout
已通过。未验证 New 的 16:10、超宽屏、4K、HDR 或所有倍率/档位组合。

## 热运行采样

同一 Native/Helper，New、DLSS SR、GPU DMA-BUF、`consumer-queue-v1`，
`BB_FPS_LIMIT=60`、实际 `Immediate`。以下各窗口约 8 秒，在图像读回之前采样；
NR OFF 为实际尚未启动 NR Helper，NR ON 为真实 Present 选中 NR cache。

| 配置 | NR | 真实 FPS | 生成 FPS | Present/s | 真实间隔 P50/P95/P99 ms | 设备总显存采样峰值 MiB |
|---|---|---:|---:|---:|---|---:|
| 1080Q 3X | OFF | 47.1 | 94.0 | 141.1 | 21.44 / 23.13 / 24.14 | 4954 |
| 1080Q 3X | ON | 42.3 | 84.2 | 126.5 | 23.37 / 28.14 / 29.55 | 5917 |
| 1440P 4X | OFF | 33.7 | 101.0 | 134.7 | 29.54 / 32.85 / 33.99 | 5330 |
| 1440P 4X | ON | 30.9 | 92.9 | 123.7 | 31.90 / 37.31 / 39.04 | 5934 |
| 1440B 5X | OFF | 29.2 | 117.0 | 146.2 | 34.17 / 35.91 / 36.50 | 5663 |
| 1440B 6X | OFF | 26.7 | 133.5 | 160.2 | 37.13 / 40.03 / 40.41 | 5571 |
| 1440B 6X | ON | 24.8 | 123.7 | 148.5 | 40.24 / 44.55 / 45.36 | 6008 |

表值从实际窗口长度计算，末位四舍五入，不能从固定整数 8 秒反推所有计数。
生成与真实帧在窗口边缘可能属于相邻批次。所有采样窗口的完整生成批次顺序错误为 0，
失败 Present 为 0。Present/s 是成功 Vulkan Present 返回数，不是显示器实际显示帧率。
显存为 8188 MiB 整块设备的离散总占用，含桌面及各进程，不是 FG 独占预算或瞬时峰值。

相机、热状态及其他系统工作未严格固定；本表不替代 M8 的同构建 Old/New 热对照，
也不能与 Old 的 cold validation 时间直接比较。尤其不能从显示帧数推断输入延迟。

### 动态输入与 NR 恢复

同一 New 4X 会话依次 Performance → Balanced → Quality → Performance，输出保持 1440p。
每步均等待实际新 resource epoch 的生成 Present，再等待新鲜 NR cache_selected 并采样：

| 阶段 | NR cache_selected / 真实帧记录 | 真实 / Present FPS | 设备总显存采样峰值 MiB |
|---|---:|---:|---:|
| 初始 Performance，8 秒 | 247/247 | 30.9 / 123.7 | 5934 |
| Balanced，5 秒 | 152/152 | 30.4 / 122.0 | 6167 |
| Quality，5 秒 | 147/147 | 29.4 / 118.2 | 6045 |
| 恢复 Performance，5 秒 | 156/156 | 31.2 / 124.4 | 6098 |

四个 FG generation 均实际生成并最终正常退休，没有沿用旧尺寸输出假充切换成功。
这里证明公共修复之后 NR 能恢复；采样窗口避开切换瞬态，不宣称瞬态每帧均为 NR。

New 6X 的初始 NR ON 为 198/198，1080Q 3X 为 338/338。6X 正常 FG 关闭后，3 秒窗口
181 次真实 Present、**生成 0**，其中 NR 177/181；保持 NR Helper 运行而非随 FG 关闭。
恢复 FG 后 3 秒真实 77 / 生成 386，NR **77/77**，顺序错误 0。
FG OFF 短窗口约 60.3 真实 FPS，仍设有 60 限帧，不能作为无限帧 SR 性能基准。

### GPU 与源帧到达时间

NR ON 的源帧匹配采样中位数，单位 ms；3X、4X、6X 分别有 11、8、6 个
桥接样本，具体各字段计数见本机 JSON：

| 区间 | 1080Q 3X | 1440P 4X | 1440B 6X |
|---|---:|---:|---:|
| Native guides GPU copy | 0.212 | 0.219 | 0.293 |
| Native producer completion CPU wait | 7.79 | 5.17 | 0.82 |
| Wine Evaluate Vulkan queue interval | 9.13 | 23.10 | 34.04 |
| Native 等 Helper CPU wait | 12.92 | 26.34 | 37.60 |
| 独立 consumer copy GPU interval | 0.108 | 0.285 | 0.472 |
| capture→真实 Present 返回 CPU age | 53.42 | 72.06 | 97.27 |

这些区间重叠，不能相加。Evaluate queue interval 没有分离内部 CUDA/OFA 工作；
capture age 不是按键到光子的硬件延迟。高倍率的批处理与 pacing 仍会增加真实图像
到达时间，本轮没有为追求 Present 数量添加完整游戏帧队列，也没有取消真实完成等待。
玩家仍需比较高倍率快速操作的手感；本轮没有人工或硬件输入延迟验收。

## 多图与画面证据

每场 16 张实际 Present 读回均保留 source/index/count/epoch 映射，PNG 无损归档并逐字节
重建核对原始 SHA。完整帧组全部图像唯一，局部世界区域位移随索引递增：

| 倍率 | 完整帧组数 | 示例局部位移：中间帧…当前真实帧，px |
|---|---:|---|
| 3X | 4 | 4.58 / 8.98 / 13.31 |
| 4X | 3 | 3.20 / 6.76 / 9.87 / 13.27 |
| 5X | 2 | 0.85 / 1.33 / 2.00 / 2.56 / 3.36 |
| 6X | 1 | 2.85 / 4.50 / 6.79 / 8.91 / 10.82 / 13.31 |

5X 采用较慢转镜，亚像素平移估计不能用于精确等分误差评分；6X 采用较大位移避免
这一估计歧义。以上是局部平移诊断，不能替代遮挡、粒子、战斗动作等全图质量验收。
检查真实与生成完整截图中 HUD 位置及数值未见明显破坏；6X 另执行有界快速转镜，
随后 FG 关闭/恢复继续正确生成并正常退出。未宣称长时间游玩或战斗人工验收已完成。

## 退休、validation 与交接

结果根目录是公共 `nrbridge/out/nr-dev/runs/`，精确会话如下：

```text
m9-new1080q-3x-r11
m9-new1440p-4x-r12
m9-new1440b-5x-r13
m9-new1440b-6x-r14
```

四场均为 rc=0、`PASS_NORMAL_TEARDOWN`、native_lastuse_proof=true、residuals=[]，
FG/Native/Helper/FD/模型退休证据通过；NR prefix 借用均 RETURNED。
最后 r14 receipt 为 retired，已向 M8 明确归还 GPU 独占窗口。
没有修改锁、guard、HOLD、GPU 完成证明或 ACK 规则。

本四场为**未开启 validation layer 的热测试**，全 stdout 汇总虽 VUID=0/sync=0，
不能写成 core/sync validation PASS。Old 冷测的 WRITE-AFTER-PRESENT 累计记录仍保留，
不由本轮无报错推断公共 WSI 问题已消除。M8 的独立 New 2X cold 结果单独记录。

FG 工作树本机 `out/m9-new-acceptance-results.json` 汇总结果，`out/m9-*-checks.json`
保留逐窗口 Present、NR、显存与重叠区间，`out/m9-evidence/r11-*` 至 `r14-*` 保留
PNG、SHA、帧映射及分析。四场常规文件及图像归档合计约 **446 MiB**，不是全工作区
精确净增量；不含共享构建缓存或私有存档备份。逐字节核对后仅删除自己的 64 份
重复 raw，释放约 **506 MiB**，所有实际图像与退休证据保留。

无需再合入重复的动态 FG 实现：本轮使用公共 295dd7c 已有代码，未新增 GPU 算法修改。
统一启动器继续选择 New Memory、DLSS FG、2X–6X；3X–6X 使用 GPU DMA-BUF 与
consumer-queue-v1，NR 独立启用。成熟范围可按上表实验使用，未测组合不自动列为通过。
后续由 M8 统一处理 Old/New 热对照、最终显示尺寸、区域加载和主线发布；
FG 线继续负责发现的 Helper/多输出/Presenter 专属缺陷。
