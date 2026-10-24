# M8 New Memory：阶段检查点

2026-10-24。按用户“完成当前阶段后暂停”的要求，收尾本阶段后停止新测试与合流。
这不是整个 M8 验收：New Memory 的 DLSS FG/MFG、跨区域长时稳定性及 M9/M10 合流仍待继续。

## 基线与实际内存路径

工作树是 `.worktree/nrbridge`，起点 `7a11771`。根目录 `master` 保留为构建基础设施线，
没有覆盖它。已检查所有分支、工作树及 480 个可达提交；M4/M5 完整合流历史保留。
已有 FSR 子模块构建补丁脏状态保留，没有将它纳入本轮提交。

RTX 4060 Laptop，8188 MiB，NVIDIA 615.78.08。实机 New 使用
`VK_EXT_external_memory_host`（4096 字节对齐）、guest in-place、layer chunk buffers
和 VRAM mirrors；没有对游戏直接内存使用 sparse binding，也没有静默回退 Old。
Old 对照实际为 in-place=0、layer=0、write-tracking=1。

新增 `MEMORY_MODEL_ACTIVE_V1` 在驱动探测和回退决策之后报告实际路径，受管日志将原始行
保存在 `memory-model-active.json`，不因双文件 stdout 轮转丢失。菜单选择不能替代这个证明。
显式选择 New 才启用；受管 NR 的既有 Auto/Old 行为保留，可随时正常退出后选择 Old 回退。

## 源码落地

- 显式 New 可通过 NR/FG 的受管启动检查；未知 memory model 拒绝，不静默当 Old。
- M5 NR 借用 TemporalUpscaler 的 render-size Motion 图像/视图，SR/FG 继续使用同一所有者。
  HDR 和 LDR 阶段仍各自计算 Motion，保留队列读写屏障；NR 跨帧推理仍使用不可变快照。
  删除第二份 render-size RG16F 分配，以及每帧 ImageView 创建和退休回调。
  1080p Quality 减少 3,686,400 字节重复图像，1440p NR-only 减少 14,745,600 字节；
  这是像素存储量，不等于驱动实际分配或整卡显存下降。布局变化日志为 `NR_GUIDES_SHARED_V1`。
- Motion 重建等待真实 Native 队列完成；NR-only 即使没有 SR context，退出也覆盖该资源。
- 实机 sync validation 发现 GPU marker 写 guest chunk 后，镜像 CopyBuffer 缺少依赖。
  原因是 `WriteLabelOnGpu` 只有 portable 分支登记 `Runtime::AccessBuffer`，marker 分支遗漏。
  现按 TRANSFER_WRITE、TRANSFER 与请求的 BOTTOM_OF_PIPE 阶段登记，由实际消费者触发现有屏障，
  没有用 CPU 提前写标签或伪造完成。[Vulkan marker 同步契约](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteBufferMarkerAMD.html)
- 实机发现 sRGB 代理视图继承父图像 STORAGE usage。`SceneTargets::Attachment` 还把
  `is_storage=true` 混作“包含 stencil”标记。现仅 depth 使用该标记，采样/颜色附件视图限制用途；
  HDR storage 消费保留 STORAGE，M5 HDR proxy 明确申请 storage view。
  初次将 CachedView 一律设为 SAMPLED 的修复过窄，被后续实机校验抓到并修正。
  [Vulkan 视图 usage 契约](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewUsageCreateInfo.html)
- 修正 `--fg-validation` 与保存的 validation=off 快照冲突。新请求记录实际诊断选择；
  旧诊断只允许加强 core+sync 校验，不允许移除已请求的校验。同步层设置更新为 `syncval_full_validation`。

## 本阶段实机矩阵

所有数据来自独立测试设置/存档；Native 构建缓存、模型、Wine/GE、已退休 prefix 原地复用。
没有新 AppImage、Wine 环境副本或正式玩家安装切换。

| 模式 | 本阶段证据与边界 |
|---|---|
| New 原生 / FG Off | 实际世界画面、可操作，正常退出和 actual GPU last-use 通过 |
| New 1080p Quality，NR Off → NR→SR | 实际 SR 调用、NR 输出被真实 Present 选中，正常退休通过 |
| New M5 NR→SR | 1080p Quality；同进程切 1440p Quality/Balanced/Performance/Ultra Performance/DLAA，布局 reset 后恢复 NR |
| New 1440p NR-only | render/output=2560×1440、execution=1280×720；日志证明没有 SR Evaluate 调用；世界画面核对 |
| New NR 开关 | 实际 SDL F8 接收；关闭窗口内 48 次 Present 全部 cache_selected=0；恢复窗口 65/65 选中 NR |
| New NR→SR + HUDless FSR x2 | 已观察真实 generated→real 与 NR 选中；最终 storage-view 修正后的复验结果见本文件下方收尾记录 |
| New DLSS FG x2 / MFG 3X–6X | 本阶段尚未实测，不沿用 Old 的验收充当 New 通过 |
| New 动态尺寸 + FG | M9 独立开发，尚未合流验收 |
| 长时跨区域加载、异常退休注入 | 待继续；多次启动/分辨率切换不能代替这两项 |

SR 各档位是在 NR 开启的 M5 路径测试；不能据此宣称每档 SR-only 已独立重跑。
M5 分段探针受双文件日志轮转影响，部分计数仅覆盖保留子窗口；仅用其证明实际布局/NR恢复，
不作为完整窗口的性能横评。

## Old / New 同场景探索性对照

相同 Native 构建、1080p Quality、NR 310.9.16 Preset B、同一位置/镜头、uncapped、FG Off。
20 秒实际 real Present 返回窗口；非 GPU 执行时间、扫描显示或输入延迟。
此对照在后续 marker/view 修复前采集；未控制温度/时钟及其他开发线 CPU 构建，不作为最终性能验收。

| NR→SR | Old | New |
|---|---:|---:|
| real Present/s | 41.55 | 42.83 |
| P50 / ms | 22.41 | 21.71 |
| P95 / ms | 35.38 | 37.66 |
| P99 / ms | 38.29 | 48.62 |
| 设备总显存采样峰值 / MiB | 3994 | 5367 |

New 在该窗口中位数略低，但尾部更差、显存更高；没有全面性能胜利结论。
设备用量包含驱动/桌面/Helper，不将不同 PID 的显存报告相加。原始短窗口与 UUID 采样
保存在 `out/nr-dev/runs/m8-{old,new}-nrsr-r{3,2}/`。
New SR-only Quality 的早期窗口未保存显存序列，标记 NOT_MEASURED，不当作 0。

`tools/nr_dev/memory_profile.py` 可测独立 real Present，不依赖 NR 缓存已启用。
显式 `BB_MEMORY_PROFILE=1` 后，在已观察的同一场景执行：

```bash
python3 tools/nr_dev/memory_profile.py SESSION LABEL --model new --seconds 20
```

## 验证与保留问题

Native 增量构建与基础运行时检查通过。聚焦 launcher/player settings、日志轮转、Present
解析、维度、受管启动及退休 CPU 检查通过；协议导出身份检查未放宽。
生产 `scene-resolution-test` 已扩展 sRGB sampled/attachment 回归，在 RTX 4060 上
开启 core+sync validation：真实颜色/深度/模板读回、720p–4K 尺寸切换和 mip proxy 均通过，
零校验错误。首次失败日志与修正后日志分别保留，没有覆盖原始失败。

游戏全链路尚非 validation-clean。首次 FSR 检查还报告 guest object-motion varying
Location 31 超出带 built-in 的组件限制、空颜色附件 format 不匹配，以及首次采样的 image layout 问题。
这些没有归咎为 New 独有，也没有用过滤校验消息宣称通过。已修复的 marker RAW 与 sRGB
usage 错误在后续真实世界日志中没有再次出现；HDR storage 视图修正继续单独复验。
正常 GPU 退休 PASS 与功能/validation 是否通过分开记录。

本机小型证据在 `out/m8/` 和 `out/nr-dev/runs/m8-*/`。`r5` 保留首次 validation FAIL，
`r6` 保留过窄 HDR view 的 FAIL；两场均正常退休，未留下未知 GPU 所有权。

## 独立线与恢复顺序

FG M9 的动态 layout、逐代退休、pending-source scratch 仍在独立分支
`m9/dlss-fg-dynamic@26044ae`；精确 scaled-NR gate 的第二处 HDR prepass 准入仍待补齐。
NR M10 的默认关闭画质增强和合成测试仍在独立分支 `m10/nr-quality@1b50c6a`。
通过 Codex 原生通信协调，未使用 Herdr。
用户暂停期间不启动两条线的完整 GPU 测试，不把 CPU/合成结果当主线实机验收。

恢复后先补 New FSR 剩余校验问题和最终同场景性能对照，再进行 New DLSS x2、MFG 3X–6X，
随后审核/分批合入 M9/M10，安排区域切换、长时负载及异常退休验证。

## 收尾记录：已暂停

本轮源码已提交到 `nrbridge`，从统一基线 `7a11771` 顺序新增：

- `5871c4e`：显式 New 准入、真实路径/Present 遥测与共享 Motion 资源。
- `a08c65d`：GPU marker 写入的 guest buffer 同步登记。
- `7311cb5`：sRGB 视图用途、场景回归和诊断设置快照修正。
- `6349339`：保留 HDR storage 用途及 M5 HDR proxy storage view，修正上一提交过窄的限制。

最终源码修正的 Native 增量构建成功，见 `out/m8/build-final-view-usage.log`。
该构建在 `7311cb5` 加本次修正的工作树上完成；r7 保存了当时的构建来源与 diff，
不能将提交动作本身当作又一次运行验证。

`m8-new-fsr-r7` 使用最终修正构建，但停留在 Continue 菜单，FG generated=0，
没有启动 NR Helper，也没有取得世界场景联动证据。菜单日志零 VUID 不能证明 HDR merge 路径
已通过；**最终 HDR 修正后的 NR→SR + FSR x2 世界场景复验仍待继续**。
r6 的短窗口已观察 41 次 real、42 次 generated、41/41 NR 选中且 Present 顺序无错误，
但保留其 HDR storage view 校验失败，不能将它列为最终通过。

用户手动停止工作后复核，r1–r7 共七场全部为 `PASS_NORMAL_TEARDOWN`，
`native_lastuse_proof=true`、`residuals=[]`，可复用 NR prefix 均为 `RETURNED`。
r7 为无 NR peer 的正常退出，不能将其 no-peer 证明外推为 NR Helper 退休；
带 NR Helper 的 r2/r3/r4/r6 分别保留实际 GPU/FD/model 正常回收审计。
暂停前实时进程检查未发现游戏/Helper/runner 残留，NVIDIA compute-apps 为空，
共享 GPU 锁可获得。没有清除失败 latch、强行解除未知所有权或伪造完成。

正式安装 current/previous 目标与测试前一致；测试存档仅回写各自私有 user 目录。
私有预览入口为 `out/play-new-memory.sh`，保留 1080p Quality、New、FG Off 和独立存档设置，
入口语法已检查，尚未启动验收，不作为正式部署交付。Old 回退使用同一受管入口选择 Old，
本阶段 Old 实机对照与正常退出已完成。检查点机器记录为 `out/m8/checkpoint.json`。
