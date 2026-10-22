# M2：NVIDIA DLSS FG x2 实验

工作树仍为 `.worktree/fg`，开发分支 `m2/dlss-fg`，从 M1
`nrbridge@7489493` 分出。M1 恢复标签 `archive/m1-fsr-experiments` 保留；
玩家安装、正式存档和 NR 分支未被改写。FSR 高速转镜植被错位仍是已知缺陷。

## 已验证的边界（2026-10-22）

RTX 4060 Laptop，CachyOS，NVIDIA 615.78.08。

| 路径 | 实际结果 | 尚未证明 |
|---|---|---|
| M1 NR B + SR Quality + Smooth Motion | NVIDIA present layer 实际映射；驱动报告 support=1、enabled=1；1080p；FSR 生成数为 0；正常退出 | 驱动插帧的独立 scanout 计数、画质和延迟对照 |
| Native NGX requirements | FG 11 返回 `0xbad00012`；SR requirements 成功 | 不代表所有 Linux FG 都不可能；旧探针未到 Create/Evaluate |
| Wine 公共 NGX core | Vulkan Init/parameters 成功，FG Create `0xbad0000b` | 未通过 core 的 FG 路由 |
| Wine + 系统 DLSSG 310.2.1 snippet | requirements support_bits=0；Init/Create 成功；Evaluate 持续 `0xbad00005` | 缓存 Backbuffer extent 始终为 0；没有有效输出 |
| Wine + Streamline 2.14.1 包 DLSSG snippet | 6 次实际 Evaluate、实际 GPU fence；首张 disable=true，后续 5 张 false；3 张输出像素验证为中间运动位置；正常退休 | 尚未接入 Bloodborne 输入、Native Present 或可玩 x2 |

最后一项是实际离屏插值，不能称为游戏交付。合成矩形每帧右移 32 px，三张生成帧
的红色区域重心与中点之差分别为 0.0725、0.0294、0.1104 px；与相邻真实帧均不相同，
没有残留的洋红清屏像素。条纹内部存在插值扭曲，不作画质通过结论。

GPU 会话分别为 `m2-sm-1080q-r1` 和 `m2-wine-*-r1` 至 `r10`。
截至 `m2-wine-evaluate-streamline214-r10`，全部正常退休，没有新 HOLD。
原始日志和绑定清单位于现有 NRROOT 的 `runs/`；离屏 PoC 的零 Native reader 退休
不能替代未来共享输出的 Native GPU 最后读取证明。

## Wine 离屏入口

复用已有 GE-Proton11-7、LLVM MinGW、共享 Vulkan/NGX 头文件及一份已正常退休的私有
Wine prefix。`tools/fg_dev/build_wine_probe.py --compiler /absolute/existing/mingw-clang++`
只构建 `out/wine-dlssg-probe.exe` 和很小的调用适配 DLL，并记录源码、头文件、编译器
及产物 SHA256；不复制 SDK、工具链或游戏。

系统 DLSSG DLL 依赖 CUDA。GE 普通 `x86_64-windows/nvcuda.dll` 是无导出的占位文件，
直接加载 DLSSG 因而失败 126。实验的 `out/nvcuda.dll` 仅为指向 GE 自带
`lib/wine/nvidia-libs/nvcuda/x86_64-windows/nvcuda.dll.so` 的符号链接；显式预加载该桥接
解决加载问题，不修改系统 DLL、prefix 或玩家环境。

调用适配 DLL 的文件名包含 `nvngx.dll`，与现有 NR forwarder 相同的调用模块要求。
FG 的 `Init_Ext` 参数顺序为 version、params，与 NR 私有适配器不同。
参数使用 core 的能力参数块；uint、float、64 位值通过精确回读及 canary 验证 ABI。
没有修改硬件能力、型号、MFG 限制或 NVIDIA 二进制代码。

```bash
NRBRIDGE_HOME=/absolute/existing/nr-dev \
python3 tools/fg_dev/wine_probe.py unique-session \
  --snippet --cuda-interop --evaluate-fixture \
  --model /absolute/streamline-2.14.1/nvngx_dlssg.dll \
  --local-inputs /absolute/existing/nr-dev/localinputs/nr310-B-playable.json
```

启动器持有既有 GPU 独占锁、调用原有 preflight/boot guard、绑定模型及可执行文件身份。
每次提交等待真实 fence；Release、Shutdown、实际 device idle/device destroy 后才允许
退役全局凭据。失败输出从不因统计、哈希变化或 Evaluate 成功而自动判为插值成功。
`OutputDisableInterpolation` 按 SDK 检查 GPU buffer 的首字节；x2 固定 count=1/index=1。
独立退休结果与功能结果分开记录。

## Native 集成接口约束

复用 `FrameGeneration::Scene` 的真实 source_game_frame、SR 后颜色、Depth、Motion、
HUDless 和相机数据。Wine FG 必须持有连续帧的独立 feature/history，不能沿用 NR 的
稀疏 origin job 编号或逐 job reset。生成输出只能进入既有 `TakeOutput` →
`EncodeInterpolated` → Vulkan Present，不能产生新的游戏帧编号或触发 NR Evaluate。

共享输出还需独立证明 producer 完成、Native 最后读 GPU tick 和双方资源退休。
在该边界落实前，离屏结果不作为可玩 FG 功能开放。

SM 使用 `play.py --daily --smooth-motion ...` 独立启动，与 FSR/游戏内 DLSS FG 互斥。
退出时真实等待 device idle、经 layer 销毁 swapchain、再次等待 idle，并要求唯一
`SM_EXIT_PROOF` 和既有 FG/NR owner 退休证据。不要把环境变量加在旧 FSR 启动器外层。

## 参考实现

- [NVIDIA Streamline](https://github.com/NVIDIA-RTX/Streamline) 与
  [DLSSG 指南](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)。
  Vulkan 支持不等于存在可用 Linux ELF FG 插件；本机检查的 Streamline payload 是 PE DLL。
- [OptiScaler Vulkan FG](https://github.com/optiscaler/OptiScaler/blob/master/OptiScaler/framegen/dlssg/DLSSG_Vk.cpp)：
  实际走 Win32 surface/Streamline WSI，不能直接接收另一个进程的 Native Wayland 句柄。
- [NVIDIA Smooth Motion](https://download.nvidia.com/XFree86/Linux-x86_64/580.173.02/README/nvpresent.html)。

目前磁盘新增主要是恢复的一份 Native 增量构建（约 700 MB）及一份 SM 私有运行环境
（逻辑大小约 741 MB）。后续 Wine PoC 复用该 prefix，仅保留日志和有界图像，
不重复生成 AppImage 或完整运行环境。精确增量以各阶段记录为准。
