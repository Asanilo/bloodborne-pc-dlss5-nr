# 工作区与构建系统：日常增量、Release 分离和产物回收

2026-10-22。本文记录本次实际清理、构建改造和 NR/FG 工作区安排；不改变任何 NR、FG
或游戏功能逻辑。

## 结果

- 工作区从 **21.92 GB** 降到 **6.92 GB**（`du -sb`，含 `.git`、`out/`、`dist/`、
  `.worktree/`）。实际文件系统已用从 **123.42 GB** 降到 **117.32 GB**（`df`）；
  两者差异来自 Btrfs zstd 压缩、reflink 和期间新增的构建/子模块对象。
- 普通开发只使用各 worktree 的一份可变 `out/`，不再为每次试验复制完整构建目录或
  AppImage。无改动增量构建约 4–5 秒；一次完整 GPU 构建约 100–190 秒。
- NR 与 FG 各自独立 worktree，共享 `out/deps`、`out/toolchain`、`out/cache/ccache`；
  不再复制 SDK、头文件、Vulkan validation 或运行时依赖树。
- 当前可玩安装、回退版本、正式配置/存档、唯一源码和必要证据均保留。

## 交接状态（2026-10-22）

| 线 | 当前 tip | 构建基础设施 |
|---|---|---|
| `master` | `8aae671` | `build.sh`/`packaging/appimage.sh` 支持 `BB_OUT_DIR`；`scripts/` 全套 |
| `nrbridge` | `59ed7cb` | 同一套 `scripts/` 和本文档；保留分支自己的 `build.sh`（`BB_BUILD_DIR`、在线/prepared deps） |
| `fg` | `59ed7cb` | 同 `nrbridge` |

`ca37cd6`、`03a9332`、`331aaa3`、`3fdb611` 只在 `master` 历史中。NR/FG 通过
`59ed7cb` 获得等价的脚本和文档内容，没有合并整个 `master`，也没有改动 `build.sh`、
在线依赖逻辑或功能代码。三条线共享主工作区的 `out/deps`、`out/toolchain`、
`out/cache/ccache`。

## 目录约定

| 路径 | 用途 | 处理 |
|---|---|---|
| `out/deps/` | DLSS SDK、Vulkan 头文件/validation、Xbyak 等共享依赖 | 保留，所有 worktree 通过 `out/deps` 符号链接复用 |
| `out/toolchain/` | 本地 ccache 4.11.3 和 `cc`/`c++` 包装器 | 保留，`scripts/install-toolchain.sh` 可重建 |
| `out/cache/ccache/` | 共享编译缓存 | 保留，按 `artifacts.json` 限制大小 |
| `out/gpu/`、`out/bb-probe`、`out/libatrac9.a`、`out/dlss-bridge/` | 主 worktree 的可变开发构建 | 保留，增量覆盖 |
| `out/audit/`、`out/integration/`、`out/input-fix/`、`out/pre-mainline-merge-20261010/` 等 | 历史验证日志、截图、源码归档 | 保留（小型证据） |
| `out/ingame-nr-ui-20261021/`、`out/preview-20261020/` 等 | 历史证据目录 | 仅保留日志、截图、JSON、baseline 元数据；删除测试 HOME、Wine prefix、构建副本 |
| `out/releases/<id>/` | 不可变 Release/Baseline 构建 | 按保留数量/预算自动回收 |
| `dist/` | 交付 AppImage 与校验和 | 保留当前、上一版和基线各一份 |
| `.worktree/nrbridge/` | NR 开发 worktree（分支 `nrbridge`） | 保留，独立可变 `out/` |
| `.worktree/fg/` | FG 开发 worktree（分支 `fg`，从 `nrbridge` 分出） | 新建，独立可变 `out/` |

## 日常开发

在任意 worktree 中默认使用共享脚本，不需要把脚本复制进分支：

```bash
cd /home/arinp/Code/bloodborne_pc

# 当前 worktree（在哪个 worktree 里执行就构建哪个）
bash scripts/dev-build.sh --test

# NR
bash scripts/dev-build.sh --worktree .worktree/nrbridge --test

# FG
bash scripts/dev-build.sh --worktree .worktree/fg --test

# 只清可重建构建输出，保留证据、依赖和缓存
bash scripts/dev-build.sh --worktree .worktree/nrbridge --clean
```

产物仍在各 worktree 的 `out/`：

- `out/bb-probe`
- `out/bb-gpu-capabilities`
- `out/gpu/libbbgpu.so`
- `out/libatrac9.a`
- `out/libbbport_dlss.so` 与 `out/libnvidia-ngx-dlss.so.*`
- `out/pad-test` 等（`--test`）

可用 `BB_JOBS=8` 限制并行度。默认开发构建 `BB_LTO=OFF`、`BB_PGO=off`，优先迭代速度；
Release 构建默认 `BB_LTO=ON`。

## Release / Baseline

普通开发不再生成不可变副本。需要发布或固定基线时才运行：

```bash
bash scripts/release-build.sh --worktree .worktree/fg \
  --id 20261022-fg-baseline --test
```

输出：

- `out/releases/<id>/build/`：完整构建，构建完成后文件设为只读；
- `out/releases/<id>/manifest.json`：源码 HEAD、脏状态、产物 SHA-256；
- `out/releases/<id>/build.log`；
- 有 Nix 时生成 `out/releases/<id>/Bloodborne-bbport-<id>.AppImage`。

从脏工作树构建会被拒绝；`gpu/third_party/fsr-vulkan` 的构建补丁脏状态会被忽略。
本次清理没有重新打包 AppImage，也没有执行 GPU 实机测试。

## 保留策略与自动回收

策略文件：`scripts/artifacts.json`。默认值：

| 配置 | 默认 | 含义 |
|---|---|---|
| `keep_releases` | 2 | `out/releases/` 保留最新 2 个 |
| `keep_appimages` | 3 | `dist/*.AppImage` 保留最新 3 个 |
| `budget_gb` | 8 | Release 构建与 AppImage 的总预算 |
| `min_age_hours` | 24 | 新于该时间的产物不删 |
| `ccache_max_gb` | 10 | 共享 ccache 上限 |
| `evidence_max_gb` / `evidence_max_age_days` | 2 / 180 | 仅在显式 `--include-evidence` 时生效 |

先 dry-run，再实际回收：

```bash
python3 scripts/artifact-gc.py
python3 scripts/artifact-gc.py --apply
python3 scripts/artifact-gc.py --include-evidence --apply
python3 scripts/artifact-gc.py --trim-ccache --apply
```

保护规则：不删除 `out/deps`、`out/toolchain`、`out/cache`、开发用 `out/gpu` 等；
不删除已注册 worktree、进程打开的文件、24 小时内修改的产物；不删除
`/home/arinp/bloodborne/runtime/current` 和 `previous` 指向的 release。

## 必须保留的基线

- `runtime/current`：`20261021T140840Z-nrbridge-025503b`；
  `runtime/previous`：`20261021T062311Z-nrbridge-a037043`。
- `dist/` 中 025503b、a037043、c91bf94-r4 三个 AppImage 及 `.sha256`。
- `packages/Bloodborne-bbport-x86_64.AppImage` 与
  `packages/Bloodborne-bbport-x86_64.pre-nrbridge-20261021.AppImage`。
- NR 参考 Native `c91bf94`；Helper `1eddac56`（源码 `713f8ac9`）；forwarder `9a825`。
  这些是验收基线，不用后续文档或路径整理冒充新的实机基线。

## 本次实际清理

删除的可重建内容：

- `out/ingame-nr-ui-20261021/` 的 `native/`、`package/`、`direct-package/`、
  `home/build/`、`home/lib/` 和 3 个测试 `profile/`（Wine prefix + 缓存 + 测试存档）。
- `out/preview-20261020/` 的 `freshhome-r2/{build,lib}`、`package-layout-migration/` 中
  重复的 `libbbgpu.so`。
- `out/bbsc-mouse-coexist-20261021/`、`out/launcher-walk-20261021/` 的 AppImage 副本；
  `out/launcher-integration-20261020/` 的 `player-fixture/`。
- `out/gameplay-06-*`、`out/native-mouse-candidate-20261020`、
  `out/upstream-06-native-cold-20261020`、`out/upstream-06-completion-cold-095bb87` 的
  `gpu/`、`atrac9/`、`dlss-bridge/`、测试二进制和 NGX 运行库副本。
- `out/upstream-06-completion-20261020/online-build/` 与
  `retired-integration-unique/out/` 构建产物（源码保留）。
- `out/retired-worktree-artifacts/dlss-input-integration/` 的 `gpu/` 和全部二进制。
- `.worktree/nrbridge/out/nrbridge-home-*`（3 个测试 HOME）、`nrbridge-appimage/AppDir/`、
  `tools/squashfs-root/` 和重复的 `dist/` AppImage。
- `dist/` 中已被取代的 4 个 AppImage（原始 c91bf94、r2、r3、c031a37）及校验和。
- `/home/arinp/bloodborne/runtime/releases/` 中除 current（025503b）和 previous
  （a037043）外的 4 个旧 release；`packages/` 中 3 个 `pre-*` 快照。

保留：

- `out/deps/`（1.9 GB）、`out/toolchain/`、`out/cache/ccache/`、主 worktree 开发构建。
- `out/audit/`、`out/integration/`、`out/input-fix/`、`out/pre-mainline-merge-20261010/`、
  `out/handoff-20261010/`、`out/publication-review-20261021/`、
  `out/upstream-06-integration-20261020/`。
- 历史目录中的日志、截图、JSON、baseline 的 `README/source-head/source-diff/artifacts.sha256`
  元数据；测试 run 的日志和 captures。
- `dist/` 当前 025503b、上一版 a037043、基线 c91bf94-r4 三个 AppImage 及校验和。
- `/home/arinp/bloodborne/runtime/current`、`runtime/previous`、
  `packages/Bloodborne-bbport-x86_64.AppImage`、
  `packages/Bloodborne-bbport-x86_64.pre-nrbridge-20261021.AppImage`。
- 游戏内容 `CUSA03173`、`data`、`config`、`GLCache` 和所有正式配置/存档。
- 唯一源码：Git 对象、`out/pre-mainline-merge-20261010/`、
  `out/retired-worktree-artifacts/native-mouse-candidate-5ab490f/candidate-source/`、
  `out/upstream-06-completion-20261020/retired-integration-unique/` 源码。

未做：全局 `git clean -fdx`、AppImage 重打包、GPU 实机测试、NR/FG 功能修改、
删除被 worktree/进程/构建脚本/回退机制引用的内容。

## NR / FG 工作区

```bash
bash scripts/workspace-init.sh
```

- NR：`.worktree/nrbridge`，分支 `nrbridge`；当前 tip 用
  `git -C .worktree/nrbridge log -1 --oneline` 查看。
- FG：`.worktree/fg`，分支 `fg`，从 `nrbridge` 分出；当前 tip 用
  `git -C .worktree/fg log -1 --oneline` 查看。
- 两个 worktree 的 `out/deps` 都指向主工作区 `out/deps`；子模块在各自 worktree
  独立 checkout，`build.sh` 会应用 `gpu/patches/fsr-vulkan` 补丁。
- 每个 worktree 的 `out/WORKSPACE.md`（已被忽略）记录同样的命令。
- `scripts/dev-build.sh`、`release-build.sh`、`artifact-gc.py` 等会通过
  `git worktree list` 找到主工作区，因此可以从任一 worktree 内直接运行，也可以从主
  工作区用 `--worktree` 指定目标。
- NR/FG 分支各自带一份构建脚本和本文档；没有合并 master 的 `build.sh` 改造，继续使用
  两条分支自己的 `BB_BUILD_DIR`、在线依赖和 prepared-deps 逻辑。
- FG 从当前可玩主线分出；若后续要求 FG 基于 master，可在 FG worktree 中
  `git rebase --onto master nrbridge fg`，不影响 NR worktree。

## 构建系统改动

- `build.sh`：支持 `BB_OUT_DIR`（Release 输出到独立目录）、`BB_SOURCE_DIR`、
  `BB_JOBS`、ccache 检测/包装、`CMAKE_C/CXX_COMPILER` 固定；默认行为仍是 `out/`。
- `packaging/appimage.sh`：支持 `BB_OUT_DIR`、`BB_DIST_DIR`、`BB_APPIMAGE_NAME`。
- `scripts/dev-build.sh`：统一开发入口，设置共享依赖、ccache、LTO/PGO 默认值；
  识别目标 worktree 自己的 `build.sh`（NR/FG 使用 `BB_BUILD_DIR`，master 使用 `BB_OUT_DIR`）。
- `scripts/release-build.sh`：独立、只读、带 manifest 的 Release 构建。
- `scripts/artifact-gc.py` + `scripts/artifacts.json`：默认 dry-run 的保留/预算回收。
- `scripts/install-toolchain.sh`：下载并校验静态 ccache 4.11.3 到 `out/toolchain/`。
- `scripts/workspace-init.sh`：创建/检查 NR、FG worktree，建立 `out/deps` 链接并初始化子模块。

## 验证

- 主 worktree：`bash scripts/dev-build.sh --test` 通过（pad/runtime/file-mods/sema/content）。
- FG：`bash scripts/dev-build.sh --worktree .worktree/fg --test` 通过，包含 DLC 内容测试。
- NR：`bash scripts/dev-build.sh --worktree .worktree/nrbridge --test` 通过，包含 NR 相关运行时测试。
- 无改动二次构建 4–5 秒；构建脚本语法检查通过；`artifact-gc.py` 在临时目录完成
  dry-run 和 apply 自测。
- Release 路径用 `workspace-selftest` 实测：独立输出、只读产物、manifest（16 项）生成成功，
  随后用 `artifact-gc.py --apply` 回收，确认保留/预算逻辑可用。
- 未执行 GPU 实机测试，因此不声称新的 GPU 运行验收。
