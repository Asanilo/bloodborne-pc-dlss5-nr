# 急转时生成帧中的草片错位

## 已复现的范围（2026-10-22）

在猎人梦境草路位置、1080p Quality、NR 关闭的受控转镜中，
`fg-grass-wholeframe-r4` 的整帧 FSR 3.1 生成帧出现了悬空草片：
`wholeframe-grass-fast-turn` 的 f010_real 与 f012_real 天空干净，
中间 f011_interp 的天空有孤立的大块植被贴片。ROI 为 (350,0,600,360)。
本机图证为 `out/fg-grass-generated-only-repro.png`，对应记录为
`out/fg-grass-r4-reproduction.json`。这与正常掉落叶片、F8 切换造成的材质外观
变化不同，也证明问题不只存在于 HUDless 合成。

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
NR daily 和退休审核回归。新 Native 的实际 GPU 验证结果需另行记录，不能将
CPU 结果当作画质或 GPU 退休验收。

默认继续使用玩家 `play-20261022-141640` 的猎人梦境私有检查点；草路诊断只复制
私有会话 `fg-grass-wholeframe-r4`，不改玩家源存档与正式安装。
