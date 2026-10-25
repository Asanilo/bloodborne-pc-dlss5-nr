# Bloodborne · DLSS 5 NRBridge

<p align="center">
  <a href="README.md">English</a> · <strong>简体中文</strong> · <a href="README.ru.md">Русский</a>
</p>

![Bloodborne · DLSS 5 NRBridge · Native Linux](docs/assets/nrbridge-banner.svg)

<p align="center">
  <strong>在 Linux 上，重返亚楠。</strong><br>
  DLSS 5 NR + DLSS SR · 原生鼠标视角 · 持久视觉历史 · 完整启动器
</p>

<p align="center">
  <a href="#features">功能亮点</a> · <a href="#getting-started">开始使用</a> ·
  <a href="#how-it-works">NRBridge 原理</a> · <a href="#docs">构建与文档</a>
</p>

**NRBridge 将 DLSS 5 NR 接入了 Bloodborne 的 Linux 原生移植版。**
在上游 [bbport 0.6](https://github.com/deadinside28/bloodborne_pc) 的基础上，
本 Fork 把独立 NR 推理与原生 DLSS SR 串联起来，同时带来原生鼠标、复合键、中文设置和存档管理，
让你从完整启动器直接进入游戏。

![Bloodborne 在 Linux 上运行 NRBridge 的实机画面](docs/assets/nrbridge-gameplay.png)

<p align="center"><sub>NRBridge 实机画面 · NR 已开启，右下角为游戏内状态显示。</sub></p>

<a id="features"></a>

**NR、超分辨率与帧生成已统一到完整启动器，并可独立控制。**
支持 AMD FSR x2、NVIDIA DLSS FG/MFG 2X–6X、多分辨率、多 SR 档位及 Old/New 内存。
可使用 NR-only + FG。内存默认 New，NR 画质策略默认 Legacy。
Smooth Motion 暂不开放，尚未完成安全退出验收。
本机安装与开发工作树分离，更新保留上一版、设置和存档；目前不公开发布二进制。
参见[玩家指南](docs/PLAYER_GUIDE.zh-CN.md)与[当前功能](docs/PC_FEATURES.zh-CN.md)。

## 为亚楠加上 NR

| 🟢 NR + DLSS SR | 🌙 持久视觉历史 |
| :--- | :--- |
| 独立 Helper 执行 NR 模型，结果接回原生渲染链路；游戏内可调节 NR 参数。 | 跨帧保留并更新 NR 结果，让后续 SR 帧继续使用有效的视觉历史。 |
| **🖱️ 原生键鼠体验** | **🎛️ 完整启动器** |
| 鼠标直接控制相机，菜单指针、复合键绑定、按键提示和中文设置一并保留。 | 统一管理画面、输入、Mod／补丁、联网选项与存档；支持自动备份和恢复。 |

此外，还保留 bbport 0.6 的手柄支持、宽高比与游戏内画面设置、DLC／内容挂载，
以及 **21 种游戏语言选择**；实际语言文本取决于你的游戏资产。
The Hunter’s Dream、Seamless Hunt 和可选 BBSC 入口也已集成，联网内容正在进一步验证。

**已经跑通：** NR 实机运行、原生鼠标操作、场景／标题页恢复、存档写入与重新载入、正常退出。
玩家已确认原生鼠标转镜更跟手，停鼠、F1 释放和菜单恢复正常。

<a id="getting-started"></a>

## 开始狩猎

### 准备

- **Linux x86-64** 与兼容的 NVIDIA 显卡、Vulkan／NGX 驱动环境。
- 自己的 **Bloodborne 1.09** 游戏文件。
- 本机 Wine／GE、NR 模型，以及匹配的 Helper 和 forwarder；详见[依赖与构建说明](tools/nr_dev/README.md)。

### 启动

已有 AppImage 或安装目录时，打开 AppImage 或 `./launch.sh`：

1. 打开已安装的启动器，沿用游戏和存档目录。源码安装的依赖准备见开发说明。
2. 独立选择 NR、超分辨率、分辨率和帧生成；可从 1080p 清晰度优先开始。
3. 启动游戏，进入可操作场景后按 **F8**，直接启用 NR，无需切回启动器或再次确认。
4. 结束游戏后，等待进度保存完成，再关闭启动器。

<details>
<summary><strong>快捷键与画面配置</strong></summary>

| 按键 | 功能 |
| :--- | :--- |
| F1 | 释放鼠标；点击游戏画面重新捕获 |
| F6 | 切换离线自由相机，无需启用 BBSC 或联机 |
| F8 | 首次按下启用 NR；之后切换 NR 效果 |
| Delete | 打开 NR 和画面设置 |
| Insert／L3+R3 | 打开 bbport 设置浮层 |

状态栏区分真实帧、生成帧与应用 Present 数量；Present 不等于屏幕实际显示帧数。
F8、当前 FG 开关及 DLSS GPU 模式下 2X–6X 倍率可即时生效；只有确实需要重启的选项
才显示提示，例如更换 FG 算法的运行时。NR 开关不会被 FG 切换改变。

DLAA、Quality、Balanced、Performance 支持多输出分辨率与宽高比；关闭超分并保持 NR
开启即为 NR-only。NR 画质分辨率与屏幕输出分辨率分别设置。NR 需要抖动及物体运动向量。
普通游玩默认关闭 Validation、性能 Profiling 和诊断图像抽样。

</details>

<details>
<summary><strong>已知问题与兼容性</strong></summary>

- NR 仍可能出现着色闪烁、有效历史骤降和亮尾。
- 社区谏言、血迹、幻影及 BBSC 多人仍在验证；手柄、真实 DLC 和更多区域的覆盖也在继续补充。
- 当前 AppImage 的部分依赖要求 GLIBC 2.44，旧发行版兼容性待完善。
- FSR 4.1.1 DLL 转换暂不可用，已准备好的资源仍可选择。

当前限制见[功能说明](docs/PC_FEATURES.zh-CN.md)，历史研发报告保留供开发者参考。

</details>

<a id="how-it-works"></a>

## NRBridge 如何接入游戏

bbport 让 Bloodborne 的 x86-64 游戏代码直接在 Linux CPU 上运行，并将图形转换为 Vulkan。
NRBridge 在这个基础上分工处理渲染与模型推理：

```text
游戏画面与运动信息 → 独立 NR Helper → 持久视觉历史 → 原生 DLSS SR → 屏幕
```

### 实验性 Native Linux Vulkan NR Bridge

NRBridge 以 Bloodborne 为实际案例，展示 **DLSS 5 Neural Rendering 如何接入原生
Linux Vulkan 游戏**。游戏侧提供颜色、深度、运动向量和持久时域历史。

```text
原生 Linux Vulkan → GPU 资源共享 → Wine 中的 NVIDIA NGX NR Helper
→ 持久视觉历史 → 原生 DLSS Super Resolution → 显示
```

游戏和渲染接入在 Linux 原生运行；神经推理目前通过 Wine 使用 NVIDIA 的 Windows
NGX 运行时，专有 NR 模型并非原生 Linux 实现。

独立 Helper 的源码已从冻结的 OptiScaler-Linux-NR 快照迁入 [`nrbridge/`](nrbridge/README.md)，
仍独立编译、跨进程运行。它的源码、版本化协议、GPU 同步和时域接入可供研究 Linux
Neural Rendering 互操作的开发者参考。游戏与模型文件由用户自行提供。

参见 [架构与 Helper 构建](nrbridge/README.md)、[Native 构建](tools/nr_dev/README.md)、
[迁移和实机验证](docs/NRBRIDGE_MIGRATION.md)、[许可边界](nrbridge/LICENSE-NOTES.md)。

<a id="docs"></a>

## 构建与文档

仓库默认分支为 **`nrbridge`**：

```bash
git clone --recursive --branch nrbridge \
  https://github.com/Asanilo/bloodborne-pc-dlss5-nr.git bbport-nrbridge
cd bbport-nrbridge
```

| 想了解什么 | 入口 |
| :--- | :--- |
| 准备依赖、构建 Native 与 Helper | [开发指南](tools/nr_dev/README.md) |
| 制作 AppImage | [打包说明](packaging/nrbridge/README.md) |
| 改键、中文与存档 | [玩家使用说明](docs/PC_FEATURES.zh-CN.md) |
| 当前进展与已知问题 | [当前功能](docs/PC_FEATURES.zh-CN.md) |
| 工作空间和分支布局 | [仓库维护说明](docs/WORKSPACE.md) |
| bbport 0.6 的更新 | [上游更新记录](docs/CHANGES_0.6.md) |

如果你也想在 Linux 上逛亚楠，欢迎点个 **⭐ Star** 关注进展。
实机反馈、问题报告、翻译和代码贡献都欢迎。

## 致谢

感谢 [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) 提供 bbport，
感谢 [shadPS4](https://github.com/shadps4-emu/shadPS4) 提供渲染器与着色器翻译器的起点。
完整出处见 [组件致谢](gpu/VENDOR.txt) 和各组件许可证。NRBridge 相关问题请反馈到本 Fork。

Bloodborne 属于相应权利人。本项目与 Sony Interactive Entertainment、FromSoftware、
NVIDIA 或 AMD 无关联。许可证见 [LICENSE](LICENSE)。
