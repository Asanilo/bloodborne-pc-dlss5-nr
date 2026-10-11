# C3 常驻真实游戏 NR 候选交付

2026-10-11。Integration 已完成串行实测，C3 窄 profile 正负关卡 PASS；B 只读复核证据，没有自行运行 GPU。唯一契约由 Integration 维护：`../integration/C3-CONTRACT.md`（相对于 game-poc 根）。C2 重要基线提交为 `2b0cb9206ee96668f146ea8b9cfd5aec8b572467`；本说明随 C3 重要基线提交；完整 C2 历史保持。

## 构建和冻结证据

最终候选：`bbport/runs/c3-build-20261011T131247Z/build-result.json`，PASS/exit0。该目录保留 build.log、commands.txt、33 项编译输入的构建前后 SHA256 清单、source-snapshot 和四项 artifact-sha256.txt。以前的 130344、130705、131038 构建记录完整保留；131247 增加呈现关联的最小拒绝检查。

`out/gpu/libbbgpu.so`：`52827a2547bcd3db94af6d74f9120d9420024f80dab26fc5793c1865f7265081`。
`run_game_c3.sh`：`872cc7a454afb5e33a457ee1f935e43d20bcf448dd103982b6390c8610d688cf`。
A 的 helper 保持冻结：`opti-worktree/poc/game_nr/c3/out/wine_game_nr_resident.exe`，`3f77b4b77a807472f3342f314ad6121dbeb790ca33934fb8883d5d4161567f39` / 1116160 bytes。本候选不修改 helper、契约、旧 C2 档案或稳定仓库。

## 实际改动位置

- `c3_nr_process.{h,cpp}`：后台 posix_spawn、绝对 Unix 64 位 preloader/loader、独立 prefix、真实 PE 后代 PID/starttime 身份核验、四帧/120 帧配置、持续 waitpid。PID 就绪不冒充模型/资源就绪。
- `c3_game_bridge.{h,cpp}`：使用游戏现有 Vulkan device/Scheduler；四 DMA-BUF 导入、真实 FD payload/UUID/需求校验、常驻单 slot。实际当前 SR/Depth/MV 同 texel 转换后发布动态 fN；真实源帧、previous source、SR reset、NR reset/reason 与 history/params 全部逐帧验证。
- `c3_game_bridge.comp`：原 SR/guide 转共享 float 和返回 float 转独立 half 图；不覆盖原 SR。
- `c3_frame_validate.{h,comp}`：全像素 GPU 有限/poison/alpha/signature 检查，64B canonical 布局。额外 native-only 16B 检查**实际转换后的 RGBA16F 图**全部 8294400 通道 finite / 2073600 pixels visited，覆盖 float32 有限但 half 溢出；不改变 A 协议。
- `vk_temporal_upscaler.{h,cpp}`：default-off HDR Run 1080p DLAA/j0 hook；root 创建一次 trigger 后推进请求帧数。返回 Color+Output 实际 acquire、检验、release/fence 后 consumed；下一输入等待独立返回图真实 merge 末次 tick 完成。每次 Flush 重新取得命令缓冲。仅 valid resident 选择返回 view；skip/失败保持原 SR。
- `vk_presenter.{h,cpp}`：一项未消费 ledger guard；真实非 upscaled guest buffer prepare/ready/Present 日志，full 帧截图。拒绝旧 source 关联后来的 guest buffer；C3 submitted 仅非 reuse 的 game frame，DrawLastFrame 不重复计数。
- `gpu/CMakeLists.txt`、`build_c3.sh`、`run_game_c3.sh`：必要 shader、隔离离线构建和私有启动 glue。

## 调度与审计边界

首/末和指定 full 帧保留实际 FNV 与大读回：4 帧 smoke 全部；120 帧 0/29/59/89/119。其他帧实际全像素 GPU 扫描 + mix32 xor/sum 和 tiny CPU counter 读回，不跳 Evaluate、不把旧结果算有效。每个 resident 帧需 Evaluate1/result1/resolve1 及累计计数，helper 常驻同 PID/device/model/feature/images；这些已在 smoke4、static120、motion3 的 120 帧中实测。

输入 acquire 四图；Wine acquire/release 四图；native 返回 acquire **Color+Output** 才读 source alpha/signature，release 两图并等待 fence 后 consumed。consumed 不释放本地 return 图的 merge 使用；下一 slot reuse 必须证明 prior merge tick 已提交并完成，最终释放也绑定真实末次 merge GPU 完成。超时/peer exit/类型或有效性错误均 abort+quarantine，保留资源，不正常退役未知在途工作。

呈现仍使用已验收的 `next_nonupscaled_videoout_prepare_after_merge` 关联，不声称完成通用 guest 帧溯源。Run 记录 source counter，后续 display-pass NoteFrameStart 实际加 1；绑定要求 prepare counter=source+1 且同 draw Scheduler prepare_tick>=merge_tick，否则明确拒绝且不计有效输出。present Scheduler tick 独立，不能与 draw tick比较。正常释放需另核 Wine result、actual waitpid0、完整同状态 FD 清单；单个 RELEASE 字符串不等于生命周期 PASS。

私有 profile 仍为 1080p DLAA HDR/j0/manual WhitePoint1，实际曝光未知；真实同 texel Depth/MV，不重新去 jitter 或改 SR 算法。首次 NR reset1，后续依据 actual SR reset/source gap；不强制重置成熟 SR。冷初始化120s、控制60s、GPU10s、completion30s均有限，诊断时长不能当纯 NR 性能。C4 60 FPS 目标尚未测量。

## 仅由 Integration 运行

Root 先核 approved helper 和本构建全部 manifest，复制本轮私有 NRTest seed，串行持锁运行。唯一 launcher 接口：

```sh
tools/nr_probe/run_game_c3.sh SESSION resident 4 smoke
tools/nr_probe/run_game_c3.sh SESSION resident 120 static
tools/nr_probe/run_game_c3.sh SESSION resident 120 motion
tools/nr_probe/run_game_c3.sh SESSION skipEvaluate 4 skipEvaluate
```

实际限帧是 `BB_FPS=uncap` + `BB_FPS_LIMIT=90`；不是 fixed-timestep BB_FPS=90。case 为 `bbport/runs/c3-game-SESSION/profile/.c3-profile`。启动无自动 trigger；root 进入 Clinic、确认 ready 后创建 case 下 `c3-trigger` 一次。外层由 `integration/run_continuous_nr.py` 承担 GPU lock、own-PG watchdog 和 cleanup；不得绕过 approval 直接把 launcher 当完整验证脚本。

首 smoke 外层示例（cwd game-poc-20261011）：

```sh
python3 integration/run_continuous_nr.py c3-smoke-20261011 resident --frames 4 --case smoke --seconds 600 --build-evidence bbport/runs/c3-build-20261011T131247Z
```

该命令尚未由 B 执行。运动关要 actual camera/DepthMV 变化证据，不用 hash 变化代替。skip 关需 invalid 被同 predicate 拒绝、SR 实际 Present；timeout-late/helperexit 仅 root 对已核实的私有 Helper 做 pidfd 控制，核 deadline 拒绝、无 reuse、SR 继续与异常 cleanup，不能把 abnormal exit 写 normal release。实际结果及保留的失败见下节；C3 运行结论来自真实日志和离线像素审计。

## 实测收束与证据边界

Root 最终依据是 `integration/c3-final-audit.json`、`integration/c3-gates.json`；已通过产物另存 `baselines/c3-accepted-20261011/manifest.json`。本仓库小索引 `docs/game-nr-poc-c3-evidence.json` 记录各外部报告的 SHA256，未提交二进制、模型、私有 profile、存档或像素 dump。构建时的 `bbport/c3-build-delivery.json` 保留原 BUILD_ONLY/NOT_RUN 历史，不拿该历史字段冒充最终运行结论。

- smoke4：source3865..3868，实际 Evaluate/resolve/唯一 Present 各4，reset1/0/0/0，全字节/FNV/GPU signature/finite/half/alpha及正常生命周期通过。
- static120：source4755..4874，120实际推理和唯一 Present；115非full帧仍真实全像素GPU统计，native144B/Wine64B读回。f29事件与其它线程 printf 相邻，首版 auditor 的前导 word-boundary 漏读；保留原 FAIL/auditorv1及原始日志，修正 marker 匹配后重审通过，未改运行代码。
- motion1：超过 Helper 60s INPUT 控制期限后才触发，Evaluate0，BLOCKED，保留失败。motion2：模型/传输/120 Present 通过，但五个实际 camera view 完全相同，BLOCKED_MOTION_NOT_PROVEN；rx165被读取不等于镜头运动。motion3用实际rx255与五个不同视角证明运动，source4757..4876、120推理/呈现通过；旋转模糊来源和画质未评价。
- skipEvaluate4：Evaluate/resolve0，四帧 INVALID 被拒绝、全sentinel读回、原SR实际呈现和正常退役通过。
- timeout：实际30.005s过期隔离，Helper异常exit3，未接受输出，SR继续。early-exit：source6128实际INPUT后0.092011s检出唯一已核PE被SIGKILL，wait_status9；无complete/consumed/NRmerge，f003实际Clinic/HUD和后续帧统计确认SR继续。
- late：source4341真实Evaluate1/result1/resolve1/fence；30.0036s先过期，49.6311s才有真实OUTPUT_VALID。已隔离session不再读该文件，无accept/consumed/NRmerge；f002实际Clinic/HUD确认SR继续。**不声称显式解析LATE_REJECTED**。Helper异常exit3。三种异常均不声称正常FD退役，outer受控停止后本次PG/privateprefix残留为空。

FD按实际identity而非count判读：smoke Wine83/native136完整表精确相等。static Wine83完整相等；native136→136但17移除/17新增（PipeWire memfd、eventfd/eventpoll、socket及采样/proc/fd槽迁移），不能写整个native表稳定。与四个共享payload交集为0、GPU相关FD identities不变；日志没有音频重连事件，不把FD变动推断成已证明的音频重连。motion3 Wine cold84中一个fd80 readlink ENOENT/stat失败，其余83成功identity与final83相同；native136完整相等；不声称cold全表可读或精确相等。成功关卡四个实际DMA-BUF payload均已退役。

结论限定于1080p DLAA HDR/j0/manualWP1连续正确性及回退。手动WhitePoint不是游戏曝光。诊断full约14.5s/frame、非fullnative约130ms、30s失败预算都不是可玩性能；C4近60FPS目标、质量、非零jitter/缩放/长期游玩和C5/C6尚未验收。
