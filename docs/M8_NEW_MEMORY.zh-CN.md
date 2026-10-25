# M8 New Memory：阶段检查点

2026-10-24。用户已明确恢复开发；当前 CPU 修复/合流已完成，后续 GPU 实测等待新 boot。
下方“此前暂停检查点”节保留此前阶段历史。
这不是整个 M8 验收：New Memory 的 MFG、跨区域长时稳定性及 M9/M10 合流仍在继续。

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
| New NR→SR + HUDless FSR x2 | r10 世界场景 NR236/236、real236/generated236、顺序错误0；FG 同进程关闭/恢复通过，正常退休 |
| New DLSS FG x2 | M9 r1 独立线 1080p 世界场景 NR482/482、real482/generated481、顺序错误0；退出审核遗漏已完整重审纠正 |
| New MFG 3X–6X | 尚未实测，不沿用 Old 的验收充当 New 通过 |
| New 动态尺寸 + FG | M9 r2 的 1440p Helper 成功生成，但 Presenter 源帧标识缺失导致安全拒绝；源码已合流构建，待新 boot 实测 |
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

首次 FSR 检查还报告 guest object-motion varying
Location 31 超出带 built-in 的组件限制、空颜色附件 format 不匹配，以及首次采样的 image layout 问题。
这些没有归咎为 New 独有，也没有用过滤校验消息宣称通过。恢复开发后，三项均已定位到源码并修正，
r10 世界场景及 NR/FG 动态关开保留日志中 VUID/sync hazard 为零，详见下方恢复记录。
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

## 收尾记录：此前暂停检查点

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

## 恢复开发记录

用户明确恢复工作后，继续测试、修复与分批合流，没有继续停在暂停状态。

`r8` 使用最终 HDR 视图修复，在实际教堂区场景观察到 NR209/209、real209/generated210、
Present 顺序错误0；marker RAW、sRGB usage、HDR storage 视图错误均未重现。
它保留另三类校验错误，不能视为 validation-clean。`r9` 修正 Motion 与空附件后，
只剩最后一类未初始化图像错误；通过 HostImage 的尺寸/格式 debug name 定位为 reactive mask。

源码修复与主线提交：

- `0985161`：Motion varying 从30/31移到28/29，按照实际设备 VS/FS 分量上限分别计算 built-in
  预算，并检查 guest 参数（含 clip emulation 位移）碰撞；超预算、缺少 FS 或 FS 自带 MRT7
  均保留普通 shader，回退 camera motion。`BB_OBJECT_MOTION_ALL` 也不能绕过此契约。
  SPIR-V binary version21 阻止从旧缓存取回超限 shader。新增真实 backend SPIR-V 和预算边界回归。
  新增 Motion slot7 时，未使用的中间 color attachment format 必须为 Undefined。
- `2ff14f4`：DLSS 不生产 reactive mask，HDR merge 却始终绑定其 typed R8 storage view。
  分配时同队列一次清零、Undefined→General、transfer-write→shader-read/write 屏障，
  保证关闭 debug 分支时描述符也合法；没有增加每帧 CPU 完成等待。
- `66ea3e2`、`5b7b525`、`c06f484`：接入 FG 独立线逐代退出审核，拒绝未列出/嵌套 owner，
  核对最后一代与根日志的最终证明。新增仅针对“动态目录审核分派遗漏”的受限纠错入口：
  原始 HOLD/result 和失败 audit 另存原字节，绑定同一 boot/session/nonce 与证据 hash；
  发布前两次完整审核，receipt 最后提交，失败恢复 HOLD。没有清除 failure latch 或将进程消失视为完成。
- `2eccdcd`：Vulkan VUID/sync hazard 全会话有界计数跨 stdout 轮转保留，独立于 GPU 退休。
  老会话缺少计数字段时不能据此报零。后续 M9 r2 开启 core+sync，完整捕获计数为零。
- 通用 `--gpu-validation` 已从 NR 独立线 `97f82db` 分批合入，可对原生、SR、NR、FG 使用
  已准备的 core+sync layer。旧 `--fg-validation` 兼容及其限制保留；诊断不计入热性能验收。
- `f21a315`、`b2b387f`：性能探针改为按 inode 增量读取 Present 记录，避免每0.5秒重扫全部日志；
  缺失连续帧 telemetry 时拒绝评分，并记录采样器自身 CPU 时间。opt-in 驻留日志区分
  active/valid mirror、仍由真实 timeline 保护的 retired mirror、图像缓存和 driver heap 用量。
  `known_completed_tick` 仅为缓存的完成下界，不能替代实际退休证明。

`r10` 使用上述 GPU 修复、实际 New Memory、1080p Quality 和 core+sync validation：

| 同一世界场景窗口 | NR 选中 | real / generated | 顺序错误 |
|---|---:|---:|---:|
| NR→SR + FSR x2，10秒 | 236/236 | 236 / 236 | 0 |
| 关闭 FG，5秒 | 132/132 | 132 / 0 | 0 |
| 恢复 FG，5秒 | 119/119 | 119 / 119 | 0 |

这些窗口是验证功能与关联的诊断负载，不用于 Old/New 性能横评。r10 的 retained stdout
VUID/sync hazard 为零；它在全会话计数补丁之前启动，保留此证据范围限制。
r8/r9/r10 全部 `PASS_NORMAL_TEARDOWN`、actual GPU last-use=true、residuals=[]、prefix RETURNED。

M9 r1 首轮 New1080Q x2：Helper 与真实 NR/SR/FG 均工作。未 arm NR 时10秒 real543/generated543，
arm 后 real482/generated481、NR482/482，顺序错误0。它最初由旧审核入口只查根目录而进入 HOLD；
原始失败仍保留在 `retirement-reaudit-original-{result,audit}.json`，独立逐代重审及 receipt
纠正后为正常退休，prefix 已归还。该轮只开 core，不能报 core+sync 全清。

M9 r2 为 New1440Q x2、render1706×960/output2560×1440，Helper 已完成1000余次真实请求，
完整 core+sync 计数零，但 generated Present 为0：scaled Presenter 使用 NR 才填写的 c1 source，
未 arm NR 时为0，严格匹配正确拒绝。已引入独立于 NR 的真实 display source stamp，
由实际 producer 阶段绑定 source/address/tick，严格一次消费，不放宽配对或伪造 NR ledger。
r2 已正常退出及释放实际 GPU owner；没有将这项缺陷捆绑为其他已运行组合的全局阻塞。

New 的显存镜像与纹理缓存同时存在。当前驻留日志可见约2 GiB镜像分配，其中约1.5 GiB有效，
部分容量为 loader 合并/增长预留；这不是单凭设备显存曲线就能判断的泄漏。
现有 LayerCompactMirrors/LayerYieldToImages 仅在压力下回收空间，退休缓冲在真实 timeline
完成前仍持有。下一步用修复后同一 Native、同一位置的 Old/New 窗口，分开比较这些持有量和峰值。

## 当前恢复边界：M10 冷捕获异常

`m10-new1080q-cold-r1` 在独立 NR 线使用实际 New、1080p Quality、FG Off、显式 core+sync，
真实 NR→SR 连续工作。画质候选只在该私有会话开启，未改正式安装；整个 stdout 的
VUID/sync hazard 计数为零。F9 实际送达 SDL/overlay 后，旧诊断构造器只接受硬编码
`NativeDefaultPaths::DevRoot()`，拒绝当前已经由受管入口验证的 `NRBRIDGE_HOME=.../out/nr-dev`。

该异常进入 generic Source quarantine。虽然目录检查发生在 capture staging 分配/GPU Record
之前，Source 上游已经可能录制其他工作，因此不能把“诊断还没分配”外推为“整个 NR 无 GPU 使用”。
Helper STOP_READY 报告自身 GPU 完成，但没有 Native_RELEASED；Helper 超时异常退出，
Native 清理超时后留在隔离状态。正常关闭超过120秒未完成，负责人保存原始身份、HOLD/abort
hash、线程等待点及 GPU/内核观察后，仅对该私有 Native PID/starttime 请求 TERM、再 KILL。

最终事实：rc137、HOLD、native_lastuse_proof=false、residuals=[]、prefix RETAINED_UNPROVED；
当前 NVIDIA compute-apps 为空。**进程结束不等于正常 GPU 退休**，没有重审为 PASS，
没有清除 latch/receipt，没有归还该 prefix，也没有在同一 boot 绕过 guard 再启游戏。
原始记录在 `out/nr-dev/runs/m10-new1080q-cold-r1/`，小型异常收尾证据在
`out/m8/m10-exception-r1/`。本 boot 的内核观察未发现 NVIDIA Xid/GPU hang；这仍不能补造缺失证明。

必要源码修复已从 NR 线独立合入：

- `abfa69f`：使用既有 `ValidDevRun(run, session)` 契约，接受合法 daily 自定义根目录，
  仍拒绝错 session、非 canonical 路径及 symlink。
- `8671140`、`6cccb93`：CPU-only `ValidateRequest` 在 staging 分配前完成，
  仅 `DiagnosticAdmissionRejected` 被捕获并标记“请求拒绝”，消费该 capture_seq，不每帧重试；
  正常 Source/lease 流程继续。未知 allocation/record/GPU 异常仍外抛隔离。
  CPU 回归实际编译/执行生产 catch 片段，证明拒绝后继续、complete=false，未知异常不被吞掉。

M9 分代资源、精确 NR gate、真实源帧 stamp 已合入主线并通过 Native 增量构建与基础运行时检查。
主线保留比独立分支更严格的逐代退出审计；完整 FG58、launcher30、play15、settings16、
daily runner12、reaudit8、daily log12、capture admission2 等 CPU 检查通过。
NR M10 画质/重放核心仍在独立线，未因候选能运行就认定画质通过。

为遵守复用 Wine 环境的要求，已准备 `tools/nr_dev/prefix_recovery.py`，但没有在当前 boot 执行。
它要求实际 boot UUID 已改变、原始 HOLD 保留、原 prefix source 有正常退休证明、没有活跃私有 owner，
再按目录 inode 移回原位置；状态明确为 `RETURNED_AFTER_REBOOT`，不更改失败 result/audit/latch/receipt。
同 boot、活 owner、source 被重建均拒绝；恢复中断可重入，7个真实临时目录 CPU 回归通过。

用户安排重启后按此顺序恢复，不新建 prefix/AppImage/证据树：

```bash
cd /home/arinp/Code/bloodborne_pc/.worktree/nrbridge
source out/m4-m5-integration/env.sh
python3 tools/nr_dev/prefix_recovery.py m10-new1080q-cold-r1
```

随后先测统一主线 New1080Q FGOff 与修正后的 F9，再做 M9 1440Q x2/source-stamp 动态回归，
补 MFG3X–6X 及修复后 Old/New 热对照、区域加载。现有 Old/New 短窗口仍是前文探索性数据，
没有把尚未执行的恢复后测试写成通过。

最后统一构建保留共享依赖/ccache，未生成新 AppImage 或 Wine prefix。公开 out 中旧 FG Helper 的源码闭包不匹配 M9；私有恢复配置已显式改为复用 `.worktree/fg/out` 的已封存 Helper42bc6f7，未复制二进制或改写 manifest。主线正式检查器对2X–6X的 source closure/artifact/model 身份核对均通过；这些是只读准备检查，不是GPU实测。

## 新 boot 恢复与下一处真实切换缺陷

上述恢复安排现已执行；最新事实见 [新 boot 基础验收](M8_BOOT2_RECOVERY.zh-CN.md)。
New1080Q NR→SR+DLSS2、F9 和正常退出通过，旧 HOLD 未改写；GPU 随后交给 M9 完成 Old 多尺寸/MFG。
M6 同设备接线及共享资源契约见 [Native NR 接线草案](M8_NATIVE_NR_INTEGRATION.zh-CN.md)，尚未启用游戏后端。

M9 的 `m9-old1440q-2x-nr-r10` 暴露独立 NR 恢复问题：Q→DLAA→Q 时 FG 每代重建和生成正常，
但暂时的 unsupported domain 进入 `PollPeer` 的 profile_change 停机，Helper 正常退出后不能重新开启。
该会话实际正常退休，没有 HOLD。修复将 daily preSR 的此类不支持阶段视为可恢复暂停，保留当前 Helper
及不可变资源，仍经原来的真实 acquire/obsolete/finalized/CONSUMED 链排空已提交单 job。
非 daily 诊断仍按原 profile_change 退出；exit/rejected/sourceLimit、未知 GPU/peer 状态和所有租约门禁不变。
恢复时仍走已有 source gap/layout_changed/history reset，不能重新消费旧布局 candidate。
新增小型 CPU 检查覆盖暂停、退出/拒绝、有限诊断行为及实际租约/lineage 门禁；它不是 GPU 完成证明。
该修复现已完成受管实机回归（Native bd654b2）：Old 的 Q→DLAA NR224/231、回Q223/230，
稳定窗口165/165；New 对应166/173、176/183，稳定119/119。两场正常rc0，逐代GPU/FD/model及
Native最后使用审计通过，prefix RETURNED，stop request仅最终exit，没有profile_change。
Old stdout仍有10条cold WSI hazard，New整场VUID/sync0，均按原计数保留。
New1440Q 未启动NR时 real117/generated117/order0，补上此前 scaled FG source stamp 的实际生成证据。
详细边界及会话名见 [M9报告的后续回归](DLSS_MFG_M9.zh-CN.md#m8-后续公共修复回归)。
内部SR/FG输出为2560×1440，最终窗口读回1920×1080；不声明物理1440p scanout或画质/热性能通过。
