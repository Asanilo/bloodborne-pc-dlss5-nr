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

## M1 决定：舍弃急转抑制方案

`734d05e` 的急转 real-only fallback 已从 M1 源码与启动参数中撤销。
玩家确认此方案没有收益，不作为默认或可选修复继续维护；原提交和下述测量
保留在 Git 历史中。M1 保留 HUDless FSR FG x2，植被空间漂移仍为已知缺陷，
画质尚未验收。下一阶段方向为 NVIDIA DLSS FG，FSR FG 保留为可选后端。

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

上述 r7/r8 仅为已弃用方案的历史测试，不是 M1 的默认行为或画质验收。
当前实验入口见 [M1 集成说明](NR_SR_FG_INTEGRATION.zh-CN.md)。

默认继续使用玩家 `play-20261022-141640` 的猎人梦境私有检查点；草路诊断只复制
私有会话 `fg-grass-wholeframe-r4`，不改玩家源存档与正式安装。
