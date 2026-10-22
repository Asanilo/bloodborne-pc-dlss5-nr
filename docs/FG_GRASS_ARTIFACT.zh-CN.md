# 急转时生成帧中的草片错位

## 已复现的范围（2026-10-22）

在猎人梦境草路位置、1080p Quality、NR 关闭的受控转镜中，
`fg-grass-wholeframe-r4` 的整帧 FSR 3.1 生成帧出现了悬空草片：
`wholeframe-grass-fast-turn` 的 f010_real 与 f012_real 天空干净，
中间 f011_interp 的天空有孤立的大块植被贴片。ROI 为 (350,0,600,360)。
本机图证为 `out/fg-grass-generated-only-repro.png`，对应记录为
`out/fg-grass-r4-reproduction.json`。这与正常掉落叶片、F8 切换造成的材质外观
变化不同，也证明问题不只存在于 HUDless 合成。

玩家随后补充：原视频约 2 秒后、离开 OBS 画面时即为指定场景；下一轮由玩家先
手动挪到该位置再采样。当前草路图证是独立复现，不声称已精确匹配原视频机位。

现成 FSR 3.1.6 整帧路径的 `fg-grass-fsr316-r6` 同样出现该空间错误：
`fsr316-grass-fast-turn` 的 f009_real / f010_interp / f011_real，
ROI 为 (0,0,600,360)。因此不将切换 SDK 当作修复，也不改默认 HUDless31。
两次路径的转镜轨迹未严格同步，不据此作整体画质或性能排名。

这些会话均正常退出，实际 FG draw/present 退休完成，SR-only 的 NR 无 peer
证明通过。较早门口、墓碑附近的落叶样本没有复现此错位，不作为反证或修复证据。

## 可选急转保护

`--fg-fast-turn-guard` 默认关闭，需支持该能力的新 Native 构建；旧构建会拒绝
该选项，避免悄悄忽略。实验入口：

```bash
bash out/play-integration.sh B --fg-fast-turn-guard
```

每个真实场景比较相机前向向量；超过 2 度/真实帧时，仅使该 FG 输出不具备
Present 资格，连续两个低于 1 度的真实场景后恢复。正常慢转/静止仍为 x2；
急转期间退回真实帧率，因此手感和流畅度需要玩家另行验收。

实现复用现有 FG reset：继续记录 Prepare/Dispatch 和正常 GPU 退休，只重置
FG 的插值历史，不推进或重置 NR 的源帧编号、Evaluate、模型及持久历史。
`FG_TURN_FALLBACK` 记录进入/退出与真实源帧号；实际生成/真实 Present 继续分别
计数。此措施是可选画质回退，不是草片运动矢量/光流根因的算法修复。

CPU 已通过真实生产角度判定代码的 UBSan、边界/滞回/恢复测试，以及入口能力、
NR daily 和退休审核回归（共 53 项针对性 CPU 用例）。增量构建基线为
`734d05e`，源差异 0，四个运行产物 SHA 核验通过。

## 新 Native 的实际 GPU 结果

独占 GPU、新 boot `c2371c64-e6df-43f0-a0ef-9e5a4ce14938`，RTX 4060 Laptop，
1920×1080 外屏。所有捕获均在同一个进程中先核对身份，再开始有界输入，200ms
后触发实际 Present 读回；不再使用先输入、后启动昂贵检查导致错过转镜的旧方法。

| 实际场景采样（16 帧） | SR-only r7 | NR 联动 r8 |
| --- | --- | --- |
| 慢转 | 8 real + 8 generated | 8 real + 8 generated |
| 快转 | 16 real | 16 real |
| 释放转镜后 | 8 real + 8 generated | 8 real + 8 generated |
| 独立关闭 FG | 未重复 | 16 real，NR 继续 |
| 重新开启 FG | 未重复 | 8 real + 8 generated，NR 继续 |

r7 为 `fg-grass-turnguard-r7`，r8 为 `fg-native-turnguard-r8`。
r7 的首份 `guard-slow` 是菜单样本，明确排除；表中使用进入游戏后的
`guard-scene-slow`。r7 日志记录 >5 度/真实帧进入保护，76 个真实场景被抑制后
退出；保留窗口 5714 对生成→真实 Present，无身份/顺序错误。

r8 实际加载 NR 310.9.16 SF Preset B，1080p Quality，Old memory model。
三段转镜及独立开关的日志尾均有 `cache_selected=1` 的真实 SR 读取，读取源编号
连续且不重复，`applied_sr_reset` 均为 0。对应采样不是仅启用 NR 开关但仍读 raw。
Helper 实际完成 2244 次 Evaluate，功能创建/释放各一次；完整收尾通过
`PASS_NATIVE_HELPER_GPU_FD_MODEL_NORMAL_REAP`。保留窗口 3067 对生成→真实 Present
没有顺序错误；其中包含独立 FG off 阶段，不以整窗平均比例判断 x2。

两会话均 rc=0、无残留、无 watchdog/监督中断，FG draw/present 的实际 GPU 完成及
资源释放证明通过。当前 boot 全局 receipt 已 retired。可玩性能采样未启用
validation layers，不据此宣称 Vulkan validation 全净。CPU Present 返回间隔也
不等于屏幕 scanout 或端到端输入延迟。

本机详情：`out/fg-grass-turnguard-r7-verification.json`、
`out/fg-native-turnguard-r8-verification.json`。本 boot 的六轮会话（包含复现与
两版 SDK 对照）合计约 2.21 GiB 逻辑大小，已正常退休会话的 raw 无损转 PNG 并逐
字节回解核验，完整原始 SHA 与 capture 元数据保留；未新建依赖/构建树或 AppImage。

急转保护仍需玩家验收：同一片草丛连续快速左右转镜，比较开关保护时是否还出现
悬空草片，以及退回真实帧率时是否有可接受的流畅度变化。静止/慢转应保持 x2，
F8 与 FG 独立开关应保持可用，最后正常关闭窗口。未加参数的启动命令保持原行为。

默认继续使用玩家 `play-20261022-141640` 的猎人梦境私有检查点；草路诊断只复制
私有会话 `fg-grass-wholeframe-r4`，不改玩家源存档与正式安装。
