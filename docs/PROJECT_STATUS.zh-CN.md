# Bloodborne · NRBridge 项目现状

> 更新：2026-10-26 · 主线 `nrbridge` @ `c937e07` · 只描述当前源码与已实测状态。
> 规划与未完成方向见 [NRBridge 路线图](NRBRIDGE_ROADMAP.zh-CN.md)。

## 1. 当前正式版本

| 项目 | 事实 |
|---|---|
| 源码主线 | `bloodborne-pc-dlss5-nr` 的 `nrbridge` 分支（仓库默认分支） |
| 正式运行版本 | `/home/arinp/bloodborne/runtime/current` → `releases/20261025T140754Z-player-251aff7f` |
| 回退版本 | `runtime/previous` → `releases/20261021T140840Z-nrbridge-025503b` |
| 构建身份 | Native `251aff7f`（NR/GPU）、Player `c3318730`（FG/播放器）、主线基线 `af01e33` |
| 阶段标记 | `VALIDATED_LOCAL_PLAY` — 本机可玩验收，**不是公开发布** |

运行版本是解包的 AppDir，与源码树互相独立：改源码不会自动改变已安装版本，重新打包会产生新身份并需要新的验收。

## 2. 工作空间：源码、开发数据、玩家安装

三者分离，任何一方都不应引用另外两方的私有路径：

```text
/home/arinp/bloodborne/            玩家安装 + 正式游戏（唯一日常入口 launch.sh）
  CUSA03173/                       游戏本体
  runtime/releases/…               已安装运行版本（current / previous 指向此处）
  data/{saves,nrbridge}/           存档与运行时私有状态
  config/bbport-launcher/          启动器设置

/home/arinp/Code/bloodborne_pc/    研发源码（多 worktree）
  .worktree/nrbridge/              当前主线开发检出
  .worktree/fg/  .worktree/m5-nr/  FG / NR 独立线检出（待退役，见第 6 节）
  out/  dist/                      被忽略的构建与本地打包产物

/home/arinp/Code/nr-home/          开发数据根（NR_DEV_ROOT / NRBRIDGE_HOME）
  inputs/source-deps/              已打补丁的第三方源码与校验清单
  inputs/helper-f527/              Helper 固定闭包（头文件与规格）
  localinputs/  seed/              私有运行配置、存档 seed
  build/{baselines,convergence,helper-runtime-model}/
```

**关系**：源码 → 编译 Native 与 Helper → 打包 AppImage → 解包进 `runtime/releases/` → 玩家入口启动。玩家安装自包含，不加载研发目录里的任何文件。

**已退役**：`/home/arinp/Code/nrbridge-dev` 已于 2026-10-26 整体删除（回收约 17.7 GiB 独占空间）。其中仍被引用的输入、私有配置、seed 与 Helper/forwarder 构建产物已迁到 `nr-home`；`build.sh`、`runner.py`、`prepare_helper.py`、`prepare_runtime_inputs.py`、打包脚本与本机路径头文件的所有默认值改为从 `$HOME` 推导，不再硬编码个人目录。

## 3. 已有功能

状态口径：**已实测**＝有玩家或实机 GPU 证据；**可用未验收**＝编译并在 CPU/入口层通过，实机未覆盖；**实验**＝需显式开启且默认关闭。

| 功能 | 状态 | 默认 |
|---|---|---|
| NR（DLSS 5 Neural Rendering） | 已实测：实机运行、持久视觉历史、正常退出与资源退休 | 开启可选，F8 控制效果 |
| NR 预设 / 画质策略 | 已实测：Preset A／B／模型默认；Legacy 视觉过渡 | 预设 B；过渡 Legacy |
| SR（超分） | 已实测：DLSS、FSR 3.1.6、DLAA 及既有实现；DLAA→Ultra Performance 档位 | 依玩家选择 |
| FG（帧生成） | 已实测：AMD FSR x2、NVIDIA DLSS 2X–6X；互斥；倍率可运行时切换 | 关闭 |
| NR-only + FG | 已实测：`--sr native` 关闭超分后 NR 与 FSR／DLSS FG 联动 | 否 |
| 动态尺寸 | 已实测：1080p／1440p／16:10／超宽，按真实 Color/Depth/Motion/HUDless 创建资源 | 依输出设置 |
| Old / New 内存模型 | Old 已实测为公共默认；New 显式可选（`--memory-model new`） | `auto` → 解析为 **Old** |
| 原生鼠标 | 已实测：直接控制相机，停鼠、F1 释放、菜单恢复正常 | 关闭（可选开启） |
| 菜单指针 / 输入捕获 | 已实测：菜单内鼠标指针与 F1／点击捕获 | 匹配站点后开启 |
| 复合键 / 滚轮 / 提示图标 | 可用未验收：CPU 输入与录入回归通过 | 启用手柄/键盘提示 |
| 手柄 / 触点 / 体感 | 可用未验收：SDL 热插拔与校准已编译，真实设备混合输入待验 | 自动检测 |
| 完整启动器 | 已实测：画面、输入、Mods、补丁、联网、存档、日志；GTK4 | 默认入口 |
| 中文／21 种语言 | 已实测：启动器语言与游戏内简繁选项；文本依赖自有资产 | 跟随系统 |
| 存档保护 | 已实测：启动备份、独立副本、正常退出后同步、恢复入口 | 开启 |
| DLC / 内容挂载 | 可用未验收：挂载与回滚源码通过，真实 DLC 未挂载验收 | 依资产 |
| 联网 `bbnet` / `bbnet20` / THD / BBSC | 可用未验收：可编译，默认不启动会话，未做真实联机验收 | 关闭 |
| HUD 计数 | 已实测：真实帧／生成帧／应用 Present 分开统计 | 开启 |

## 4. 已知限制

**画质**

- NR 仍可能出现着色闪烁、有效历史骤降与亮尾（未解决，证据见第 5 节）。
- NR 可有轻微拖影或局部外观变化；本机版本保持 Legacy，不默认启用新 Fade 策略。
- FSR FG 快速转镜时生成帧可能出现植被位置错误；DLSS FG 的已有玩家测试未再复现。

**性能与显存**

- NR 执行分辨率与高倍率 MFG 增加显存和 GPU 工作；Present 数不等于屏幕帧率，也不等于输入延迟。
- 高倍率 MFG 不改善真实游戏帧率；倍率越高生成工作越多。
- Old/New 无普适结论：New 不一定更快，且可能占用更多显存。

**覆盖范围**

- 1440p 证据是内部 SR/FG 输出，最终窗口读回仍为 1920×1080，**不是物理 1440p 扫描验收**。
- 未验收：New 的 16:10／超宽／4K／HDR，以及全部倍率×档位组合。
- DLSS 3X–6X 只对已识别运行时做内存内适配，不修改磁盘上的 NVIDIA DLL。
- FSR 4.1.1 DLL 转换不在启动器内进行（独立 GPU 工作流），已准备资源仍可选择。

**平台与分发**

- 当前 AppImage 部分依赖要求 GLIBC 2.44；面向 CachyOS，不宣称适配所有发行版。
- 本轮不公开发布二进制；社区谏言、血迹、幻影与真实联机仍在验证。

## 5. 未解决故障与证据位置

| 问题 | 现状 | 证据 |
|---|---|---|
| NR 着色闪烁 / 有效历史骤降 / 亮尾 | 未解决，主线保留现场 | `out/nr-dev/runs/` 中 2026-10-25 与已标记失败轮次 |
| NR 时域画质（分数运动、深度边缘） | 实验策略 `fade-new`，默认关闭 | [NR 时域稳定性](NR_TEMPORAL_STABILITY.zh-CN.md) |
| FSR FG 植被错位 | 未解决，未承诺修复 | [FG 通用化](FG_GENERALIZATION.zh-CN.md) |
| Smooth Motion | 不可用（安全退出未验收） | 同上，及第 2 节路线图 |
| 真实联机 / DLC / 手柄混合输入 | 未验收 | [bbport 0.6 功能状态](BBPORT_06_FUNCTION_STATUS.md) |

故障证据按轮次保存在被忽略的 `out/` 下，不属于运行依赖；清理时不要以"曾用于验收"为由整体删除。

## 6. 分支与退役状态

| 分支 | 状态 | 说明 |
|---|---|---|
| `nrbridge` | **主力**，默认分支 | 当前全部合流工作的主线 |
| `master` | 保留 | Fork 旧主线，未自动合入 NRBridge，历史对照用 |
| `m9/dlss-fg-dynamic` | **待退役** | FG 独立线，内容已合入主线；工作树 `.worktree/fg` 暂留 |
| `nr/temporal-stability` | **待退役** | NR 时域独立线，实验部分未合流；工作树 `.worktree/m5-nr` 暂留 |
| `m10/nr-quality` | 保留（仅本地） | 未推送的画质实验，避免丢失 |
| `m2/dlss-fg`、`m4/dlss-mfg`、`m5/nr-dimensions`、`testing` | **已删除**（2026-10-26） | 均已是 `nrbridge` 祖先，本地与远端引用一并清理 |

退役顺序建议：先确认工作树无独有提交与脏文件 → 归档必要证据 → 删除工作树与分支引用 → 更新本文与 [工作空间说明](WORKSPACE.md)。

## 7. 文档地图

**当前事实（以此为准）**：本文、[玩家指南](PLAYER_GUIDE.zh-CN.md)、[当前功能](PC_FEATURES.zh-CN.md)、[路线图](NRBRIDGE_ROADMAP.zh-CN.md)、[工作空间说明](WORKSPACE.md)、[Native 构建](../tools/nr_dev/README.md)。

**阶段性验收报告**（保留为历史，数字与结论只对当时身份有效）：`M8_FINAL_HANDOFF.zh-CN.md`、`DLSS_MFG_M9*.zh-CN.md`、`FG_GENERALIZATION.zh-CN.md`、`NR_TEMPORAL_STABILITY.zh-CN.md`、`BBPORT_06_FUNCTION_STATUS.md`、`M8_NATIVE_NR_INTEGRATION.zh-CN.md`。

**上游与本机历史**（不代表当前版本）：`CHANGES_*.md`、`ROADMAP.md`（上游俄文路线图）、`*.ru.md` 等。
