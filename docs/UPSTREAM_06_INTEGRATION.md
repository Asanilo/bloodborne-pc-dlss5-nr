# 上游 0.6 集成候选

## 2026-10-20 功能补齐增量（现行）

本轮没有再次合并上游。游戏侧成果已正常快进收回 NRBridge 开发主线；实际空输出冷构建
及四阶段GPU验证 Native 为干净 `6753999346d4314430eb7ae95ae7c7cc81884fcc`。
原生相机只安装一套，结合候选epoch/mailbox与状态保护，并严格核对既有FPS启动写入，
保持936B guest leaf及帧率行为；默认仍关闭。玩家已确认更跟手、停鼠正常、F1释放、关菜单恢复。
在线两套库已实际构建并通过禁网注册，DLC挂载生命周期及失败边界已通过实际源码CPU/ASan；
真实联网、真实DLC资产、手柄及存档写后重载仍待验，不能用构建成功代替。
resident实际24 Evaluate、23captured/1obsolete；Astra独立复核实际SR/Present与正常退休通过。
功能状态、限制及证据见 [BBPORT_06_FUNCTION_STATUS](BBPORT_06_FUNCTION_STATUS.md)。
旧稳定5635804/7f69/9a825及配置不覆盖，master不合入，不制作AppImage。

以下为初次Merge的历史快照；其中鼠标“编译期关闭”、online“未构建”等已由上述增量覆盖。

本候选从 gameplay `7265d3b6ec2ea80afd4fbe1a94d9f323acdd3887` 正常 merge
上游 `8185b88d80573fa300595d5c54450183b4109935`；分支 `sync/upstream-0.6`。
已完成 CPU、空输出完整冷构建及四阶段受控 GPU 回归，收回 NRBridge 开发主线。
实际验证的 Native 源码是 `4b4fdcc664fc5259a291a0a422db25a5e02cc35c`；
`0d073d6` 仅修正验证 profile 为 16:9，后续文档提交不冒充新的 Native 产物。
旧稳定运行组合不覆盖、不自动切换；本次是有限集成验收，不是发布或长期稳定性验收。

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
| 鼠标 | 保留上游源码；安装入口编译期关闭，`BB_NATIVE_MOUSE=1` 也回退摇杆。独立 `5ab490f` 候选未叠加 |

Astra 已定点复核 CP/GNM/sampler、NR 末用、owner pump、Present、observer ABI 和冷诊断迁移。
另已独立复核下述实际 GPU 证据；鼠标手感仍不通过，原生候选保持独立。

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

## 冷构建与实际 GPU 验证

新空目录 `nrbridge-dev/build/baselines/4b4fdcc664fc5259a291a0a422db25a5e02cc35c`
完成全部 Native 构建，来源 diff 为空，四产物及 provenance 已只读固定。Helper 未修改，
协议 wire1/interface3 与锁定导出 SHA 一致；CPU Native 195/195、Helper 11/11。
额外实际 runtime/rwlock/sema 六项、launcher/save 十二项通过，未把零测试命令当覆盖。
可选 online 库因缺 nlohmann_json CMake package、miniupnpc 未构建；在线和真实 DLC 挂载未验收。

同一实际 Native `4b4fdcc`、Helper `7f69`、forwarder `9a825`，在 boot
`54bbe091-3e4f-4fdf-abf3-4505c72a7f71` 经既有排他 Runner 和真实前序递进执行：

| session 前缀（均后缀 `-4b4fdcc-20261020`） | 实际工作负载 | 结果 |
| --- | --- | --- |
| `sync06-sr` | SR-only，加载 Clinic、菜单、摇杆注入 | 正常 no-peer 退出；本轮 Auto 宽高比不适配外接屏 |
| `sync06-skip1080` | off/skip，120 admitted sources，24 skip | 正常退休，Native 末用 22136 完成 |
| `sync06-persist-skip` | persistent/skip，120 sources，24 skip | 正常退休，Native 末用 23988 完成 |
| `sync06-resident` | persistent/resident，120 sources，24 Evaluate；23 captured / 1 obsolete / 0 skip | 正常退休，Native 末用 28538 完成 |

四轮均 `PASS_NORMAL_TEARDOWN`，实际退出后 kernel 检查通过、holders 为空，无 watchdog/信号/残留。
Helper 轮均有真实 GPU/FD 退休及 Feature create/release 1/1、正常 shutdown/wait0。
Astra 重审 resident 原始 job/model ACK、23 个 candidate、141 次 cache-selected SR 读取及成功 Present，
无重复 source 或关联失败；两个 persistent slot 在 28536/28538 完成后销毁，后续 1845 次 SR 均为 raw。
未发现阻止合回开发分支的回归；这不关闭历史 PM4/Xid31/float HOLD，也不证明画质或热性能改善。

固定私有 profile 为 16:9 后，日志确认 scene1280×720 → output1920×1080。
玩家确认外接屏已铺满、菜单/场景正常，WASD/鼠标可操作；**鼠标视角仍不跟手**。
启动闪动仅报告在窗口出现/铺开期间，进入菜单后正常；未定位或宣称修复闪动根因。
键盘提示是上游按最后输入设备切换的行为；真实 PS 手柄提示切换未验收。
Continue 私有存档加载通过；真实存档写入后重载、更多场景与长会话仍待验收。

原始证据保留于数据根 `runs/sync06-*`；汇总、构建身份与截图在 Fork
`out/upstream-06-integration-20261020/`。四个 scope 已消费，不能重复启动；后续 GPU 仍按现有安全流程。

## 鼠标与回退

原 Native `5635804` / Helper `7f69` / forwarder `9a825`、产物及私人配置不变。
Astra 已比较上游相机与独立 epoch 候选：上游超时后返回摇杆但仍发布 native pending，存在重复消费；
失焦/菜单未清 pending，consumer 未自行验时；状态保存及安全发布检查也弱于候选。
因此本次单独关闭安装门槛。实际安装块与 producer CPU 夹具覆盖 unset/空/0/1/01/true：
installer 零调用、站点 canary 不变、producer 返回摇杆且不入队，Astra 复跑通过。
后续只整合一套 Hook，保留候选三态归属、epoch、consumer 超时/heartbeat、状态与发布检查；
还须适配上游 capture/wheel/自由相机。不能复用候选 CPU 证明覆盖上游 Hook，也不能声称手感已验收。
原生鼠标候选 `5ab490f` 已正常推送到 Fork；本次不合入该候选、不默认启用原生 Hook、不制作 AppImage。

回退只需继续使用既有稳定组合；新开发源码与原运行选择分开记录，不覆盖旧只读产物。
如需从未来已合入的分支撤销 merge，应正常 `git revert -m 1 <merge>`，不重写共享历史。

## 分支收敛与剩余验收

上游 merge `4fd3ad6` 的第一父是 gameplay `7265d3b`，其祖先完整保留原 NRBridge 和复合键等成果。
验收后正常快进 NRBridge 开发分支；master、testing、Helper 和 upstream 引用不变。
已正常删除 19 个本地、18 个远端废弃 `poc/*` 引用，没有建立归档标签或改写共享历史。
已合入的 gameplay/sync 引用随后正常退役；鼠标 `5ab490f` 独立保留。
本轮不删除旧构建/runs；有独有 CPU 证据的集成目录保留 detached，鼠标工作树及其代理 cwd 保留。
其它历史 dev 引用中仍有独有研究成果，本轮未扩大删除范围。

下一轮 AppImage 尚需独立处理依赖闭包与可选 online 构建、路径/打包验证，补更多场景、
存档写入重载、手柄、正常退出与长期回归。鼠标需单独整合唯一 Hook 的安全修复并经 CPU、
受控实机和玩家手感验收；未验收前沿用摇杆路径。bbport 与 OptiScaler 继续分别跟进各自上游。
