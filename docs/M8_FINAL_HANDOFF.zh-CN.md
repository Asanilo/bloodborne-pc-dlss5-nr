# M8 最终交接

2026-10-25：公共集成阶段结束。交接前远端与本地均为 `nrbridge@8029398`；
本次仅追加交接文档，不改渲染行为、不编译、不扩展实测矩阵、不启动 Agent。
后续由用户直接管理 FG 与 NR 两条线，不保留长期公共协调任务。

## 版本与独立开发线

| 范围 | 已核对状态 |
|---|---|
| M4 | `ca8b891` 是主线祖先，DLSS MFG 2X–6X 核心已合流。|
| M5 | `f300267`、统一验证 `7a11771` 是主线祖先；NR 多尺寸、档位及 NR-only 路径保留。|
| M8 | 共享 guide `5871c4e`、同步/HDR/Motion/reactive 修复及动态切档恢复 `2f0c3d1` 已在主线；Old/New 显式入口保留。|
| M9 | 动态资源代 `ded5c88`、逐代退休 `66ea3e2`、真实显示源关联 `4e2ea00/2c9cc16` 已在主线。Old 报告 `bd654b2`、New 报告 `91856fa` 分别对应 FG 的 `385768f/6b3fe19`，不重复合入。|
| FG 独立线 | 工作区 `.worktree/fg`，`m9/dlss-fg-dynamic@6b3fe192bb15c873e99cd62951afe78d66d95e87`。本次逐文件核对 Presenter、DLSSG bridge/contract/peer/layout/mailbox、Helper stream/export/probe 与主线一致。|
| NR/M10 独立线 | 工作区 `.worktree/m5-nr`，`m10/nr-quality@86b96899616419d7f913925a0030127d9ca5aafa`。分支引用和工作树均保留；实验 `000f074` 不是主线祖先，画质主体未强行合流。指纹/context/noise 默认关闭，真实画质和热性能未验收。|

三处开发工作树均仅显示原有 FSR 子模块内部改动；本次不重置它。
M10 的保护为现有本地分支与工作树，未额外推送其分支或声称已有远端备份。
M6 继续暂停；本轮没有唤醒任何独立研发 Agent。

## 当前可玩与玩家反馈

Old 默认、New 显式可选。已有代表性验收覆盖 NR→DLSS SR＋FG Off/x2，
Old/New 多尺寸 DLSS MFG 2X–6X 的代表性组合，及独立 SR-only、NR-only 入口。
DLAA/Quality/Balanced/Performance/Ultra Performance、1080p/1440p、已支持宽高比与运行时切档保留。
不是全部组合的穷举验收；具体证据以 [Old](DLSS_MFG_M9.zh-CN.md)、
[New](DLSS_MFG_M9_NEW.zh-CN.md)及[玩家入口](PLAYER_PREVIEW.zh-CN.md)为准。
NR-only＋FG handoff 尚不支持；FSR 回退保留 1080p Quality x2 的当前限制。
1440p 证据为内部 SR/FG 输出，既有最终窗口读回1920×1080，不是物理1440p扫描验收。

最新玩家反馈如下，属于实际游玩观察，尚无本轮新增定量测量或根因结论：

- New 移动后的闪烁恢复时间比 Old 短。
- SR Ultra Performance（线性3X缩放，不是 MFG 3X）真实 FPS 略高。
- FG 开关与闪烁未观察到直接关联；不能据此宣称已排除全部 FG 影响。
- F6 失效；游戏内 MFG 倍率菜单缺失，当前需启动参数选择倍率。
- FSR 多尺寸受限。

遗留：NR 闪烁仍待 NR 线定位；Old 冷启动 WSI WRITE_AFTER_PRESENT 未定根因；
SM 旧 ErrorDeviceLost 与 HOLD 证据保留；New 性能差异未建立统计显著性。
这些调查从公共集成任务移交，不在本次继续研发。
现有数据流及证据边界见 [NR→SR→MFG 审计](NR_SR_MFG_DATAFLOW.zh-CN.md)。

## 后续文件归属

| 归属 | 文件/接口边界 |
|---|---|
| FG 线 | `gpu/upscale/vk_frame_generation.*`、`gpu/upscale/dlssg_*`、`gpu/upscale/frame_generation_*`、`gpu/render/presenter.*`、`tools/fg_dev/`；负责 Helper、多输出、源帧配对、Present/节奏、FG GPU/FD退休及倍率 UI。|
| NR 线 | `gpu/render/native_async_pre_sr*`、`gpu/render/native_persistent_history*`、`gpu/render/nr_*`、`nrbridge/`、`tools/persistent_history/`；负责 NR 模型/历史/重投影/画质及捕获。M10 独立树的质量工具继续由 NR 线管理。|
| 用户决定合流的共享边界 | `gpu/upscale/vk_temporal_upscaler.*`、Scheduler、Buffer/ImageCache、memory layer、Vulkan runtime/WSI、`launcher/` 和 `tools/nr_dev/` 的 runner/play/session/save/配置基础设施。两线按具体功能修改，互相明确接口后由用户决定主线合流；不得将整个共享目录视为任一线独占。|

继续保持真实源帧/资源代身份、同一实际 timeline 的 last-use、Helper 完成与 CONSUMED/ACK、
异常资源隔离、GPU 独占锁及 HOLD 规则；不得将 host recording、模型 serial 或菜单状态当 GPU 完成。
正常退休后才归还 prefix/同步试玩存档，不清除或重写旧异常证明。

## 入口与依赖

本机两个入口无需手工拼装 Helper：

```bash
/home/arinp/Code/bloodborne_pc/out/bbport-play --memory-model old
/home/arinp/Code/bloodborne_pc/out/bbport-play --memory-model new
```

默认原生鼠标、1080p Quality、NR 路径准备好、FG 初始 Off；进入场景首次 F8 开启 NR，
Delete 菜单切换 FG。完全不运行 NR 用 `--nr off`，MFG 示例为
`--fg dlssg --multiplier 6 --sr balanced --output 2560x1440`；切倍率/后端需正常退出重启。
试玩私有存档在主线工作树 `out/player-preview/user`，研究 smoke 存档另放，正式安装与正式存档不改。

当前 Native 产物源码为 `d47d5f6`（`out/source-head.txt`）；之后仅文档提交，无需因此重编译。
当前 FG 配置复用 `.worktree/fg/out` 的已核对 `42bc6f7` Helper；它是运行依赖，不能清理。
NR 使用现有 M5 v3r2 local inputs、Helper/model/forwarder/NGX 与既有受管 Wine prefix。
模型、DLL、Wine prefix、私有配置/存档、游戏和构建产物均不上传 Git。

必要构建入口为 `bash scripts/dev-build.sh`，复用主工作区 `out/deps/bbport-env.sh`、
prepared sources、`out/toolchain`、`out/cache/ccache`，以及 C/C++、CMake/Ninja、Vulkan/SDL 和 DLSS SDK。
Helper 需已有 LLVM-MinGW、Vulkan/NGX/JSON 头、Wine CUDA 兼容库；NR 另需 Python3.11+、
SPIR-V 工具及固定依赖版本。详见 [NR Helper](../nrbridge/README.md#build)和
[集成构建说明](M4_M5_MAINLINE_INTEGRATION.zh-CN.md#构建与接口处理)。不新建 prefix 或复制 SDK。

## 供独立维护任务考虑的清理候选

以下为本次只读 `du -sh` 近似占用，非已释放容量；**未执行删除**。
需确认没有构建任务，并接受下次重新编译/缓存回填成本：

| 主工作区相对路径 | 预计可回收 |
|---|---:|
| `out/cache/ccache` | 约3.4GiB（会损失共享编译缓存） |
| `.worktree/nrbridge/out/gpu/CMakeFiles` | 约401MiB |
| `.worktree/fg/out/gpu/CMakeFiles` | 约399MiB |
| `.worktree/m5-nr/out/gpu/CMakeFiles` | 约399MiB |

三处 CMakeFiles 为可重建编译中间产物；维护时只评估这些目录，不扩大为删除 out/ 或工作树。
不列 prefix/runs、私有存档、模型、最终运行库、M10 输入/重放、GPU 捕获/异常证据为安全清理对象。
保留所有 FG/NR 独立工作树、分支及未合流提交。
