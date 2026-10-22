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

### 真实游戏输入与资源复用

`m2-game-input-capture-r11` 从指定猎人梦境私有存档读取连续源帧 `8398–8403`：
SR 后 1920×1080 颜色、同源 1280×720 D32 深度及 RG16F 像素运动向量、相机和 jitter。
专门的 `dlssg-capture` 模式不创建 FSR context，也不生成帧；GPU 完成后才写出至多六组
文件。无 NR Helper/Evaluate；Native 游戏正常退休。该次 Native 源码差分中未收录当时
尚未跟踪的新采集头文件；本提交保存该文件，实际运行二进制 SHA 仍由原清单绑定。

`prepare_game_inputs.py` 只绑定已退休采集、连续真实编号和完整文件，不复制图像。
离屏 `m2-wine-game-inputs-r12` 实际执行六次 DLSSG Evaluate，首张 disable=true，
后五张 false，生成图有实际运动变化，正常 Release/Shutdown/device destroy。
近景遮挡物可见拖影，尚未进行可玩画质验收。

`m2-wine-game-reuse-r13` 将同一组输入改为两组交替使用的常驻 Vulkan 图像，始终保留
上一帧，等待本次真实 fence/idle 后才覆写较旧的一组。保存的三张有效生成图与六组
图像版本逐字节一致，五张有效标志相同，正常退休。此有限对照支持后续单槽 CPU 桥
PoC，不宣称所有场景的资源复用均已验证。原 r13 `GetTickCount64` 计时粒度不足、
多项为 0，不能用于性能结论；代码后续改用高精度 CPU 时钟。

Native/Present 尚未运行 DLSS FG。拟先以 CPU 数据桥接入现有 `TakeOutput` 边界：
固定前后真实编号、session/epoch/请求序号，响应被 Native 复制前不得复用槽位；
Native 自有 staging 和目标仍需实际 GPU 最后读完成。两组常驻输入 descriptor 地址
保持稳定；helper 的私有图像只在 Release/Shutdown 和后续 idle 后释放。
接口还需核对游戏 scene 原始字节与 display BGRA/sRGB encode，避免红蓝交换。

GPU 会话分别为 `m2-sm-1080q-r1` 和 `m2-wine-*-r1` 至 `r10`。
截至 `m2-wine-evaluate-streamline214-r10`，全部正常退休，没有新 HOLD。
原始日志和绑定清单位于现有 NRROOT 的 `runs/`；离屏 PoC 的零 Native reader 退休
不能替代未来共享输出的 Native GPU 最后读取证明。

后续 r11–r13 也均正常退休，没有新 HOLD。r11 逻辑大小约 168 MB（其中输入图像
约 94 MB），r12、r13 各约 67 MB；仍只复用原 Wine prefix，没有新增完整游戏环境。

### 常驻 Helper 单槽 CPU 协议

`m2-wine-stream-fixture-r14`、`m2-wine-stream-tmpfs-r15`、
`m2-wine-stream-cached-r16` 均完成六条真实游戏输入请求、五张有效输出以及实际消费
ACK、停止、Release/Shutdown/device destroy，均正常退休。三张保存的有效输出与
r12 逐字节一致。GPU input image/view/NGX descriptor 一直保留至 Release；两组交替
输入，上一真实帧不被当前请求覆写。GPU 输出在同一 Evaluate 命令流后复制至 CPU，
真实 fence/idle 完成后才原子发布响应，Native 消费 ACK 后才允许覆盖 CPU 槽。

r14 普通 Btrfs 文件的有效请求服务约 44–48 ms，r15 仅换到 tmpfs 仍约 37–40 ms。
r16 定位到 HOST_VISIBLE|COHERENT 读回内存 type 2（flags=0x6）不具备 CPU cache，
将输出读回/状态缓冲改用 HOST_CACHED type 3（flags=0xe）后，服务降为约 10–12 ms。
其中输入复制约 2.6–3.9 ms，记录命令、实际 fence 和 idle 约 5 ms，输出复制约
2.3–3.8 ms。这是六次离屏请求的 CPU 服务区间，不是纯推理 GPU 时间，也不是游戏
帧率或输入延迟。没有绕过 fence、退休或型号限制。

传输槽为单个私有 `/tmp/bb-dlssg-*` 目录，正常退出且所有 CPU 输出已消费后清理。
失败则保留槽和真实 HOLD。持久记录只保留协议终态、日志、三个对照图像；每次常驻
协议试验约新增 25 MB 证据，不新增完整环境。CPU 协议/相机/FG 工具测试当前 28 项
通过，Native 可玩路径尚在接入。

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
