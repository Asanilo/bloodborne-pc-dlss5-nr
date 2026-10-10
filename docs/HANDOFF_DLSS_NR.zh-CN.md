# DLSS NR 开发交接：统一主线基线

更新：2026-10-10。面向没有先前对话记录的新开发会话。

## 当前基线

**从 `/home/arinp/Code/bloodborne_pc` 的本地 `master` 继续。**
本地基线包含上游 `testing / 0.5-pre4`、已部署的键鼠/中文/维护改进、鼠标修复，以及独立
DLSS SR / GPU IPC 审计工具。原 `dlss-input-integration` 和 `dlss-nr-audit` 已整合后清理。
可复用标签为 `dlss-sr-input-ipc-baseline-20261010`；用 `git log -1` 查看当前提交。上游基础为 `10baf9df25f106da9013433a2e7348839baf917a`。

用户已明确授权提交、合并并推送个人 fork。统一基线发布到 `Asanilo/bloodborne_pc` 的 `master`，
本地 master 跟踪 `origin/master`。`upstream` 为 `deadinside28/bloodborne_pc`，只读取，不推送。
上游 master 仍是旧 0.4 基线；个人 fork master 保留 testing/0.5-pre4 及本地功能。

首先阅读以下现有文档，不需要重新审计已有结论：

- [SR、既有 Wine helper、NRFrameDesc v1 与失败注入审计](DLSS_NR_AUDIT.zh-CN.md)。
- [本地主线整合与验证记录](MAINLINE_BASELINE.zh-CN.md)。
- [运行程序集成与部署记录](INTEGRATION_DEPLOY.zh-CN.md)。
- [鼠标修复与实际游戏证据](MOUSE_INPUT_FIX.zh-CN.md)。
- [功能与操作](PC_FEATURES.zh-CN.md)。

审计、部署、上游同步文档保留了分阶段历史，旧文中的 worktree 路径、未提交状态、
“尚未进入 3D 场景”和旧运行包不是当前状态；本文及主线整合记录优先。

## 已完成与边界

| 项目 | 已有结果 | 仍未完成 |
|---|---|---|
| 原生 DLSS SR | 上游 NGX Vulkan 实现；SDK/runtime 310.9.1；DLAA/Quality 各 10 帧合成测试；实际游戏 Quality 执行 2910 帧 Evaluate。 | 完整固定路线 DLAA/Quality 画质、真实帧时间和显存比较；审计中 MV、粒子、camera-cut/reset 等问题。 |
| 本地操作与维护 | 鼠标视角修复、捕获/焦点保护、改键、中文、存档维护、Lutris 入口与封面；猎人的梦境已确认鼠标转向。 | 原生游戏菜单鼠标点击不在范围；完整战斗、实体手柄与正常退出仍需游玩验收。 |
| Linux GPU IPC | 两个独立进程的 external memory/semaphore、所有权交接、no-op/pass-through、逐值读回、故障拒绝和超时验证。 | 仅 Linux OPAQUE_FD、transfer usage 和探针格式；未覆盖游戏真实格式/usage、Wine 或 D3D12。 |
| NR | 实验性 NRFrameDesc v1 与独立验证工具。 | **没有真正 NR 执行，没有 OptiScaler 接入，没有完整 Linux→Wine→NR 共享链路。** |

`tools/nr_probe/nr_frame_desc.h` 是协议权威定义，424 字节、4 个图像角色、6 个 fd、
ready/done binary semaphore、单帧在途；它是项目实验协议，不是 NVIDIA 的 NR API。
`tools/bridge_helper/bridge_helper.c` 仍是未完成的 Wine buffer 互操作原型，不是已验证的 NR 桥。
具体资源语义、风险、错误码和后续文件级修改表见审计文档，不在这里重复。

此前公开 master/testing、路线图、issues/PR 审计未发现明确 NR 规划，PR #83 是 SR。
这不证明不存在新的私有/公开工作；开始后续开发时按需要再次核对上游。

## 范围和约束

- 本次仅合并基线、验证、清理分支和更新文档，没有启动真正 NR 实现。
- 后续开发用独立 `.worktree/`，分支名不加 `omos`；不要直接改已部署游戏目录。
- 此前明确禁止修改另一个 OptiScaler 项目、禁止新增 SR/FG 后端，本次没有放宽这两项范围。
- 先证明目标平台资源共享与 no-op/pass-through，再接 NR。Linux-only 测试或 SR 成功不能当作 NR 成功。
- 保留游戏内容、配置和回退包；SDK、运行库、测试产物和游戏文件不得跟踪进 Git。
- `gpu/third_party/fsr-vulkan` 的本地差异来自 `gpu/patches/fsr-vulkan/` 的构建补丁；
  `build.sh` 会应用它们。本次验证后已反向应用补丁恢复干净子模块；下次构建后出现同样
  dirty 状态是预期结果，不是未合并功能。

## 依赖与复现

共享依赖统一在主目录 `out/deps/`，由 `.gitignore` 的 `out/` 排除：

| 路径 | 内容 |
|---|---|
| `out/deps/dlss-sdk/` | SDK 310.9.1；固定 commit `374959484e79a640feaba44c93ac8cfb0a03f5b5`。 |
| `out/deps/vulkan-validation/` | 本地解包的 Khronos validation layer。 |
| `out/deps/root/usr/` | Vulkan headers、Zycore、Zydis、miniz 等现有开发依赖。 |
| `out/deps/xbyak/` 等 | 其他现有开发依赖。 |
| `out/deps/bbport-env.sh` | source 后设置本机 SDK、头文件、编译/链接及 layer 搜索路径；不自动启用 validation。 |

SDK runtime SHA-256 仍为审计记录中的值。依赖已从审计 worktree 移出，删除旧 worktree
不会删除 SDK。SDK 自己的 `.git` 用于固定版本，保留。

独立 IPC 与既有 SR 检查，无需游戏或存档：

```bash
cd /home/arinp/Code/bloodborne_pc
source out/deps/bbport-env.sh
bash tools/nr_probe/build.sh
python3 tools/nr_probe/run_checks.py --validation \
  --bridge out/dlss-bridge/libbbport_dlss.so \
  --ngx "$DLSS_SDK_ROOT/lib/Linux_x86_64/rel" \
  --logs out/audit/checks-next-session
python3 -m unittest discover -s tools/nr_probe -p 'test_*.py'
```

完整主线构建及运行时测试：

```bash
cd /home/arinp/Code/bloodborne_pc
bash out/integration/build-env.sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

构建脚本已迁到主目录的 `out/integration/`，相对路径以主目录为根；使用 SDK、Xbyak、
本地依赖和 `BB_LTO=OFF`。独立 probe/bridge 已从主目录重新配置，不能直接沿用旧 worktree
的 CMake 绝对路径缓存。详细本轮验证见主线记录。

## 日志、备份与部署

- 历史集成日志：`out/integration/`；鼠标证据：`out/input-fix/`；独立审计：`out/audit/`。
  文档中的旧 worktree 前缀均映射到这些主目录路径。历史日志正文中的旧路径不做改写。
- 整合前源码归档：`out/pre-mainline-merge-20261010/`；更早备份：`out/handoff-20261010/`。
  未使用的 worktree 构建资料保存到 `out/retired-worktree-artifacts/`，依赖未丢弃。
- 游戏内容：`/home/arinp/bloodborne/CUSA03173`。入口 `/home/arinp/bloodborne/launch.sh --play`。
- 当前部署包：`runtime/releases/20261010T102648Z-6ad9496e`，回退包：`20261010T093252Z-9240e689`。
  本次源码合并没有重新部署，也没有改用户配置。
- 当前 `bbport.ini`：DLSS Quality、1080p、mouse_capture=1；启动器界面中文，游戏语言选择仍为 1。
  游戏简/繁中文入口及地区解锁已可用；创建角色人物预览问题未修复。
- 用户已要求删除正式存档、自动备份及隔离测试存档；原始 frame dumps 也已清理。
  `out/input-fix/before.png` / `after.png`、run.log/result.json 保留。
- `out/integration/check_candidate.py` 已改为允许正式存档不存在，以空白隔离 profile 启动。
  也可用 `tools/nr_probe/run_game_case.sh`，省略 `BB_AUDIT_SAVE_BACKUP` 从空白存档开始。
- 历史游戏测试采用看门狗，退出 142 是预期超时，不等于正常关窗退出验收。

## 下一步

1. 从本地主线创建新的独立 worktree；此时基础源码已有可引用提交，不再需要复制旧区的未提交文件。
2. 复跑 16 项共享资源/SR/故障检查。先扩展真实格式、sampled/storage usage、image view、
   资源重建及有界失败恢复，保留明确失败日志。
3. 确認拟用 NR 接口、SDK/运行库及平台支持，不从 DLSS SR SDK 推断 NR 接口。
4. 在独立程序中验证目标平台（尤其 Linux↔Wine）的句柄导入、GPU 同步和 no-op/pass-through；
   通过后再设计游戏侧资源导出与回写。默认关闭并保留安全回退。
5. 在这些门槛完成前，不声称 NR 已集成，不扩大到新的 SR/FG 实现。

## Suggested skills / 建议技能

- `handoff`：`/home/arinp/.agents/skills/handoff/SKILL.md`。后续交接更新本文并引用审计/日志；
  用户明确要求放在 `docs`，覆盖该技能默认的临时目录位置。
- `domain-modeling`：仅需要固定协议术语、资源角色或记录所有权 ADR 时使用，路径
  `/home/arinp/.agents/skills/domain-modeling/SKILL.md`。

新会话可直接说明：“读取 `docs/HANDOFF_DLSS_NR.zh-CN.md`，从本地主线创建独立 worktree，
在既有探针上推进目标平台资源共享验证；暂不修改 OptiScaler，不新增 SR/FG 后端。”
