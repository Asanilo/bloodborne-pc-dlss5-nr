# 上游 0.6 集成候选

本候选从 gameplay `7265d3b6ec2ea80afd4fbe1a94d9f323acdd3887` 正常 merge
上游 `8185b88d80573fa300595d5c54450183b4109935`；分支 `sync/upstream-0.6`。
这是 BUILD_ONLY 集成候选，尚未完成冷构建及真实游戏/GPU 验收，不能替代运行基线。

## 合并与本地职责

初始 155 个冲突：120 个目录迁移 AU、23 个内容 UU、12 个旧文件 UD。
保留自动合并；旧 renderer 的本地职责移至 0.6 实际模块，不链接两套 renderer。

| 本地功能 | 0.6 对应与适配 |
| --- | --- |
| NRBridge、持久历史、Helper 协议 | `gpu/render`，保留 typed control、origin、release/ACK、同 Scheduler 最大末用 tick；NR 与 FG 同开拒绝进入 NR |
| 提交发布、退休、idle | `gpu/cp`、`gpu/platform/gnm`，队列锁内发布，逐队列水位，通知 exchange，generation 检查与 release 同一事务 |
| PM4 边界与冷证据 | decoder/graphics/compute 全部验证实际包范围；owned DCB 摘要、writer footprint、错误上下文迁入新路径 |
| sampler GC | 新 texture cache，move-owning 延迟销毁；保留三项资源追踪事件，无 cache 指针闭包 |
| owner pump / Present | 新 CP 排序命令与 DrawPipe drain；区分真实、插帧、blank、reuse，只有实际新游戏帧发布 NR 通知 |
| 输入、中文、存档 | 保留复合键、旧 walk/look 配置兼容、中文和存档管理；采用上游输入与菜单新增能力 |
| 鼠标 | 合并上游路径，但只有精确 `BB_NATIVE_MOUSE=1` 才安装；默认保留摇杆鼠标。独立 `5ab490f` 候选未叠加 |

Astra 已定点复核 CP/GNM/sampler、NR 末用、owner pump、Present、observer ABI 和冷诊断迁移。
检查仅覆盖源码与 CPU 行为，不能证明实际 GPU 生命周期或游戏手感。

## 已完成 CPU 验证

- Native 工具回归 195/195；普通 Python 142 项中 114 通过、28 因构建夹具缺失跳过。
- 复合键、pad、HostInput、捕获/焦点/滚轮唯一消费与旧配置；pad/HostInput ASan/UBSan 通过，未声称 leak 检查。
- 中文设置与 launcher、存档管理；content 生命周期与 file mount 表测试通过，不声称真实 DLC 挂载或在线验收。
- 实际 decoder 边界、CP 发布/跨队列退休/通知/idle、sampler tick 延迟销毁；四项故障负对照按预期失败。
- 实际 RunCompute 三段 ring 组包、Wait 重试及越界拒绝，ASan/UBSan 通过。
- 实际 GNM writers 的 footprint、framing、短 reservation、canary、null PS 与 CS 单记录通过。
- 实际 Present 路由及三项负对照，实际 observer entry/exit stub、寄存器/FP/栈转储夹具通过。

可复跑的新增 CPU 测试：`gpu/cp/tests/`、`tests/pm4/gnm_writers_test.py`、
`tests/pm4/present_identity_test.py`。详细执行日志位于本机隔离 `out/upstream-06-integration-20261020`
与集成树 `out/cpu-merge`、`out/cp-ownership-review`，不提交生成二进制。

## 验收与回退

原 Native `5635804` / Helper `7f69` / forwarder `9a825`、其产物和私人配置保持不变。
当前 boot 与旧安全记录不匹配，既有 preflight 拒绝复用旧 scope；GPU 前须核对当前 kernel、holders、
历史 HOLD 和精确候选身份，按既有流程建立新的受控 scope，不修改准入门槛。

随后分别完成 SR-only、NR、输入/菜单/存档/正常退出，再由玩家确认鼠标手感。
上游相机和独立 epoch 候选尚须定点比较；不能同时安装两个 Hook，也不能复用候选的 CPU 证明覆盖上游 Hook。
正式验收前不合回 NRBridge、不退役 gameplay/sync/mouse、不切换稳定产物、不制作 AppImage。

回退只需继续使用既有稳定组合；本次只在独立集成分支和输出目录工作。
如需从未来已合入的分支撤销 merge，应正常 `git revert -m 1 <merge>`，不重写共享历史。
