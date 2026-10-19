# NRBridge 构建与私有运行入口

现行工程规范见开发根 `CURRENT.md`，阶段、历史故障和画质证据见 [NRBRIDGE_PLAN](../../docs/NRBRIDGE_PLAN.md)。本页只说明当前入口，不把旧运行报告当成启动指令。以下命令从 `/home/arinp/Code/nrbridge-dev` 执行。

当前推荐显式组合：Native 只读 baseline `563580452e46ea253cecbf75c24ac65579ee031b`、独立 Helper `7f69c1e556810b2b`、forwarder `9a8251430ccfa68a`，资源配置 `localinputs/runtime-s3-20261019.json`。此组合已完成 Clinic SR 正常退出、off/skip 各24个 skip，以及24次真实 Evaluate（23 captured、1 obsolete）的正常退休；完整当前 boot 内核和 holders 检查通过。这是有限加载/传输/退休验证，不代表亮尾修复、热性能或日常发布验收。

## 构建

```bash
# 隔离 Native 输出；不覆盖现用 build/native 或只读 baseline。
BB_BUILD_DIR="$PWD/build/convergence/native" bash native/tools/nr_dev/build.sh
# 独立 Helper 与协议导出；forwarder 单独构建，不替换运行选择。
python3 helper/nrbridge/build.py
python3 helper/nrbridge/forwarder/build.py
```

Native 包装器的 `NR_SOURCE_DEPS_ROOT` 默认是开发根的 `inputs/source-deps`，包含已打补丁的 FSR、imgui、LibAtrac9 和各自 `*.content.sha256`。`NR_BUILD_ENV_ROOT` 独立选择 SDK/sysroot 环境，默认仍是稳定 `bloodborne_pc/out/deps`。完整源码依赖清单、环境实际导出及本仓库固定协议副本在编译/输出副作用前校验，缺失、漂移或错误依赖链接会拒绝。不要把本地 gitlink symlink 提交进 Git。

输出包含 `build-inputs.json`、源码 HEAD/diff 和 Native 四产物 SHA；Runner只接受 `build/native` 或已核验的 `build/baselines/<完整提交>`。`NR_DEV_ROOT` 只改变构建默认根，不迁移 Runner 的固定 `ROOT/runs` confinement。默认构建隔离旧 P0/Q 实验路径；完整构建不等于实机验收。

`native/tools/nr_dev/prepare_helper.py` 和旧 `helper_runtime/build.py` 现在委托独立 Helper 仓库，可用 `--helper-root` 或 `NR_HELPER_ROOT` 选择已准备源码。Native只保留固定协议副本及 peer 退休验收。Legacy配方和部分来源记录仍只读保留，但其运行二进制/部分历史环境已退役；`--legacy-rebuild` 不属于当前构建流程，需先恢复对应历史输入，不能假定完整旧环境仍在。

## 资源配置

```bash
# 只读 CPU 校验；不打开 GPU 锁或运行 Wine。
python3 native/tools/nr_dev/prepare_runtime_inputs.py \
  --check "$PWD/localinputs/runtime-s3-20261019.json"
```

新配置显式选择只读游戏资产、私人 seed、GE `files`、sysroot 库目录、Helper PE/manifest、forwarder/model、实际系统 NGX `.so` 及 NGX Wine DLL 目录。配置和私人绝对路径只放开发根 `localinputs/`，不提交游戏、模型、存档或生成的运行 pins。公开格式见 [runtime-inputs.template.json](runtime-inputs.template.json)。需要建立另一个配置时，填写模板后封存到全新文件：

```bash
python3 native/tools/nr_dev/prepare_runtime_inputs.py \
  --draft "$PWD/localinputs/runtime-draft.json" \
  --output "$PWD/localinputs/runtime-next.json"
```

生成器拒绝覆盖配置或 pins。独立 pins 锁定完整 GE/sysroot/NGX Wine DLL 目录的文件 SHA、大小及 symlink 真实目标，并锁定所选 Helper/manifest、forwarder、模型、系统 NGX、`eboot.bin`、seed manifest 与全部私人 seed 存档；其他实际系统运行文件可用重复的 `--extra-runtime-file` 纳入。配置中的目录和所选 DLL/`.so` 使用 `realpath` 后的规范路径。其余游戏资产保持只读引用，不声称整份游戏都经过逐文件 SHA 审计。

新模式运行准入不依赖历史源码、SDK 头、编译器或旧预检 PE；构建来源单独记录。启动及开启 Helper/NR 前校验资源、配置和 Helper 协议身份；run 内生成的 `helper-runtime.json` 也记录 SHA。旧 Native 缺少新接口标记时拒绝新配置。标记只证明接口兼容，真实 GPU/FD 完成仍以实际审计为准。

## 受控验证与私有试玩

所有 GPU 启动由协调方排他执行。推荐入口显式指定上述新配置和已验 Native baseline；不自动发现或切换全局 current 配置。下一轮 resident 受控观察可以使用公开源码入口：

```bash
NATIVE="$PWD/build/baselines/563580452e46ea253cecbf75c24ac65579ee031b"
INPUTS="$PWD/localinputs/runtime-s3-20261019.json"
# 协调方准备该新scope后，仅在session尚未使用时执行。
SESSION=play-converged-20261019
SCOPE="$PWD/inputs/context-$SESSION.json"
python3 native/tools/nr_dev/play.py \
  --session "$SESSION" --context-recovery "$SCOPE" \
  --native-build "$NATIVE" --local-inputs "$INPUTS" \
  --seconds 240 --watchdog-seconds 360 --source-limit 7200 \
  --pm4-cold-trace --pm4-producer-context
```

`play.py` 固定 resident/persistent，先只读核对 scope 的实际 boot、原 HOLD 摘要、精确 session/请求/诊断开关、build/资源身份和真实前序退休，再启动监督进程。该旧scope已失效，不能直接执行上述命令；新scope仍须匹配真实前序。上述7200预算要求匹配已有 resident 正常退休前序；不是通用放宽。`--context-recovery` 使用现有精确 schema，不生成许可、不解决旧故障，也不能复用已消费 session。Runner在持有 GPU 锁后重新检查完整前序、内核、holders 和资源身份，控制动作也复检。新 HOLD 或任何证据漂移都会拒绝。

没有 `--context-recovery` 时仍走原 `preflight`，旧 HOLD 和当前宿主安全要求继续生效；不能靠省略 scope 进入同一 boot 的未知上下文。普通私有入口默认仍是480秒计划关闭、600秒 watchdog、36000 source 预算。传入 scope 时所有请求参数必须完全匹配，默认预算不会自动套用到受控记录。

游戏出现后，用 Space/手柄 Cross 继续到可操作场景，再回终端按 Enter。入口核验真实 Helper GPU/FD readiness 后开启 NR，并尝试切回对应游戏窗口；场景确认不能只凭加载日志。正常关闭窗口结束。Delete 打开 NR 菜单，模型 Style/Intensity 需显式 Apply；F8 是显示旁路，F9 请求诊断捕获。冷诊断和捕获不能用作热性能评分。

SR-only、off/skip 和 persistent/skip 使用同一个公开 `runner.py start`，显式填写匹配 scope 的 `--mode`、`--sky`、预算和诊断参数；可用 `--help` 查看完整接口。启动后按实际观测依次调用：

```bash
python3 native/tools/nr_dev/runner.py scene-ready "$SESSION"
python3 native/tools/nr_dev/runner.py start-helper "$SESSION"  # 需要 Helper 的阶段
python3 native/tools/nr_dev/runner.py arm "$SESSION"           # readiness 通过后
python3 native/tools/nr_dev/runner.py stop "$SESSION"          # 正常关窗请求
```

SR-only 不调用 start-helper/arm。所有阶段共用真实运行目录和同一锁内检查，不依赖私有 analysis 启动脚本。每个 session 独立保存 profile、缓存、Wine prefix、生成 guest、manifest 和日志；游戏资产与正式存档保持只读。默认复制私人 NRTest seed；仅 `PASS_NORMAL_TEARDOWN` 且重新审计通过的旧 dev session 可用 `--continue-session SESSION` 复制私人存档/NR配置到新 run。HOLD 即使另有恢复证据也不能作为续玩来源；受控 scope 当前要求全新 seed，不允许 `--continue-session`。

正常退出审计完成、子进程退出且无残留后，Runner自动删除本次 `profile/out` 的7个可再生文件：`boot.bin`、`eboot.elf`、`entry.bin`、`boot-libc.bin`、`boot-linked.bin`、`content.bin`、`patches.bin`。删除前的大小/SHA及执行结果记录在 `result.json.generated_inputs_cleanup`；生成JSON、日志、存档和诊断仍保留。HOLD、watchdog、中断、缺失末用证明或异常文件链接均不自动清理。Native/Helper继续复用选定build，不为每个run复制或重编译。

## 当前部署与已退役的历史构建

2026-10-19按用户要求仅保留上述Native `5635804`、Helper `7f69c1e556810b2b`和forwarder `9a8251430ccfa68a`。旧 `build/native`、c8fb/3608等二进制及旧Helper组合已退役，Git源码/分支和历史来源记录保留；这些旧路径不再是可用回滚入口。

正常试玩必须显式传入上文的 `--native-build` 与 `--local-inputs`。当前CLI仍解析legacy兼容参数，但未部署legacy运行产物，不能把CPU兼容测试当作旧组合仍可启动。旧版需先恢复其完整输入并重建。

此前准备的 `context-play-converged-20261019.json` 绑定旧boot `199949cd-b649-4b57-b99f-a502f91a2118`，重启后已失效。上文命令仅展示已验证组合和请求参数；下次执行前必须按现有安全流程准备匹配当前boot的新session/scope，不修改或复用旧许可。本次清理没有创建新GPU许可或启动游戏。

## 结果与限制

正常退出审计检查真实 Native/Helper 末用、精确 origin tuple、共享资源/FD 归零、模型释放/shutdown、正常 waitpid；无 peer 的退出也要求明确的无 owner/snapshot/pending-use 与 preparation join 证明。CPU Present、计数归零、kill 或无残留进程不能代替完成证明。watchdog 只请求正常关闭，未知状态保持 HOLD，进程仍在时继续持有锁。

查看每次 `result.json`、`retirement-audit.json`、实际终端 wire/FD 证据和当前 boot 观察。`present-metrics.json` 统计真实非复用 Present 间隔，不是画质验收；`gpu-memory.jsonl` 的整卡数据包含其他应用。有限生命周期回归不等于间歇性加载故障消除，也未证明亮尾、鬼影、闪烁或热性能改善。本轮不制作 AppImage，也不开放日常无约束发布。
