> M3 统一 FG 后端、DLSS 两条流水线和 SM 当前限制见 [M3 说明](FG_BACKENDS_M3.zh-CN.md)。

# NR + DLSS SR + FSR FG 集成实验

状态：**M1：NR + DLSS SR + FSR FG x2 功能性成功。三者联动可玩，
FG 已实际生成并 Present，HUD 正常，原有模糊问题已改善；画质尚未验收。**

`integration/nr-sr-fg` 的完整历史（截至 `abf1b61`）已合回主开发分支 `nrbridge`，
保留独立 Helper、310.9.16 SF Preset A/B、旧模型回退和 FSR 3.1.6 实验后端。
M1 配置为 1080p Quality、Old memory、NR Preset B、HUDless FSR 3.1 x2。
FG 仍为 Experimental，需玩家显式开启；不切换已安装的 `runtime/current`。

已知问题：快速大幅转镜时，FSR FG 生成帧存在植被空间漂移，NR 关闭时亦可复现。
急转抑制 FG 没有玩家收益，已撤销 `734d05e` 代码及参数，不作为默认或可选修复。
[植被复现记录](FG_GRASS_ARTIFACT.zh-CN.md)保留，画质尚未验收。
下一阶段方向为 NVIDIA DLSS FG，FSR FG 保留为可选后端；本轮不启动其开发。

## 独立入口

从公开源码增量构建 Native 和独立 Helper，再用用户自行准备、封存的运行时依赖启动。
构建及私有配置准备见 [开发指南](../tools/nr_dev/README.md)和
[Helper 构建](../nrbridge/README.md)。不提交 DLL/SDK/游戏/存档，不需要开发机路径。

```bash
bash scripts/dev-build.sh --test
export NRBRIDGE_HOME=/absolute/private/runtime-root
# NR + SR，FG 默认关闭
bash tools/nr_dev/play-m1.sh "$NRBRIDGE_HOME/localinputs/preset-B.json"
# 三者联动：显式开启 Experimental HUDless x2
bash tools/nr_dev/play-m1.sh "$NRBRIDGE_HOME/localinputs/preset-B.json" --fg
# SR-only；追加 --fg 可选择 SR + FG
bash tools/nr_dev/play-m1.sh "$NRBRIDGE_HOME/localinputs/preset-B.json" --sr-only
```

A/B/旧模型分别选择对应封存 JSON；不要在运行中替换 DLL。`--continue-session NAME`
只续用正常退休的私有进度，不暗中绑定任何开发机检查点。正式选档仍由完整启动器
走原有锁定、备份、私有副本和正常退休后同步。

完整启动器新增默认关闭的 **Experimental FSR FG x2 (HUDless)** 开关；NR 独立开关
继续存在。旧安装保持原配置，选择 FG 时需匹配重建的 M1 Native。
游戏内 F8 开关 NR，Delete 菜单可独立切换 Experimental FSR FG。
`--fg-validation` 可启用已有 Vulkan core/sync 检查；FSR 3.1.6 整帧 PoC 保留在底层
`play.py --daily --sr-only --experimental-fg fsr316`，不是植被修复或 HUDless 默认。
游戏关闭使用正常窗口关闭或 `runner.py stop SESSION`，等待监督进程及退休审核结束。
独占 GPU 锁、boot/HOLD 守卫、身份校验和真实 GPU 末用保护继续生效。

`out/play-integration.sh` 是被 `.gitignore` 的私人实验脚本，含机器路径及特定检查点，
不作为公开入口。M1 公开入口为 `tools/nr_dev/play-m1.sh`，无需复制它或依赖其路径。

## M1 合流后回归（2026-10-22）

合并提交 `33ee709` 保留 `abf1b61` 及全部 FG/NR 原始历史与作者；
`d0e385a` 撤销急转抑制。实际 GPU 构建为 `034a2d3`，构建源码差异为零。
后续记录和空间整理不视作另一次 GPU 验收。

- 现有 `out/` 增量构建及 Native 测试通过；302 项开发、58 项前端/打包、11 项 FG
  CPU 回归通过。没有复制依赖、创建 worktree 或制作 AppImage。
- 公开 `play-m1.sh` 在 RTX 4060 Laptop 载入原猎人梦境检查点的私有副本，
  1080p Quality / Old memory / 310.9.16 SF B / HUDless FSR 3.1，无急转抑制。
  模型请求和 readback 均为 2；内部权重不可读，不声称反查了权重文件。

| 同一会话的实际 Present 捕获 | 真实 / 生成 | SR 输入 |
|---|---:|---|
| NR + SR + FG，短转镜 | 8 / 8 | NR cache |
| 真实 F8 关闭 NR，FG 保持 | 8 / 8 | raw |
| 恢复 NR、独立关闭 FG | 16 / 0 | NR cache |
| 恢复 FG | 8 / 8 | NR cache |

四段均无额外 SR reset。完成 851 次 Evaluate/Resolve（847 captured、4 obsolete），
Feature 创建/释放各一次；rc0、无残留、无 watchdog/监督中断。
NR 审核为 `PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`，FG draw/Present 实际 GPU
完成和资源释放证明齐全。全局 receipt 正常 retired；原检查点 SHA 与安装的
current/previous 链接未变。64 帧捕获无损转 PNG 并逐字节回解核验。
这是有针对性的功能和生命周期回归，不是植被伪影修复、画质或新 validation 全净验收。
私人完整记录保留在 `out/m1-maintenance/` 和对应受管 run，未提交到公开源码。

## 分支与工作空间收敛

删除已完整合入的本地 `fg`（原 `05aecdf`）与 `integration/nr-sr-fg`（原 `abf1b61`）。
远端实查没有这两条分支；没有 reset、force push 或历史改写。
本地 `archive/m1-fsr-experiments` 标签指向 `abf1b61`，只作为实验恢复点，
包含已弃用 guard，不作为默认运行基线。FSR 3.1.6 源码及显式实验选项仍在 `nrbridge`。

FG 会话要求保留其 worktree 目录，因此只原地 detach 到 `abf1b61`，不移除目录。
原始 `fg-evidence`、根目录图证/记录/脚本/产物 SHA 和子模块既有诊断补丁保留原路径；
已拒绝的 guard 构建缓存被回收，历史 manifest 不改写，也不再声称原二进制仍可原地核验。
后续开发只使用 NR 的一份可变构建；workspace-init 不再默认重建 FG 分支/worktree。

先运行 `scripts/artifact-gc.py` dry-run，再用其删除与进程保护函数生成/核对精确清单，
回收正常退休会话的 Wine prefix、两类缓存及旧 guard 构建，共65项、约7.06 GB逻辑大小。
三个 HOLD 会话原样保留；所有存档/备份、配置、日志、截图、旧模型基线、Helper/模型、
共享依赖和稳定安装保留。清理前后1071个受保护文件 SHA 完全一致。
`du -s -B1` 工作区占用约21.89→15.80 GB（净减6.09 GB），含本轮新构建及验证数据。
文件系统 df 受 Btrfs 压缩、共享及同期写入影响，约125.03→122.51 GB，不能与目录逻辑
大小混算。未制作大型归档或公开二进制；既有许可兼容问题仍须在发布前解决。

## 接口与不变量

- 原 `!frame_generation` 门限保护的是 HDR 原生 Pre-SR 域。只有显式原生 FG 路径，且
  原有 HDR、1280×720 输入、1920×1080 输出、Quality、jitter、无待消费台账门限均满足，
  才允许 NR 与 FG 并用。其他 scaled/legacy 路径保留限制。
- 原生 FG 开关不再强制切到 LDR SR。真实 HDR Run 完成 SR/merge 后采集当前深度副本、
  motion、相机、reset 和 `BbStats::gpu_frames`；此时尚无最终 LDR 颜色。
- 在首个已识别的 UI draw 前绑定同一真实源、精确格式和尺寸的 LDR 目标并复制 HUDless。
  显示 pass 的只读 fragment image 地址必须包含该目标的 guest 地址，才允许插值。
  未命中 UI、空颜色、辅助图像或无有效场景时丢弃生成输出并重置 FG 历史。
- 生成输出携带独立 FG 编号、真实源编号和目标 VideoOut 地址。TakeOutput 一次消费，
  地址不符也作废；原生 Presenter 还须匹配原有 NR Present 台账的真实源编号。
- 插值仍在 display-copy 边界、下一次真实 OnFrameStart 前记录；生成 Present 的
  `is_game_frame=false`。它不执行 NR Evaluate、BeginSource、视觉历史推进或 jitter 推进。
- 正常关闭先冻结 NR 新任务，再由 draw/present owner 各自实际等待并释放 FG，随后完成
  NR source/cache/SR 的既有退休协议。联合验收必须同时具备 FG 与 NR 两组实际证明。

## 当前失败与 CPU 修复

首次 GPU 集成试验在尚未命中 UI 的真实场景中，让两个空颜色句柄比较相等而进入
Interpolate，随后 fallback 记录了空 VkImage barrier，进程 signal 11 / rc139。
没有启动 Helper，没有退休证明；会话结果为真实 HOLD，不能认为资源安全退休。
失败日志、构建差异和历史 HOLD 均保留；系统重启后通过新 boot 预检才继续，未清除旧守卫放行。

CPU 修复增加生产路径空图像/guide/尺寸防护、实际显示读取绑定、真实源与 VideoOut
核验，以及错误输出的一次作废。原生 HDR 的 NGX 输出 barrier 也包含 transfer-clear
访问域；重启后的两轮 validation 未再见该 transfer-clear WAW 或空 VkImage 报告。
纯 CPU contract 测试覆盖原始双空句柄、错图像、缺场景/context、FG 关闭、真实源错配。
CPU 验证不代表画质、正常退出或 GPU 运行已通过。

## 第一阶段实机结果

同一 GPU 构建 `0dff031`、RTX 4060 Laptop、驱动 615.78.08：

| 私有会话 | 成功真实/生成 Present | 结果 |
|---|---:|---|
| integration-native-smoke-r2 | 3590 / 1132 | 原生 SR-only，1132 对同源生成→真实配对 |
| integration-presetB-r2 | 7372 / 4705 | 联合 validation，620 次 Evaluate/Resolve 完成 |
| integration-presetB-playable | 9369 / 7055 | 联合热测，806 次 Evaluate/Resolve 完成 |

三轮均 `PASS_NORMAL_TEARDOWN`、rc0、无残留、无 watchdog/监督器信号，并分别具备
FG draw、FG Present 和 NR Pre-SR 的实际退休证明。热测日志轮转后保留的 3594 对配对无
顺序错误；累计数取最后成功 Present 台账，不能把保留日志样本数冒充整轮配对数。

F8 的真实 SDL/Overlay 回执确认 revision 1→2→3 与 cache/raw/cache 切换；FG 独立
关闭时 NR/SR 继续运行，恢复只重置 FG。覆盖转镜/短移、游戏菜单、全屏 resize、
1 秒 Native 暂停后首个恢复 dispatch 的 gap reset，未绕过 GPU 退休。
排除截图且逐源核验实际 NR cache_selected 的10秒窗口内，真实459、生成459（约46/92
真实/全部Present每秒）；间隔中位数19.80 ms / 11.40 ms，生成/真实比1.0。仅为 CPU
成功 Present-return 间隔，不是物理 scanout、端到端输入延迟或整轮平均 FPS。

实际生成图像已按 Present 顺序捕获，无损 PNG 往返验证后删除等价 raw 重复件；
生成像素不等于相邻真实帧。运动采样必须核对实际 pad 生效时间，不能仅以请求名判定
画面正在运动。画质仍为 Experimental，运动边缘可能重影。

validation 仍报告既有 WSI WRITE-AFTER-PRESENT、着色器接口 Location、sRGB view usage、
dynamicRenderingUnusedAttachments、部分 draw layout 问题；同类别曾在独立线出现，
不等于每个具体 hazard 均已证明与旧问题同源。不声明 validation 全净。
第一阶段新增约 1.6 GiB 私有运行目录/PNG，未打包或复制 Native 构建、SDK、模型。

## 第二阶段结果

已完成同分辨率 FG 开关、生成/真实帧、静态/运动、HUDless/whole-frame 和 SR-only
1440p 对照。静态真实帧未复现整体软化；1080p→2K线性放大已确认，whole-frame 的
运动 HUD 明显错位。保留 HUDless31 默认；完整方法、指标和限制见
[FG 模糊调查](FG_BLUR_INVESTIGATION.zh-CN.md)。

## 极窄窗口故障与修复候选

玩家会话 `play-20261022-134653` 在 niri 将窗口缩至极窄后，Presenter 创建帧图像
报 `ErrorInitializationFailed`，断言退出 rc23；无 GPU 退休证明，该 boot 进入真实
HOLD。第二次启动拒绝来自退休守卫，不是配置/存档损坏。该轮未启动 Helper。

根因是非零表面（如1×894）经16:9 fit后得到1×0；原先只检查fit前的零尺寸。
修复将正表面的fit结果限制在至少1像素且不超过表面，真正零表面保持最后有效帧尺寸；
同时保护旧帧resize时的letterbox目标，避免零区域blit，并一次原子发布宽高。

CPU回归覆盖薄/矮/零/恢复、常规1080p/1440p、无效比例和浮点转换边界，UBSan通过。
新boot会话 `thin-window-gpu-r1` 已对修复构建 `2a31955` 实测：标题及NR+FG运行中
将niri宽度设0（实际1×894），并将高度设0（实际829×1）；均捕获有效1×1帧，
恢复到829×894后真实NR缓存/SR与生成Present继续运行。正常退出rc0、无残留，
NR与FG两组实际退休证明齐全。保留日志的3758对生成/真实配对无顺序错误。
这是受控GPU回归，不是玩家人工画质/手感验收；旧HOLD证据未清除。
原GPU基线 `0dff031` 的常规尺寸联动验收仍保留，但它包含此极窄窗口问题。

## 玩家人工验收范围（部分已有反馈，约5–10分钟）

使用默认B、1080p Quality、Old memory，不加 `--fg-validation`。进入可操作场景后
按F8，等待终端出现“NR已开启”。本轮仍是私有测试存档。

1. 正常载入并走动，确认无黑屏、闪退或持续冻结。
2. Delete设置里的 `Frame generation (FSR 3)` 关闭/恢复，各观察同一静态位置、
   缓慢转镜和快速转镜。稳定游玩时FG约等于Real、Present约为两者之和，同时凭眼睛
   判断顺滑度；菜单、读档、历史重置时FG暂时归零允许。
3. 保持FG开启，用F8关闭/恢复NR，确认状态切换、画面继续、FG仍能工作。
4. 实际走跑、闪避或战斗约1–2分钟，观察输入拖滞、人物/武器/遮挡边缘、HUD文字
   与血条的重影、闪烁；记录最明显的一处问题。
5. 游戏菜单进出、普通窗口/全屏切换、正常关闭窗口；终端应正常结束，再启动应
   不被当前boot的pending/HOLD阻止。

反馈保留：启动/NR/FG/菜单退出是否正常；FG对顺滑度的改善；静态与运动的模糊分别
如何；输入延迟是否可接受；问题出现时的模式和本轮run路径。轻微局部伪影可记录为
Experimental已知问题，黑屏/冻结/崩溃或退出HOLD则本轮不能通过。

## 玩家反馈与后续存档（2026-10-22）

玩家会话 `play-20261022-141640` 正常退出：`PASS_NORMAL_TEARDOWN`、rc0、无残留、
无 watchdog/监督器中断，源私有存档通过既有 NR/FG 退休审核。玩家确认本次不再变模糊，
手感“还行”、HUD 正常；仍有植被闪烁，表现为贴图瞬移。未明确反馈的逐项开关、菜单等
检查不自动补记为人工通过；Experimental 保留此局部画质问题。

玩家提供桌面录屏（私人原始文件保留）（34.75秒，1920×1080/60fps）。
这是桌面录屏，游戏窗口另有缩放，且录屏不能完整保留约90次/秒的实际 Present 序列；
不能仅靠相邻视频帧判断跳变源于 NR、SR 或 FG，须对照实际标识的真实/生成帧。

按玩家要求，忽略于 Git 的本机入口 `out/play-integration.sh B` 默认增加
`--continue-session play-20261022-141640`，以后从本次猎人梦境进度复制新的私有存档，
不再默认从诊所 seed 开始。A/old 选择和追加显式 `--continue-session SESSION` 仍可使用。
源存档不改写、不替换全局 seed；读取时由现有 `continuation_source` 再次核验正常退休、
路径与 SHA。检查点清单保存在 `out/hunters-dream-continuation.json`；保留来源会话。


后续会话 `vegetation-hunters-dream-r1` 使用上述源存档的私有副本，确认实际在梦境工坊
门旁载入。构建仍为 `2a31955`，四态静态采样按实际 `display_bypass` 与
`cache_selected` 区分；NR on 两组确有真实缓存绑定。固定近叶 ROI 的相邻真实帧
平均绝对差为0.50–0.61/255，石墙参考区为0.03–0.08/255，未复现该处明显瞬移。
这些局部差值不能证明视频白花路段正常，也不能据此排除 NR、SR 或 FG。

有界右摇杆 rx160 的转镜请求虽被 pad 读取，但实际画面位移不足；对应样本只保留为
输入/画面证据，不计为有效运动对照。首次F8在继承 `display_bypass=true` 的存档中
启动了Helper但仍走raw SR，已识别为续档首启语义问题；随后真实F8关闭旁路才用于
NR on采样，最初两组纠正标为helper-bypass。该问题交NR线作独立最小修复。

本轮正常退出 rc0、无残留，NR source/cache/SR 与FG draw/Present退休证明齐全；
160张采样PNG均逐字节还原原BGRA后删除等价raw；GC dry-run后进一步精简重复旁路
与未证实运动样本，保留四态静态完整序列及其余首末真实帧，共76张约257MiB PNG，
完整采样哈希/状态清单保留，本轮私有目录逻辑新增约0.98GiB。未复制构建/依赖/模型，原始猎人梦境检查点保持不变。


玩家进一步明确：需要排查的是植被贴片飘到空中的空间位置错误，关闭NR时也会出现；
纹理外观变化不属于本次待修问题。视频前半反复F8会影响跨帧分析，但不能据此解释掉
此空间错位。下一步应固定NR关闭，只切FG并区分真实/生成帧。当前位置静态叶片样本
未复现视频白花道路的明显漂浮，不能据其排除持续运动/遮挡场景中的故障。

上游README明确FSR3.1 FG因与超分结合仍有伪影而隐藏，未具体确认植被漂浮症状：
https://github.com/deadinside28/bloodborne_pc#future 。AMD FSR3.1接入说明第80页也提示
不正确或缺失的运动矢量会造成重影，插帧放大相关伪影：
https://gpuopen.com/download/FidelityFX_Super_Resolution_3-1_Release-Overview_and_Integration.pdf 。
上述资料支持优先检查FG运动/遮挡输入，不足以证明本次根因。


## 续档首启修复与跨日志审核（2026-10-22）

NR线最小修复 `dc25fe3` 已独立cherry-pick为集成 `a8669b4`：真实首次F8/Enable在
发布既有场景marker之前只提交 `display_bypass=false`，保留其余配置、model staged值，
不推进reset/capture/model动作编号。实际发布顺序/权限/幂等CPU用例通过，现有out增量
构建通过，四产物SHA通过，源差异为0；未创建新构建树或AppImage。

会话 `vegetation-first-f8-r2` 从玩家梦境检查点复制，首次F8前确为bypass=true；仅一次
真实F8后false，实际SR日志出现cache_selected=1，且FG生成Present继续。随后NR关闭
进行FG开关局部采样。当前画面里的空中落叶和粒子经玩家确认正常，**本轮没有复现**
玩家指定的“FG开启、大幅转镜、草丛贴片飞到空中”；小幅转镜与门旁树叶样本不构成
该故障验收，不能记为修复或排除。

此轮进程rc0、无残留、无watchdog/监督中断，但原审核结果为HOLD。只读追查确认
FG_DRAW_RETIRED/FG_PRESENT_RETIRED/FG_EXIT_PROOF位于game.log.1，最终NR退出证明
位于game.log；原audit_session只读后者，造成跨轮转的假阳性缺证。保留原result与
当前boot的HOLD receipt，不人工清除、不据此启动新GPU会话。只读诊断与SHA保存在
out/first-f8-retirement-rotation-diagnosis.json。

后续定位固定NR关闭（可用本机入口追加 `--sr-only`）、DLSS SR/分辨率不变，只切FG；
在同一有草丛位置进行实际大幅转镜，并标记真实/生成帧。优先确认仅生成帧错位还是
真实帧已经错位，不改正常落叶粒子，也不把NR开关外观差异当作该空间故障。


上游 `vk_frame_generation.h` 另有更具体的设计注释：超分后还会绘制非HUD内容，
HUDless差分合成可能将其不经插值贴回而造成双影。当前HUDless31对HUD有效，但不能
据此假定其差分只包含HUD。后续在NR关闭、同SR域下比较FG off/HUDless31/whole-frame，
并检查草丛ROI是否进入HUDless差分；不先把整帧模式改为默认，因为其运动HUD重影已复现。
该注释只提供合成阶段的候选解释，仍未证明本次草片飞天根因。


跨轮转审核修复由NR `ca08889` 独立cherry-pick为集成 `6c4cbce`，只改Python审核/测试，
Native二进制仍为 `a8669b4`。读取严格按game.log.1→game.log原始字节顺序，保留每份
SHA/字节/状态SHA；仍要求完整capture、无error、failure_latched=false、常规文件与
大小界限，不补行、不去重。集成保留FG_EXIT_PROOF恰好一条且三项完成值均为1的要求。
新增FG CPU用例覆盖前文件FG证明+后文件NR证明、跨文件重复FG拒绝、未完成draw拒绝。
本地日志9、NR审计16、FG入口/证明4项CPU回归通过，首启真实发布CPU用例亦通过。

在集成分支对r2只读重审通过 `PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`，
原result、原retirement-audit与全局HOLD receipt前后SHA完全不变。报告为
out/first-f8-readonly-reaudit.json。保留窗口2735对生成→真实Present，无身份/顺序错误；
这些证明不代表草丛故障复现或画质通过。守卫只允许原pending会话退休，不支持把已经
hold的receipt补写为retired，因此继续GPU仍需新boot；不增加放行例外。

本轮两份私有会话合计逻辑新增约2.29GiB（含HOLD保留的生成输入与raw），未另外
复制Native/SDK/模型或打包。猎人梦境源检查点在测试后再次逐文件SHA核验未变。

## 植被复现与舍弃的急转方案

NR 关闭时，整帧 FSR 3.1 和 3.1.6 均已复现：前后真实帧天空干净，
中间生成帧有悬空草片。不能仅归因于 NR 或 HUDless。
`734d05e` 曾试验急转时只显示真实帧，r7/r8 的生命周期联动测试通过，
但玩家确认没有收益，M1 已撤销该实现与入口，不将其描述为修复。
原始实验历史和必要故障证据保留，见 [植被记录](FG_GRASS_ARTIFACT.zh-CN.md)。
