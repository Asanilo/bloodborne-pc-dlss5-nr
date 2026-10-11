# Q1：正常 Quality 的 unequal-extents 单槽诊断候选

唯一协议为 `integration/QUALITY-NR-CONTRACT.md`，SHA256 `482a5bccba778445efbcc2023e07a998e545fcd66c54ecb72d98c2514b9af7b0`。Root 已完成 noop1、single1、skipEvaluate1 和 resident4 的真实游戏串行验收，初始 Quality 接口可调用性/传输/呈现/生命周期短套件 **PASS**。原始证据与失败历史索引见 `Q1-EVIDENCE.json`；这不是 phase、画质、性能或加载稳定性验收。

## 构建与输入

工作树 `/home/arinp/Code/nrbridge-design/game-poc-20261011/bbport-worktree`，基线 `0da4ecc3fbf15a5212b517f1a95c09aa4a2fe440`。离线复现：`bash tools/nr_probe/build_q1.sh`，使用既有 `build_c0.sh` 的只读依赖/私有 FSR 副本机制，不安装或下载。

最终候选证据：`../bbport/runs/q1-build-20261011T173246Z/`。`build-result.json` 为 PASS/exit0；60 项源码 snapshot/live/pre/post 一致，4 项产物有实际 hash。`candidate-audit.json` 保存逐项复核和 CPU-only wire/profile/bucket 的 34 项断言。首版 `q1-build-20261011T171225Z` 保留，不作为最终候选。

最终 libbbgpu SHA256：`7f6b39036e70d94469e22f0fe70419fe7bdbf5c5cbd14138a00f11c78b5fb88c`。其余三产物及源 hash 以 manifest 为准。旧 Q0 产物和首轮 Q1 产物分别保存在 Root baseline 与各构建目录 `pre-q1-out`，未删除失败历史。

新增 native 文件在 `gpu/shadps4/video_core/renderer_vulkan/`：`q1_nr_process.{cpp,h}`、`q1_game_bridge.{cpp,h}`、`q1_wire.h`；新增 shader 为 `host_shaders/q1_game_bridge.comp`。共享修改仅 CMake 注册、HDR Run 的 default-off hook、实际 MV/depth copy-source usage 和 Q1 Present ledger。旧 C1–C4 独立模块不改。

## 实际路由与证据

仅正常 `Run / !Scaled / reduced`、render 1280×720、output 1920×1080、DLSS SR HDR 成功且私有 jitter 开启时进入 Q1。原 SR output 永远保留；模型成功或 noop 位相等时才把独立 RGBA16F return view 交给原 merge。UI/后处理/呈现路由不改。`Q1_SOURCE_IMAGE` 独立记录实际 scene proxy、Depth proxy、MV 和 post-SR Color 的 image/view/CI，不把 guest backing 当 proxy。

四个 DMA-BUF 为 Color/output RGBA32F1080、Depth R32F720、MV RG32F720。只用游戏 device/scheduler；每次 Flush 后重取命令缓冲。原 Depth130 使用 DEPTH aspect、原 MV83 使用 COLOR aspect，实际 copy-source CI 必须有 TransferSrc；仅 Q1 opt-in 在被复制的 depth proxy/MV 创建时补此 usage，不借 Q0 开关或扩大未复制的 720 scene Color。

输入 guides 同 texel 保值。独立 GPU 本地 copy 原 D32/MV half 和转换结果，CPU 比较 Depth 原 bits 与 MV 精确 half→float bits，并记录 mismatch/FNV、保存私有 leaf 原始样本。Wine 在 acquired guides 上另做真实 GPU copy/readback/FNV。没有插值、乘1.5、空间 shift 或重复 jitter 差补偿。

所有 controls 使用 protocol5、完整 quality_profile/mode/session/gen/frame/source 和严格 JSON 类型；嵌套重复键、非有限数及 >64KiB 拒绝。有限预建 `frames/bucket-%06d`，帧文件及 dump 留 leaf，setup/abort/terminal 留根。setup 用 posix_spawn 绝对64 loader、实际 descendant/PE/run/starttime 核验后才写 exporter native.pid；初始化后台推进，未 ready 直接 SR fallback。

cold120s、控制60s、本地 GPU10s、completion30s 是失败上限，不是实时目标。完成响应须先精确 tuple 和已完成 GPU，再按 noop/NR/skip 的不同 predicate 核验。超时/退出/错误保原 SR、写 native.abort、quarantine 不复用；不杀游戏或全局 Wine。共享出图真实末次 use/release fence 后才 consumed，本地 return 到实际 merge tick 完成后才销毁/复用。终端 FD 按实际 payload/完整 inventory 审计，normal release 仍需 wine-result 和实际 reap0，不能仅凭 RELEASE 行。

Noop 核共享32F bytes及原不可变 SR97→实际 return97 bits；无模型/feature/Evaluate/resolve。NR 核真实 API调用/result、source/raw/output full统计、源/输出 FNV、原 source alpha 与实际 return finite。skipEvaluate 必须被同一正常 NR predicate 拒绝，只诊断 poison/正常 consumed，merge 保原 SR。Wine noop validator 为0B，其源/输出 CPU full audit与 native 独立64B/half检查分别记录，不伪造 GPU model validator 统计。

## Root 串行运行接口

`tools/nr_probe/run_game_q1.sh SESSION MODE FRAME_COUNT CASE diagnostic`：noop1/noop → single1/single → skipEvaluate1/skipEvaluate → resident4/smoke。私有目录 `../bbport/runs/q1-game-SESSION/profile/.q1-profile` 必须由 Root prepare_seed 创建；`q1-trigger` 默认不存在。主机 launcher 本身不提供锁/approval/watchdog，须用 Root wrapper。

Root 首例在自身审核、helper approval、私有 seed 都完成后运行（B 不执行）：

```sh
cd /home/arinp/Code/nrbridge-design/game-poc-20261011
python3 integration/run_quality_nr.py q1-noop-20261011 noop --frames 1 --case noop --seconds 600 --build-evidence bbport/runs/q1-build-20261011T173246Z --execution-mode diagnostic
```

helper 为 A 独立 `opti-worktree/poc/game_nr/quality-nr/out/wine_game_nr_quality.exe`，4d770cf04bd04ac5ad99bd5ccbd7fd5abafcc5f27e1d4b0ec580aa787ae8db14 /1199104B。B 不修改它。Root input 工具确认实际 Clinic、live game、imported/fd-retired exact5 后才触发；逐帧 source→merge→next nonreused guest prepare/present/dump 独立核验，跨 scheduler 不比较 tick。

## 结论边界

`phase_correction=none` 是未做空间补偿，不是已对齐。实际 current/previous viewport/SR jitter 全数保留；NR 无 jitter setter。NR MVecScale1 为命名 production candidate，不能由已验证 SR scale1 推导；k_output NOT_IDENTIFIED、guide/NR phase UNVERIFIED。非零 jitter、正常 Quality 模型调用性和共享逐位正确也不自动证明画质。手动 WhitePoint1 不冒充 live exposure。120/1800 hot、性能及完整可玩验收尚未放行。Q0 162436 与 Q1 首个 skip 174815 两次 PM4 pre-scene assert 均保留为 UNRESOLVED；成功重试不证明修复。没有绕过断言、改锁或 renderer 等待，后续诊断必须独立 opt-in/fresh build，不混入此里程碑。

## 首次运行阻断与唯一窄修

171820 候选的 q1-noop-20261012 首次运行在正常 Quality SR 成功后仍未启动 helper：q1_profile 误把 CameraMotion::RenderSize（guest 摄像机常量1920×1080）要求为实际 host SceneTargets1280×720。Root受控退出并保留 BLOCKED_BEFORE_Q1_HELPER、transport/model NOT_RUN。173246 仅删除这两个比较；C2旧gate、camera数据、actual proxy/MV/SR CI/usage/extent检查、helper/合同/期限均不变。59其余编译输入逐字节保持，exact patch和60/4 pins在新candidate-audit。没有伪填摄像机尺寸；此修订已由 gatefix noop 及后续 single/skip2/resident4 实测通过 bootstrap；原失败仍保留，不以重试成功覆盖。旧171820证据保留。CPU-only34项检查记录仍在171820目录，本次没有复跑GPU/Wine或该CPU检查。

## 已验收的短套件与失败历史

Root 的 `integration/q1-milestone.json`、`q1-gates.json` 和各 case `root-acceptance.json` 是最终验收记录。B/A 独立只读核 wire、实际 GPU/API、原源→转换→Wine guides、source/output 本地 readback、真实 return、Present 和 FD/退出；原 raw/log/模型/存档不进入源码提交。

| case | 实际 source | 结论 |
| --- | --- | --- |
| q1-noop-20261012 | 无 Q1 input | guest/host RenderSize 错误比较导致 BLOCKED_BEFORE_Q1_HELPER；已保留 |
| q1-noop-gatefix-20261012 | 11769 | 无模型初始化；copy1，原 SR97/return97 byte-exact；传输/呈现/生命周期 PASS |
| q1-single-20261012 | 8684 | Evaluate1/result1/resolve1/reset1；单帧模型调用及传输/呈现/生命周期 PASS |
| q1-skip-20261012 | 无 Q1 input | 加载期 PM4 assert/exit23，helper 未开始，负例 NOT_RUN；根因 UNRESOLVED |
| q1-skip2-20261012 | 7113 | Evaluate/resolve0，reset1 仅请求未应用，NaN raw/-1234 output 保留；INVALID 被同一 normal_model predicate 拒绝，原 SR Present/生命周期 PASS |
| q1-resident4-20261012 | 9541..9544 | 同 PID1587369/feature/device/四图；实际4 Evaluate/result1/resolve，reset1/0/0/0，4唯一 NR Present，3次此前 consumed+merge GPU末用完成后的 slot reuse；PASS |

模型案例每帧实际 raw RGB6220800 finite/poisonwritten、resolved RGBA8294400 finite/non-sentinel、source alpha2073600原bits保持；return half8294400 finite。实际 source alpha 全0，不套用 synthetic .75。两端输出逐位一致证明共享传输，不要求模型输出等输入；single RGB float32→half 的独立 NumPy舍入复算并非合同门槛，不据差异推断 GPU错误。

正常模型/skip 案例 Wine83/native136，noop Wine72/native136 的完整 readable FD baseline/final 逐项精确相等，四原 DMA-BUF payload 在 active-retired/final 消失。所有接受案例 helper normal_release 且 actual wait0；Root outer controlled stop 后 ownPG/private prefix 空。短案例相等不扩成长期 leak-free。

resident4 同 draw scheduler merge/prepare/ready ticks19088/19092/19096/19100，各 source→Present9542..9545、nonreuse/swapchain1/真实私有dump。日志关联仅 `next_nonupscaled_videoout_prepare_after_merge` 加实际 source+1/地址/tick；独立 Present scheduler tick 不比较，不声称全 guest 帧溯源。

四帧 full 诊断包含约94MB Wine readback、CPU统计/落盘和 native审计，每帧约15.6–16.2s helper snapshot时段，不是 pure NR性能。Quality matched SR-only/NR120、1800及运动/画质/非零jitter NR空间对齐尚未验收；manual exposure、MVecScale1候选与k_output不确定性保持。
