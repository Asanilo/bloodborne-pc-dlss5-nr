# Bloodborne · DLSS 5 NRBridge

<p align="center">
  <a href="README.md">English</a> · <strong>简体中文</strong> · <a href="README.ru.md">Русский</a>
</p>

![Bloodborne · DLSS 5 NRBridge · Native Linux](docs/assets/nrbridge-banner.svg)

<p align="center">
  <strong>在 Linux 上，重返亚楠。</strong><br>
  DLSS 5 NR · DLSS SR · DLSS FG/MFG 2X–6X · 原生键鼠 · 完整启动器
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
可使用 NR-only + FG。内存默认 New，Old 可选对照；NR 画质策略默认 Legacy。
Smooth Motion 暂不开放，尚未完成安全退出验收。

参见[玩家指南](docs/PLAYER_GUIDE.zh-CN.md)、[当前功能](docs/PC_FEATURES.zh-CN.md)、
[项目现状](docs/PROJECT_STATUS.zh-CN.md)与[路线图](docs/NRBRIDGE_ROADMAP.zh-CN.md)。

## 为亚楠加上 NR 与 FG

| 🟢 NR + DLSS SR | 🌙 持久视觉历史 |
| :--- | :--- |
| 独立 Helper 执行 NR 模型，结果接回原生渲染链路；游戏内可调节 NR 参数。 | 跨帧保留并更新 NR 结果，让后续 SR 帧继续使用有效的视觉历史。 |
| **🖱️ 原生键鼠体验** | **🎛️ 完整启动器** |
| 鼠标直接控制相机，菜单指针、复合键绑定、按键提示和中文设置一并保留。 | 统一管理画面、输入、Mod／补丁、联网选项与存档；支持自动备份和恢复。 |

此外，还保留 bbport 0.6 的手柄支持、宽高比与游戏内画面设置、DLC／内容挂载，
以及 **21 种游戏语言选择**；实际语言文本取决于你的游戏资产。

The Hunter’s Dream、Seamless Hunt 和可选 BBSC 入口也已集成。

### DLSS 5 Neural Rendering

- 进入场景后按 F8 开启 NR，随时切换效果，观察同一场景的画面变化。
- 搭配原生 DLSS SR 使用，或关闭超分，体验 NR-only；帧生成也可独立开关。
- 按自己的偏好选择模型预设、NR 执行分辨率和效果强度。
- 持久视觉历史跨真实帧保留和更新 NR 结果，让异步推理持续参与画面。

### NVIDIA DLSS FG / MFG 2X–6X

为改善 FSR 生成帧的位置跳变，本 Fork 接入了 DLSS FG。

在本机实际游玩对比中，更换到 DLSS FG 后性能与 FSR 3 FG 相当，未再出现上述纹理位置跳变导致的闪烁。你仍可选择 FSR FG 进行对照。

上游 0.6 版本中，FSR 3 帧生成会出现纹理、植被位置频繁跳变导致的闪烁，本分支目前也尚未修复。

- NR、SR、FG 分别选择；即使关闭 NR，也能使用帧生成。
- 保留多分辨率、SR 档位与宽高比，按屏幕和画面偏好调整。
- 匹配的 DLSS GPU 配置可在游戏内切换倍率；需要重启的选项会明确提示。
- HUD 分开显示真实帧、生成帧和应用 Present 速率，方便比较不同设置。

AMD FSR x2 保留为可选后端，并提供 FSR 3.1.6 实验实现。

<a id="getting-started"></a>

## 开始狩猎

### 准备

- Linux x86-64 与兼容的 Vulkan 驱动。
- 自有 Bloodborne 游戏文件及 **1.09 更新**。
- 使用 NR 或 NVIDIA DLSS FG 时，需要兼容的 NVIDIA GPU、对应运行时及匹配 Helper。
- NR 模型、DLL、游戏文件等资源由用户自行提供，不包含在仓库中。

源码与依赖准备见[开发指南](tools/nr_dev/README.md)，Helper 构建见 [NRBridge](nrbridge/README.md)。

### 启动

已有 AppImage 或安装目录时，打开 AppImage 或 `./launch.sh`：

1. 打开已安装的启动器，沿用游戏和存档目录。源码安装的依赖准备见开发说明。
2. 独立选择 NR、超分辨率、分辨率和帧生成。
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
F8、当前 FG 开关及受支持的 DLSS GPU 配置下的 2X–6X 倍率可即时生效。

DLAA、Quality、Balanced、Performance 支持多输出分辨率与宽高比；关闭超分并保持 NR
开启即为 NR-only。NR 画质分辨率与屏幕输出分辨率分别设置。NR 需要抖动及物体运动向量。
普通游玩默认关闭 Validation、性能 Profiling 和诊断图像抽样。

</details>

<details>
<summary><strong>已知问题与兼容性</strong></summary>

- NR 仍可能出现着色闪烁、有效历史骤降和亮尾。
- 社区谏言、血迹、幻影及 BBSC 多人仍在验证；手柄、真实 DLC 和更多区域的覆盖也在继续补充。
- Smooth Motion 当前不可用，正常退出与资源退休尚未完成验收。
- 高倍率 MFG 会增加 GPU 工作和显存占用，可能降低真实 FPS；生成帧更多不等于操作延迟更低。
- 当前 AppImage 的部分依赖要求 GLIBC 2.44，旧发行版兼容性待完善。
- 启动器暂不提供 FSR 4.1.1 DLL 转换；已有资源可在满足 GPU 要求时选择使用。

当前限制见[功能说明](docs/PC_FEATURES.zh-CN.md)，历史研发报告保留供开发者参考。

</details>

<a id="how-it-works"></a>

## NRBridge 如何接入游戏

游戏和 Vulkan 渲染继续在 Linux 原生运行。NRBridge 将独立模型推理接回游戏画面，再交给原生渲染或 DLSS SR；开启 DLSS FG 后，由另一条独立链路生成中间帧。

[![NRBridge 的 DLSS 架构：Native Linux 渲染、NR 持久历史、独立 Wine Helper 与 DLSS FG/MFG](docs/assets/nrbridge-dlss-architecture.svg)](docs/assets/nrbridge-dlss-architecture.svg)

图中区分 Native Linux 渲染与两个独立的 Wine Helper。原生 DLSS SR 继承自 bbport；NR 接入、持久视觉历史及 NVIDIA FG/MFG 联动由本 Fork 增加。点击图片可查看完整尺寸。

NR 模型采用异步更新，持久视觉历史随真实游戏帧推进。生成帧不推进 NR/SR 的真实帧历史。

当前推理路径使用 Wine Helper；**脱离 Wine 的 Native Vulkan NR 仍是独立研究方向，尚未接入玩家版本。**

<a id="docs"></a>

## 构建与文档

默认分支为 `nrbridge`：

```bash
git clone --recursive --branch nrbridge \
  https://github.com/Asanilo/bloodborne-pc-dlss5-nr.git
```

| 内容 | 文档 |
|---|---|
| 日常游玩与设置 | [玩家指南](docs/PLAYER_GUIDE.zh-CN.md) |
| 当前功能与限制 | [功能清单](docs/PC_FEATURES.zh-CN.md) |
| 项目状态与验收范围 | [项目现状](docs/PROJECT_STATUS.zh-CN.md) |
| 构建与运行依赖 | [开发指南](tools/nr_dev/README.md) |
| NR Helper 架构与构建 | [NRBridge](nrbridge/README.md) |
| AppImage 打包 | [打包说明](packaging/nrbridge/README.md) |
| 后续方向 | [路线图](docs/NRBRIDGE_ROADMAP.zh-CN.md) |

## 致谢

感谢 [bbport](https://github.com/deadinside28/bloodborne_pc)、[shadPS4](https://github.com/shadps4-emu/shadPS4) 及相关开源项目的贡献者。组件来源与许可证见 [gpu/VENDOR.txt](gpu/VENDOR.txt) 和 [LICENSE](LICENSE)。

Bloodborne 属于其权利人。本项目与 Sony Interactive Entertainment、FromSoftware、NVIDIA 或 AMD 无隶属关系。仓库不包含游戏、专有模型或 NVIDIA DLL。
