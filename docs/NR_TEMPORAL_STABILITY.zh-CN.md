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
