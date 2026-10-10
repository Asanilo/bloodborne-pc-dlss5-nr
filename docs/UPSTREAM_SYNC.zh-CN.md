# 工作区迁移与上游更新检查（2026-10-10）

本页是第一次工作区迁移时的历史记录。后续主线合并、依赖归位和分支清理已完成，
当前状态见 [主线基线](MAINLINE_BASELINE.zh-CN.md) 与 [开发交接](HANDOFF_DLSS_NR.zh-CN.md)。
原工作区内的审计日志现位于主目录 `out/audit/`。

审计工作区现为 `.worktree/dlss-nr-audit`，分支 `dlss-nr-audit`，跟踪
`upstream/testing`。旧 `.slim` 目录已删除，本地登记信息移到 `.worktree/worktrees.json`。
稳定区仍在仓库根目录的 `master`。没有提交、推送、合并或替换稳定区的未提交修改。

执行了 `git fetch upstream master testing --tags`：

| 工作区 | 当前基线 / 跟踪上游 | 与对应上游的提交差异 |
|---|---|---|
| 稳定区 | `daa71654083b42ea0730b3f0fecee2fc20942d02` / `upstream/master` | ahead=0、behind=0；本地工作目录仍有修改 |
| 审计 worktree | `10baf9df25f106da9013433a2e7348839baf917a` / `upstream/testing` | ahead=0、behind=0；本地审计代码仍有修改 |

上游目前没有比上次审计更新的提交，`testing` 仍等于 `0.5-pre4`。因此本次无需合并；
“提交已同步”不等于“工作目录没有本地改动”。稳定区的 0.4 本地功能也没有自动搬进审计分支。

## 必须保留的本地修改

迁移前对稳定区和审计区的 Git 已跟踪文件及未忽略新增文件保存 SHA-256 清单，移动后立即
比对：稳定区 779 个文件、审计区 806 个文件内容完全相同。之后只更新了审计区文档和构建
输出；稳定区的输入、中文、存档维护等代码保持原样。

审计分支自身相对上游的修改范围：

- `gpu/dlss_bridge/bbport_dlss_bridge.cpp` 的 `BB_DLSS_AUDIT` 可选日志。
- `tools/nr_probe/` 下的协议、共享资源探针、既有 SR bridge 测试与脚本。
- `docs/DLSS_NR_AUDIT.zh-CN.md` 及本说明。

后续更新 `testing` 时，优先查看 bridge 的 NGX 参数与生命周期是否变化，并同步 smoke 测试、
日志解析器和输入契约。不要用上游文件整份覆盖本地日志改动。

## 稳定区升级到 testing 时的重叠文件

稳定区基线至 `testing` 共变化 116 个路径，其中 11 个与稳定区的已修改文件重叠；没有
“本地新增文件与上游新增路径同名”的冲突。重叠路径不是已经发生的文本冲突，是升级时需要
逐项合并和验证的范围，原始清单在 `out/audit/upstream-overlap.json`。

| 文件 | 本地能力 | 上游更新 / 合并要求 |
|---|---|---|
| `README.md` | 本地功能中文说明入口 | 保留 0.5 DLSS、NVIDIA 内存模型、构建与路线图说明；不能继续宣称 DLSS 尚未实现。 |
| `gpu/CMakeLists.txt` | host-input 测试目标 | 合并上游 layer 模块、shader 和 translation-memory 测试目标，保留输入测试。 |
| `gpu/shim/bbport_settings.cpp` | 中文、mouse_capture；旧版 DLSS/XeSS 回退逻辑 | **最高关注：旧本地代码把 dlss 强制回退 FSR3，与上游已实现的 DLSS 冲突。** 升级时应保留上游 DLSS 能力检测，只保留尚未接入算法的回退。 |
| `gpu/shim/bbport_settings.h` | 中文枚举、鼠标捕获选项 | 保留上游 `UpscalerDlss`、supported/problem 状态与 API；注意枚举和持久化格式。 |
| `gpu/shim/bbport_overlay.cpp` | 中文字体/翻译、捕获开关、焦点处理 | 合入上游 DLSS 选项和窗口布局；既有回退说明需更新，不能遮蔽实际算法。 |
| `gpu/shim/window.cpp` | 事件线程输入快照、相对鼠标、失焦释放 | 保留上游 `BB_DISPLAY` 显示器选择，再保留本地焦点、文本输入和捕获状态机。 |
| `launcher/bbport_i18n.py` | 独立 `bbport_zh.py` 中文翻译 | 上游现在也内置中文表；逐键合并新增本地设置，避免两套来源覆盖或漏翻译。 |
| `launcher/bbport_launcher.py` | 键鼠绑定、备份恢复、目录/管理器入口 | 保留上游 DLSS、多显示器、日志导出、跳过联网选择、NVIDIA 新模型选项及 UTF-8 读写。 |
| `run.sh` | 存档 supervisor、会话锁、备份、退出后恢复、诊断 | 与上游 BB_GPU_USER_DIR、驱动缓存及 MangoHud 路径处理合并；确认重启仍归原 supervisor 管理，不重复备份或占锁。 |
| `src/runtime_pad.c` | 鼠标视角、慢走、可绑定鼠标、焦点屏蔽 | 上游新增偏置摇杆校准、行程缩放和内外死区；手柄路径保留校准，键鼠路径继续使用事件快照。 |
| `tools/gpu_capabilities.c` | 鼠标录入、`--diagnose` | 合并上游显示器枚举等入口，避免互相替换命令分派。 |

不重叠但行为相关的文件也不能漏掉：

- `gpu/bbgpu.h`、`gpu/shim/host_input.h`、`sdl_window.h`、`tests/test_pad.c` /
  `test_host_input.cpp`：必须与合并后的输入路径一起验证。
- `tests/test_settings.cpp`：旧版“DLSS 一定回退”的预期不能搬到已有原生 DLSS 的分支。
- `scripts/save_manager.py` 与上游 `src/runtime_savedata.c` 的原子写入各有作用，前者负责
  会话/快照/恢复，后者保护单次存档文件写入，不能把一个当成另一个的替代。
- `scripts/install_native.py`：若以后打包上游 DLSS，bridge 和 NVIDIA runtime 必须位于
  `bb-probe` 同目录；不能简单放到现有 `bin/gpu` 额外依赖目录后就认为自动发现可用。
- `gpu/third_party/fsr-vulkan` 当前有构建脚本应用的本地 patch（109 行）；本次未清理它。
  将来更新子模块需要核对 `build.sh` 与 patch 集，不可直接 reset/clean。

## 迁移后的验证与更新方式

CMake 缓存包含旧绝对路径，不能只移动目录就认为仍可重建。本次将旧 build 目录保存在
`out/before-worktree-move/`，从新路径重建了 `out/nr-probe` 和 `out/dlss-bridge`；SDK、测试数据
和历史日志随 worktree 保留。历史日志中的旧路径是当时运行记录，不做全局替换。

迁移后重新执行 16 个 GPU / DLSS / 故障注入检查，启用 Khronos core 与 synchronization
validation；汇总在 `out/audit/checks-after-move/summary.json`，逐项结果在
`out/audit/check-results-after-move.txt`。验证范围仍是 Linux↔Linux 共享及 DLSS 合成 SR，
不代表 NR、Wine 或 OptiScaler 已接入。

下一次上游有新提交时，先在审计 worktree 查看 `git status`，获取上游，再比较旧基线到
`upstream/testing` 的变更及以上本地文件。保留未提交补丁和新增文件后，在隔离目录合并；
重跑共享资源探针、DLAA/Quality、失败日志检查。稳定区的 0.4→0.5 功能迁移须按表逐项处理，
不把审计 worktree 的更新直接部署到现有游戏。当前没有设置定时自动拉取或自动合并。
