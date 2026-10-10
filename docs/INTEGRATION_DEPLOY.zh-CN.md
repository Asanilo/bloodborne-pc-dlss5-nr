# 原生 DLSS 与本地操作改进：合并、验证和部署

最新工作区、依赖及清理状态见 [NR 开发交接](HANDOFF_DLSS_NR.zh-CN.md)。
本文记录先前的工作目录整合与部署；随后本地主线合并和分支清理见
[主线基线记录](MAINLINE_BASELINE.zh-CN.md)，其中状态优先于下方历史记录。

2026-10-10 后续更新：鼠标事件时钟比较导致视角不动的问题已修复并部署。
当前包为 `20261010T102648Z-6ad9496e`，下文为首轮合并记录；
后续实机场景与回归结果见 [鼠标修复记录](MOUSE_INPUT_FIX.zh-CN.md)。

2026-10-10，独立工作区 `.worktree/dlss-input-integration`，分支
`dlss-input-integration`。基于 `upstream/testing` / `0.5-pre4`，提交
`10baf9df25f106da9013433a2e7348839baf917a`。已获取上游，没有更新的提交。
本地修改均未提交、未推送。仓库根目录的稳定工作区及 DLSS 审计工作区未修改。

## 已部署

- 当前版本：`/home/arinp/bloodborne/runtime/releases/20261010T093252Z-9240e689`。
- 回退版本：`/home/arinp/bloodborne/runtime/releases/20261010T060739Z-22dc72b9`，
  由 `runtime/previous` 指向。
- `/home/arinp/bloodborne/launch.sh` 和 Lutris 的 `Bloodborne PC` 入口保持原样，
  现在通过 `runtime/current` 使用新版本。
- 原生 DLSS SR 与本地鼠标视角、鼠标按钮绑定、焦点释放、慢走、中文、存档维护已合入
  同一完整运行程序。保留上游手柄校准、多显示器、NVIDIA 内存模型与原子存档写入。
- NVIDIA SDK/runtime 310.9.1，bridge 与 NGX 库放在 `bin/bb-probe` 旁，许可证放在
  版本根目录。`BUILD-MANIFEST.json` 记录基线、未提交修改状态及二进制 SHA-256。
- 配置、启动脚本和实际存档共 17 个文件在部署前后校验一致。当前配置仍是 **FSR 3**，
  没有擅自切换算法。启动器选择 **DLSS (NVIDIA RTX)**，预设选 **Quality** 或
  **Native AA（DLAA）** 后启动；缺少运行库或硬件不支持时由上游能力检测回退。

## 合并中的关键修正

| 文件 | 处理 |
|---|---|
| `gpu/shim/bbport_settings.cpp`、`tests/test_settings.cpp` | 移除本地旧版对 DLSS 的无条件回退；保留 XeSS 回退，验证 DLSS 保存和真实能力失败回退。 |
| `src/runtime_pad.c`、`gpu/shim/window.cpp`、`host_input.h` | 合并键鼠快照与上游摇杆校准；保留失焦和菜单输入屏蔽、多显示器选择。 |
| `launcher/bbport_i18n.py` | 本地中文条目优先，其余使用上游中文表，防止新增设置丢失翻译。 |
| `launcher/bbport_launcher.py`、`bbport_overlay.cpp` | 保留 DLSS 选项及上游设置，加入本地键鼠、中文和维护入口。 |
| `scripts/install_native.py` | 增加 DLSS 完整包检查、库/许可证复制、多二进制依赖检查、先暂存后激活，保留旧版本。 |
| `gpu/dlss_bridge/bbport_dlss_bridge.cpp` | 复用审计区已有 `BB_DLSS_AUDIT=1` 诊断，不改变 SR 算法。 |
| `gpu/CMakeLists.txt` | 保留本地 host-input 测试；修复相机测试 Vulkan-Hpp 符号冲突，仅影响测试目标。 |

## 验证结果与范围

日志均在本 worktree 的 `out/integration/`。

- 完整构建 `bash build.sh --test` 成功，含 pad、runtime、file-mods、sema、content C 测试。
  本机可复现环境：`bash out/integration/build-env.sh`；使用本地解包开发依赖，不改系统软件。
- Python：**114 项通过，无跳过**，见 `python-tests.log`。包含备份恢复、更新保留配置与
  存档、DLSS 包暂存/激活/拒绝不完整包，以及新旧中文条目共存。
- 组件：**10 项通过**，见 `component-tests/summary.json`。包括输入、设置、超分支持、
  运动历史、UI 合成、运动着色器、场景分辨率、TAA、相机运动、内存转换。
  相机测试另在 RTX 4060 上验证，见 `camera-motion-nvidia.log`。
- 候选包 GPU / DLSS：**16 项通过**，启用 Khronos core + synchronization validation，
  无 validation error，见 `package-gpu-checks/summary.json`。包括 Linux 跨进程 no-op /
  pass-through 和故障注入；DLAA、Quality 各实际执行 10 帧合成 NGX Evaluate 并读回图像。
  日志检查器使用 `--synthetic`，两种模式均通过，见 `dlaa-log-check.log`、
  `quality-log-check.log`。这不是游戏画质验收。
- 中文 GTK 启动器构建、43 项绑定和默认/旧键盘预设切换通过，见 `launcher-check.log`
  和 `../launcher-zh.png`。
- 隔离存档实机短启动 3 次：DLSS Quality 配置启动、脚本菜单操作、候选包
  `launch.sh --play` 的 FSR 3 启动。RTX 4060 上均出现 `DLSS: ready`，预热后的
  启动菜单约 60 FPS。采集画面到游戏初始亮度设置；未进入可验证的 3D 战斗场景，
  因此游戏日志只有 NGX 初始化，不能宣称已验证游戏内的 DLSS Evaluate 或运动画质。
- 短启动均使用看门狗主动退出，**退出码 142 是预期超时**，不等于正常关窗退出验收。
  原始日志与 JSON 在 `game-*/`；实际游戏存档未作为可写目标传给测试。

失败记录也保留：首次链接缺少 `-lZycore`，补充 `LIBRARY_PATH` 后成功；记录在
`build-first-link-failure.log`。相机测试首次 SIGSEGV 的堆栈位于
`camera-motion-gdb.log`，原因是测试可执行程序的 Vulkan-Hpp 内联方法抢占了渲染库
使用另一组平台扩展的实现，按已有场景/TAA测试的隐藏符号方式修复后通过。

## 使用、复测和回退

正常入口不变：

```bash
/home/arinp/bloodborne/launch.sh
/home/arinp/bloodborne/launch.sh --play
```

从本 worktree 复测已安装包（创建新的隔离存档/配置目录）：

```bash
python3 out/integration/check_candidate.py \
  /home/arinp/bloodborne/runtime/current quality --seconds 30
python3 out/integration/check_candidate.py \
  /home/arinp/bloodborne/runtime/current fsr3 --seconds 20 --launcher
```

恢复旧版本（在游戏退出后执行；保留配置和存档）：

```bash
python3 scripts/install_native.py --root /home/arinp/bloodborne \
  --activate /home/arinp/bloodborne/runtime/previous
```

真实战斗的 DLAA/Quality 图像、帧时间、完整鼠标操作与 Alt-Tab、实体手柄及正常退出
仍需游玩验证。没有修改 OptiScaler，没有增加 SR/FG 后端，没有运行或集成 NR。
