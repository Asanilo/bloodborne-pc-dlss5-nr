# 0.6 适配状态

以下正文保留 `5ab490f` 的完整原始安全审查与 CPU 证据，是历史快照；其旧目录、分支、
运行状态和“尚未推送”等句子不代表本轮现状。当前适配见 [功能状态](BBPORT_06_FUNCTION_STATUS.md)。
0.6 中旧相机 detour/boolean mailbox 已移除，保留唯一的安全预执行安装路径；默认仍关闭。
本轮新增 stick-only 的 gate 内原 epoch/capture 校验和按 epoch 清尾巴、关窗终态及冲突开关一致性修复，
没有改动 936B leaf stub。普通与 ASan/UBSan 证据在 `out/native-mouse-cpu/06-astra-final*`，
核心原始合同继续保留；后续实机与玩家手感以本轮实际记录为准，不能沿用旧 BUILD_ONLY 结论冒充实机通过。

最终适配包括 `d986d8a` 的启动writer来源证明及两条路径RX后全范围复验，guest leaf不变；
普通/ASan各8组及实际writer、RX后camera/constants故障注入经Astra独立复核通过。
干净Native6753999完成四阶段受控GPU验证与正常退休。玩家明确确认“明显更跟手，停鼠也正常”，
并确认F1释放、关闭菜单后恢复转镜；默认仍off，不扩展为锁定目标/freecam/长会话全部通过。
实际mailbox消费计数因只读取证EACCES仍UNKNOWN，与CPU唯一消费合同及玩家反馈分开记录。
原候选的源码、原7组夹具和完整审查正文均已保留或被安全适配替代，后续开发以NR主线为准。

# 原生鼠标相机候选：原始安全合同与历史证据

本轮起点为 `7265d3b6ec2ea80afd4fbe1a94d9f323acdd3887`，已正常推送并核验 Fork 远端。
实现位于独立 `dev/native-mouse-candidate-20261020`；默认关闭，仅显式
`BB_NATIVE_MOUSE=1` 请求候选。源码、CPU 验证和完整隔离构建均完成，状态为 **BUILD_ONLY**；实机未验。
稳定运行组合仍为 Native `5635804` / Helper `7f69` / forwarder `9a825`。

## 来源与镜像支持范围

沿用 [全功能审查](BBPORT_06_FULL_PORT_AUDIT.md)，只移植上游
`deadinside28/bloodborne_pc@8185b88d80573fa300595d5c54450183b4109935`
`bbport_guest_hooks.cpp::InstallLookHook/CameraLookAdd` 的相对角度思路，
不移植其运行期 trap 发布、寄存器破坏或回退重复投递行为。

Astra 已只读解析现有私人资产并核实实际上下文：

| 范围（image-relative，右端不含） | FNV-1a64 | SHA256 |
|---|---|---|
| `0x143ac60..0x143fad0`，20080 B 相机函数及冷分支 | `ec5d03c6452b92a4` | `febac727ec4b7ab8901cef79441243c4eeadba08ac4b6229ecde8f9388cd3be0` |
| `0x143cde0..0x143cdf0`，覆盖的 16 B | `88649585e04b5555` | `a93e575d043d03cee87647310ccbfde43f94c3bb149265e78f9acdaae2b19c8d` |
| `0x48f25f0..0x48f2630`，64 B 周期归约常量 | `7c77e08196c17e8d` | `812642b299af7609dc67a2ef339af1b885f4181675e299a190b8f62aaba50ddd` |

SELF SHA256 `d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`；
解析后 ELF SHA256 `cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`。
私人反汇编和复核记录保留在 `out/native-mouse-cpu/private-camera-*`，不提交游戏字节。
FNV 用于兼容性白名单，不是恶意镜像的身份认证。

函数建立 `rbp` 后稳态 `rsp=rbp-0x400`；`r13` 来自 `rdi` 并在函数内保持。
站点重放三个完整指令：`vmulps xmm1,xmm10,xmm1`、
`vmovaps xmm2,[rbp-0x3f0]`、`vmulps xmm1,xmm2,xmm1`。
继续点执行角度累加、周期归约及原游戏 pitch clamp，不改 dt 或速度乘法。
完整函数无间接 jump；整个 text 按所有字节位置扫描相对 call/jump，没有指向覆盖区间中部的候选。
这支持该明确镜像的常规控制流，不能保证任意外部间接指针永远不进入中部。

## 发布与状态保存合同

- 在 loader 重定位、用户 patch 完成后，guest 尚未执行且镜像仍可写时安装。
  检查实际 executable segment 覆盖整个相机函数；归约常量必须有有效可读 segment。
  验证上下文 hash 和完整 16 B 签名，不凭地区或版本字符串放行。
- stub 独立分配 RW，完成生成、地址范围及字节检查后改 RX，最后发布 image 中的 rel32 跳转。
  不新增 RWX、不运行期修改代码、不安装 trap handler。分配、RX、签名、范围任一失败不改站点。
- 无 C++/vDSO 调用的 leaf stub，先用 LEA 避开原 128 B red zone，再保存所用 GPR/RFLAGS。
  保存基准是执行原三条指令后的状态。所有未授权状态均须与基准一致。
- 仅改变 XMM1 低两个 float；高两个 float 按位不变，XMM2 恢复至原 vmovaps 的结果。
  保留其他向量状态、MXCSR、x87、opmask、FS/GS base；不以调用 ABI 推定寄存器可破坏。
  MXCSR 异常 mask 非全屏蔽时不消费，额外角度运算后恢复原 MXCSR。
- 真实消费非零增量才写 `uint32 [rbp-0x3c4]=1`、`uint8 [r13+0x15c]=1`、
  `uint32 [r13+0x154]=0`。不写 `+0x158`，不直接写相机角度字段。
  原摇杆输入清零 `+0x154` 的 8 B，上游鼠标只清 4 B；这里保留上游鼠标语义，实机需评估差异。

## 输入消费与异常合同

固定布局 POD mailbox 的对齐整数 gate 使用明确的 acquire/release 原子协议。
宿主生产/reset 临界区短；guest 仅一次 try-acquire，不自旋，不获取 GPU 锁。
权威字段读写都在同一 gate 内；唯一例外为对齐的 `published_epoch` 镜像：
在 gate 内以 release 发布、HostInput 以 acquire 读取，用于无 gate 的代际通知。
不能依赖 `std::mutex` 或 `std::atomic` 的私有布局。

每批输入带捕获时的 epoch 和单调 sequence，只有 dropped、native queued、stick 一种路由。
native queued 后仅可 consumed 或 cancelled，绝不重新转送摇杆。
reset 递增 epoch 并清 pending；旧快照不允许重新贴新代际。sequence 不因 reset 重用，
epoch/sequence 溢出时禁用候选，不能回绕复用。累计净角度为零也退休 batch，但不写相机标志。
捕获关闭、失焦、文本和现有 overlay 屏蔽取消旧数据；已在线性化点取得的单次消费允许完成。
这些屏蔽信号不能代表已经识别所有 guest 菜单。

HostInput 在接收并累计事件时记录 published epoch；新代事件先丢弃旧累计，Read 发现过期也丢弃。
这不是 SDL 队列中硬件事件发生时间的识别。最终路由即使处于 fallback 也先检查原 epoch。
生产/reset 的锁序为 HostInput mutex → mailbox gate；相机和路由不反向取得 HostInput mutex。

最早 pending 时间不随追加移动刷新，期限 100 ms，恰好 100 ms 也取消。
host 与 stub 均检查过期和倒退时间。
stub 在取得 gate 后、消费提交前直接执行 Linux x86-64 LP64
`SYS_clock_gettime(228, CLOCK_MONOTONIC=1, &timespec)`，检查返回值、timespec 和转换溢出。
不用本地一次 20 ms sleep 校准的 TSC 作为期限真源。
实际 mailbox 与 stub 统一使用整数微秒，`age >= 100000 us` 取消；host 也在 gate 内最终取时，
避免跨线程提前采样造成假的时钟倒退。16 rad 的单轴及累计上限只限制 native 入队，
未安装或处于摇杆 fallback 时的有限大位移保留原饱和行为。
时间失败不消费；正常信号返回须保留状态，不能把离开持 gate stub 的非局部跳转称为可恢复。
实际消费的线性化检查通过，不意味着操作系统此后不会抢占。

超时永久取消旧 pending，进入新 epoch；未来新位移才走摇杆。
只用相机站点的零位移 heartbeat 恢复 native，fallback 数据不作为探测包。
heartbeat 递增序号，reset/fallback 记下水位，只接受水位之后的有效进展；重复观察不刷新时间。
超时取消的同次相机调用不能立即恢复，时钟失败和未屏蔽异常不能当成消费能力通过。
heartbeat 证明站点执行，不能证明自由相机/锁定模式；100 ms 模式回退仍是启发式。
拒绝 NaN/Inf 和超过明确累计边界的角度，禁止旧数据复活或重复消费。

## CPU 验证与限制

先运行不初始化 bb-probe/Vulkan 的纯 CPU 夹具：实际生产生成器/mailbox、合成 guest frame，
对比原指令参考路径与 detour 的 GPR/RFLAGS/XSTATE、red zone 及内存允许写集合。
覆盖边界、争锁、reset、旧 epoch、超时先恢复 camera、时间失败、未屏蔽异常、正常信号返回；
故障注入分配/RX/rel32/签名/segment hole/重复安装，不得留下半发布代码。
信号测试包括独立 altstack 的正常 SIGUSR1 返回，以及条件允许时由 ptrace 在真实 syscall-exit
注入信号的确定性边界；系统禁止 ptrace 则明确报告未执行，不能以随机压力替代。
之后完整构建保持 BUILD_ONLY，玩家实际手感确认前不设默认。

执行入口（不加载 bb-probe、Vulkan、Wine 或游戏资产）：

```bash
python3 tools/test_native_mouse.py --out out/native-mouse-cpu/final-regular
python3 tools/test_native_mouse.py --out out/native-mouse-cpu/final-asan --sanitize address,undefined
python3 tools/check_native_mouse_stub.py
```

最终普通与 ASan/UBSan 两套均为 **7/7 通过**，覆盖 mailbox、实际 stub/安装器、完整 XSTATE、
真实 HostInput→全局 router、HostInput、实际 pad、复合键录制往返。每套 `validation.json`
记录命令、退出码及全部受测源码 SHA；ASan 因托管执行 tracer 的 LSan 限制显式关闭 leak 检测，
不宣称泄漏检查通过。DS 在最后纯路由上限修复之前另跑过 mailbox/stub TSan；最终修改后未重跑 TSan。
NR 工具 CPU 回归 195/195 通过，NR 实现及协议文件未改。

实际汇编模板与嵌入代码逐字一致、无待应用 relocation；936 B，SHA256
`9527003802a32b3110e579e20365ce2d758297df499615a7dc1dd8094a8001b3`。
`check_native_mouse_stub.py --write` 可显式重生成数组与两个 pointer slot，重生成后必须重跑状态测试。

XSTATE 夹具用相同 XRSTOR 输入对比真实原三条指令与生产 stub，CPU 观测 XCR0=`0x2e7`、
标准 XSAVE 面积 2440 B；非 init 测到 x87、16 XMM、16 YMM 上半、opmask、ZMM 高分量及 PKRU 采样值。
比较架构字段，跳过未定义 padding；其他已启用但 init 的组件只证明保持 init，遇未知非 init 组件拒绝覆盖声明。
检查 GPR/RFLAGS、原 red zone、只读乘数、三处允许写的完整内存边界及 FS/GS。
正常信号测试从 ucontext 证明确实中断正在消费位移的 stub，并验证 handler 在已登记 altstack 上。
确定性 syscall-exit ptrace 注入未执行：本轮 `PTRACE_TRACEME` 能力探测返回 EPERM（1），
证据 `out/native-mouse-cpu/signal-boundary-limit.json`；压力覆盖不能替代这项确定性覆盖。

另用私人镜像代码/常量的独立 CPU fixture 调用真实 `BbMouseLook::PatchImage`，不执行私人 guest 代码，
捕获开/关、错误上下文、未设置开关、非法值 `01` 五项均通过；同时验证 installed 未 ready 时 30 rad
仍走真实全局 router 的 Stick 路径、成功重复调用不撤销安装、stub 实际 RX 权限。
记录 `out/native-mouse-cpu/private-production-profile.json`；实际字节只在 ignored 私人输出，未提交。

DeepSeek 实现经 Astra 定点复核修正了 RFLAGS、gate 内取时、超时触发批次、恢复升代、溢出、
发布失败与 segment 页覆盖等边界。Astra 最终确认默认关闭的 BUILD_ONLY CPU 候选可收口，
只读核对了最终 7 组日志及源码 SHA；未冒充独立重跑 sanitizer 或游戏手感验收。
执行模型实际为 WorkBuddy DeepSeek V4.1 Flash **High/standard**，官方标注 1017 的路由余额不足，
WorkBuddy 未标日期，因此不声称已核实该路由就是 1017。Astra 关键收口为 **High/default**，均未启用 fast。

## 实机条件与剩余风险

GPU/游戏验收须使用现有安全工作流：当前 boot 有效 scope、精确新候选 SHA/产物、现行 pins、
HOLD/holder/内核与前序检查，并显式选择候选，不能覆盖稳定 `5635804`。
现有 `tools/nr_dev/runner.py` 过滤宿主环境，`BB_NATIVE_MOUSE=1` 不会自动透传；
它也只接受开发根 `build/native` 或按完整 commit 命名并核验的 `build/baselines/<SHA>`。
本轮隔离构建目录不会直接成为可运行 baseline。实际验收前需由协调方补充经过审查的显式候选入口，
把开关绑定到 scope 与运行 manifest，并将干净候选构建登记为单独 SHA baseline；不能通过绕开 Runner
来试鼠标。该接入留待现有安全流程批准的实机准备阶段，当前 Runner 与已验证运行组合保持原状。
先验证 fallback 与自由相机，再覆盖锁定切目标、菜单、文本/overlay、Alt-Tab、捕获开关、
小幅瞄准/快速甩鼠/停止、手柄混合输入及正常退出。CPU 通过不构成这些游戏行为或手感通过。

仍需实机确认：锁定相机是否经过该站点（heartbeat 不能辨别所有模式）、首个超时窗口的丢弃与回退手感、
`+0x154` 仅清 4 B 的行为、每次相机更新的 syscall 开销，以及小幅精度/快速甩动/停止是否满足玩家预期。
严格 hash 会拒绝相机函数被模组修改的镜像；这时保持旧路径，不尝试绕过冲突。
回退首先取消 `BB_NATIVE_MOUSE=1` 并通过现有安全入口重新启动；不热卸载成功 stub。
源码回退时先 revert 本报告的收尾文档提交，再依序 revert 输入集成 `3f80209`、独立相机模块 `5cadbc1`；
也可从保留的 `7265d3b` 新建回退工作分支。未合并原 Native 分支或 master。
本轮未制作/部署 AppImage，未引入 DLC、在线或上游渲染重构。

## 提交与实际构建身份

- `5cadbc1`：独立相机、mailbox、模板和专用 CPU 验证，保留上游来源。
- `3f8020937f58a3c8f1d98a0e9e2919efc9241924`：窗口/pad/loader 接入、唯一分流、捕获同步、CPU 入口及报告。
- 后续收尾只改本报告和审查索引，不冒充新的实际编译源码。

完整构建命令：

```bash
BB_BUILD_DIR=/home/arinp/Code/bloodborne_pc/out/native-mouse-candidate-20261020 bash tools/nr_dev/build.sh
```

从空输出使用现有锁定依赖构建通过；PGO off、LTO off、8 jobs，实际源码为上面完整 `3f80209`，
`source-diff.patch` 为 0 B。四项产物 `artifacts.sha256` 全部核验通过，manifest SHA256
`ce6c73a63acccff83c355ea4339e67f2e267e6f5dddf029a5673909912a602d8`；额外工具
`bb-gpu-capabilities` SHA256 `9ef6af63a275c85a9fcac8f838f4036c59664cb947d57e87310fbdb4140d71d8`。
构建日志、锁定输入及来源在该隔离目录，汇总 `out/native-mouse-cpu/full-build-validation.json`。
最终 CPU 记录已逐个验证输入 SHA 对应干净 `3f80209` 的 Git 内容，而非仅用起始 HEAD 表示代码身份。

稳定运行组合/配置及 manifest 的 8 个保护 SHA 保持，原 Native master=`3630bc52`、NR 分支=`feda4a1`，
游戏侧分支仍停在已推送并核验的 `7265d3b6ec2ea80afd4fbe1a94d9f323acdd3887`。
本轮候选提交保留本地，未额外推送候选分支。

下一轮 AppImage 应成套使用匹配的 `bb-probe` 与 `gpu/libbbgpu.so`：内部 HostInput C 接口增加 epoch，
不能混用新 lib 与旧 probe。前轮复合键还要求匹配录制工具/launcher。候选鼠标继续默认关闭，
经玩家实机确认后再决定默认策略；Helper 协议、模型与当前运行 pins 不随本候选改变。
