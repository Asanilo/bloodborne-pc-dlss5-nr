# 本地主线基线：DLSS SR、操作改进与 IPC 审计

2026-10-10。用户明确授权提交、合并、推送个人 fork，再清理已合并开发分支。

## 结果

主线为仓库根目录的 `master`，跟踪 `origin/master`。上游基础为 `testing / 0.5-pre4`
`10baf9df25f106da9013433a2e7348839baf917a`，在其上增加统一功能提交。
基线标签：`dlss-sr-input-ipc-baseline-20261010`；用 `git rev-parse` 查看对应提交。

原 `dlss-input-integration` 的本地功能与修复、`dlss-nr-audit` 的独立探针和审计文档
已归入主线。两个本地开发分支及 worktree 随后清理，不删除 upstream/testing 或远端 testing。
个人 fork 为 `Asanilo/bloodborne_pc`，上游 `deadinside28/bloodborne_pc` 只读。

## 内容与保留范围

- 保留上游原生 NGX Vulkan DLSS SR、NVIDIA 内存模型、多显示器、手柄校准与原子存档写入。
- 保留本地键鼠映射、慢走、鼠标捕获/焦点保护、Esc→Options、鼠标时间戳修复、中文、
  存档维护、诊断和原生运行包安装器。
- 纳入 `tools/nr_probe/`、NRFrameDesc v1、既有 SR 合成测试、Linux IPC no-op/pass-through
  及失败注入。不新增 SR/FG 后端，不修改 OptiScaler，不执行真正 NR。
- 审计区与集成区的 bridge 诊断改动内容一致，仅保留一份。
- 根目录旧工作成果已经逐文件核对，差异是 testing 上游升级与后续改进，没有用旧实现
  覆盖新鼠标修复或恢复“DLSS 一律回退 FSR”的旧逻辑。
- FSR-Vulkan 子模块基线保持原 SHA，构建脚本应用仓库已有 patches；不提交私有子模块 commit。

## 验证

统一候选源码重新完整构建并执行运行时测试，再运行 Python 套件。清理缓存后的第一次
Python 检查曾因缺少构建产物跳过 28 项，因此该结果不作为完整验收；随后补构建并重跑。
最终完整构建和 C 运行时测试通过；Python **115 项通过、无跳过**，独立日志检查器 2 项通过。
日志记录于主目录 `out/integration/mainline-*.log`。

独立 GPU 检查包含 no-op、16 帧 pass-through、奇数尺寸、10 个 IPC 故障注入、
DLAA/Quality 各 10 帧及缺 bridge；16 项通过，启用 Khronos core + synchronization validation。
独立探针在主目录重新配置后再验，结果在 `out/audit/checks-mainline/summary.json`。
日志检查器拒绝 fallback 或不完整调用记录；这些测试不等于 NR 或真实画质验收。

既有游戏测试和鼠标实景证据沿用 [鼠标修复记录](MOUSE_INPUT_FIX.zh-CN.md)。本次没有为了
源码归位重新部署游戏，安装目录当前/回退包和用户配置保持不变。

## 清理与依赖

共享依赖在主目录 `out/deps/`，不跟踪；构建环境入口 `out/deps/bbport-env.sh`。
所有 audit/integration/input-fix 日志及本机构建脚本迁至根目录对应 `out/` 路径。
其他保留的依赖/构建资料在 `out/retired-worktree-artifacts/`。忽略目录的内容不会被 push。

清理前有 `out/pre-mainline-merge-20261010/` 的源码归档、主仓库差异、FSR 子模块差异。
已确认 FSR 子模块差异完全来自版本化构建补丁，验证后反向应用补丁恢复干净状态；
下次 `build.sh` 会重新应用。主线旧修改暂存时产生的安全 stash 保留并记录在 `out/mainline-merge-receipt.json`，
不得盲目 pop 到新主线；它属于整合前旧版本。工作区登记仅保留归档记录，无活跃开发 worktree。

源码、依赖、构建脚本及记录归位后，再验证主线跟踪状态、远端提交 SHA 和文件路径，
不把“文件拷贝到 master 工作目录”当成已经建立可复用基线。
后续入口：[DLSS NR 开发交接](HANDOFF_DLSS_NR.zh-CN.md)。
