# 停止后闪烁：隔离实验

基线 `b80ed18`；研发分支 `nr/temporal-stability`。旧 M10 保留在
`m10/nr-quality@86b9689` 和标签 `recovery/m10-nr-quality-20261025`。
旧可运行 Native 的本地恢复目录为 `out/recovery/m10-e5a659d`，使用硬链接，
重建前已解除可变链接器输出的链接，未复制游戏、Wine 或依赖。

候选没有固定五帧调度器：上一任务实际 GPU 使用完成、CONSUMED 发布、
最终确认后才回到 Ready。现有显示过渡由“两源帧或 33.34ms”任一条件结束。
PHStats 在过渡时由 CPU 把候选 edit/meta 绑定至 guide 槽位，变量名称不能
用来推断统计绑定错误。bank-fatal 与 TTL 是待验证的独立原因。

## 单变量 A2a

默认 `BB_NR_VISUAL_TRANSITION=legacy` 完全保留原显示采纳规则。
实验 `cadence` 同时把帧和时间上限改为 6 帧 / 100ms，取先到者；
过渡尚未结束时，新候选走现有 obsolete-discard / CONSUMED / 实际退休路径，
不覆盖显示目标、不重开过渡、不刷新源年龄，不修改共享资源和模型协议。
此策略可能降低候选采纳率；它是可证伪实验，不是已验收修复。

`BB_NR_HISTORY_SAMPLE_INTERVAL=1` 仅用于短诊断窗，默认仍每 30 源帧采样。
Stats 后复制 32 字节 bank state 和 4 字节 Return guard，读回前等待原有
slot 的真实末用完成，不新增 GPU wait。日志把同源的过渡权重、四个 bank
状态、Return guard、有效率与年龄并列。逐帧 Stats 的额外开销意味着不能
将该运行直接作为热性能验收。HUD 关闭时不采样。

## 跑动后停止

`tools/nr_dev/quality_scenario.py SESSION run-stop --label LABEL`
在已准入且可操作的独立游戏会话执行短时满杆+Circle、释放、反向和释放。
这只证明输入日程，真实移动/场景必须由画面和 Motion 捕获确认。
可用 `--capture --capture-index 2 --capture-at 0.1` 请求停止后的 F9 小窗；
请求事件不是 GPU 捕获完成证据。捕获沿用已有 staging 和退休保护。

首次提交仅有 CPU 和构建验证；真实游戏结果随后另记，不合流未证明有益的策略。

## 2026-10-25 迁移电脑前暂停点

用户暂停测试，所有新 CPU/GPU 测试已停止。实现提交 `7751bed`，
同提交增量 Native `scripts/dev-build.sh --test` 通过；独立 transition
边界测试和 24 项历史 CPU 参考测试通过。参考测试的历史文件路径已修正。

唯一新会话 `temporal-old-legacy-r1` 仅进入场景准备，NR Helper 尚未创建；
未完成真实跑动后停止采集，A2a 未运行。不得把这次启动当作 NR、画质或
闪烁改善通过，也没有可供 DSH B 使用的有效 stop-window。

正常窗口关闭后：rc=0，`PASS_NORMAL_TEARDOWN`，
`native_lastuse_proof=true`，`residuals=[]`，
审计 `PASS_NO_PEER_NO_SNAPSHOT_NORMAL_EXIT`，前缀租约 `RETURNED`，
全局 session receipt 为 `retired`。既有历史 HOLD 不作改写。

研究存档仅在 `out/temporal/player/user`，来自旧研究会话的必要 savedata
副本（15,002,530 字节），正常结束只同步至该研究副本。
监督器在真实退休后自动回收 393,213,394 字节派生游戏输入。
本树 out 从 1,360,112 KiB 增至 1,710,860 KiB；本次共享 runs 新目录
84,320 KiB，总净增约 424.9 MiB，主要是保留旧 Native 后的新链接输出、
独立存档与日志。未复制完整环境，未删除独有证据。

恢复顺序：重查本地最新 nrbridge 与 FG 合流；确认构建缓存/运行时的真实
路径与身份；在独占 GPU 窗口中验证实际游戏输入送达；分别捕获 legacy
和 cadence 的相同跑停动作。比较逐帧 valid/invalid/guard/age 与画面，
再决定是否值得继续 A2a 或转向像素级拒绝。逐帧 Stats 属诊断额外成本。
当前实验不合流、不推送为玩家修复。

外部交付保留：M7 `56f3ef61fc42f89578b7932fadc45e655a9c6d5d`
（低波段 context，GPU NOT_RUN），Sky 基线
`ca1c0287fc4b4bbd1cbe304aaa93dc7dd34a417d`。未迁入未经证明的算法。

## 恢复后的诊断与条件隔离

迁移后 boot `bfe7b4b5-26de-4c87-a547-fa869e56af00`。
`temporal-old-legacy-r2` 在 NR Helper 创建前复现已知 Old 冷启动 WSI
WRITE_AFTER_PRESENT（10 次 sync hazard）；普通关闭后 rc0、真实末用完成、
无残留、prefix RETURNED、boot receipt retired。验证错误未被当成驱动故障
或 NR 画质问题，也未宣称 WSI 已修复。

`temporal-new-legacy-r3`：New / 1080p Quality / NR 310.9.16 B / FG Off，
仍为 legacy transition；同步验证、逐帧 Stats、F9 冷捕获同时启用，约 24fps。
实际场景为教堂前，动作日志 `quality-baseline-stop.jsonl`，
`run-stop.mp4` 是 PID 匹配的 X11 窗口视频（不是精确源帧）。
F9 `persistent-history-diagnostic-temporal-new-legacy-r3-capture-1`
保留 3 帧输入/历史和一个候选，均有真实 copy completion；
`out/temporal/r3-readback/analysis.json` 的完整性和最终 SR/Present 关联通过，
不代表动态画质接受。

2048 个逐帧样本中，868 个 valid=0 且 bank/Return guard 未拒绝，
7 个 valid=0 与显示 bank-fatal 同现。停止窗 79 个样本中 33 个 valid=0，
bank-fatal 为 0。捕获的三帧 valid 为 0.7672、0.6678、0.5991，
年龄约 44.3、123.6、179.5ms，接近原有 200ms TTL。
玩家指出此场持续闪，故不能把诊断负载下的 TTL/raw 回退当成原始
正常帧率跑停症状。A2a 尚未运行，不合流任何画质策略。

该场 rc0 / PASS_NORMAL_TEARDOWN / lastuse=true / residuals=[]，
prefix RETURNED，boot receipt retired；capture staging/writer 正常释放。
已把上述条件和 stop-window 通过 Herdr 交给 DSH B，只读分析。
后续先关闭验证，恢复默认每30帧 Stats、不触发 F9，验证正常负载 legacy；
再决定 A2a 单变量，不能混改 TTL 或像素拒绝。

`tools/persistent_history/cadence_report.py` 只读两段保留日志，按源帧关联
候选间隔、过渡、GPU sample 与 bank/Return guard，不推断缺失帧。
旧玩家121415两段日志有5434源帧/1035候选，中位间隔5帧；181个稀疏样本
均非零，valid 均值随候选offset0..5从0.6355降至0.4067。
稀疏采样可能混叠；该趋势是进一步定位线索，不是根因结论。
