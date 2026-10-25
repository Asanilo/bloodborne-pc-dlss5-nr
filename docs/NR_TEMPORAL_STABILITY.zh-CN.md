# NR 新历史渐入（实验）

新候选有一部分像素已没有可用旧历史。原 `displayHistory` 对这些像素直接
使用完整新 NR 结果，跳过了当帧的 transition 权重。旧历史覆盖率随推进
下降时，新的 NR 影响会周期性恢复，可能形成可见脉动。

`fade-new` 让新历史独有像素从当前 raw 按原过渡权重渐入 NR：

```text
shown = (raw + epsilon) * exp2(weight * new_log_edit) - epsilon
```

这不是把新物体的颜色乘以weight或淡到黑色；新物体和纹理的raw内容仍在
当帧显示，渐入的是NR修正。可能出现短暂NR强度不均或亮暗边，仍需实际
试玩判断。没有空间滤波、额外整帧等待、历史写回或年龄刷新。

## 选择与回退

默认仍为 `legacy`，不改变玩家安装或默认配置。源码重建并准备既有
私有运行时后，可沿用公开入口：

```bash
export NRBRIDGE_HOME=/absolute/private-root
BB_NR_VISUAL_TRANSITION=fade-new \
  bash tools/nr_dev/play-experiment.sh "$NRBRIDGE_HOME/localinputs/profile.json" \
  --reuse-nr-prefix PREVIOUS_NORMAL_SESSION [其他既有play参数]
```

设置 `BB_NR_VISUAL_TRANSITION=legacy` 或取消该变量恢复旧显示行为。
F8的NR开关、模型Preset A/B和旧模型回退未修改。选择fade-new时，入口
要求Native包含 `NRBRIDGE_NATIVE_HISTORY_FADE_V1` 能力标记；旧二进制
在CPU准入阶段拒绝，不尝试进入GPU或悄悄忽略选项。

正式存档、当前安装、Wine/模型/游戏依赖不随本改动复制或切换。实测使用
独立研究存档。NRBridge既有许可证发布前事项仍保留，本阶段不发布二进制。

## 实际验证与限制

三组均使用同一Native `7e7edf9`、New Memory、1080p Quality、
310.9.16 B、FG Off、60fps上限；无Vulkan验证或F9冷捕获。
逐帧诊断仅用于测量，普通游玩默认每30源帧采样。

| 策略 | 动作段真实Present返回fps | NR影响量均值 | 完整周期归一化波动中位数 | 周期数 |
|---|---:|---:|---:|---:|
| legacy | 59.90 | 0.1118 | 12.19% | 26 |
| fade-new | 59.93 | 0.1027 | 5.57% | 25 |

NR影响量是GPU Return图相对raw的全图平均绝对log2差，停止输入后
0.5..2.9秒内分析。每个完整候选周期按自己的均值归一化，以区分减弱NR
和降低周期调制。本场周期调制约下降54%，同时NR平均影响约下降8.1%。
Compose GPU中位数约0.20ms（legacy 0.2004 / fade 0.1984）；短窗
CPU尾帧时间有噪声，不据此宣称总体性能提升或完全无代价。

这证明本场GPU内部NR调制有所降低，不证明所有场景的实际闪烁已解决。
跨run输入并非逐像素回放配对；Present返回fps也不等于有效录像或扫描输出
fps。X11录制的A/B短片被玩家指出卡顿，已停止用它作视觉验收依据，
不再扩大录制工具开发。画质、亮暗边、细节和亮尾仍需正常试玩判断。

验证范围仅上述NR→SR配置。NR-only、更多分辨率/档位、Old、NR+FG
仍保留原有默认legacy能力，不能据本实验宣称新策略全组合已验收。
两场均 `PASS_NORMAL_TEARDOWN`、rc0、真实Native末用完成、residuals=[]，
前缀RETURNED，session receipt retired；未绕过任何HOLD。

## 已否定的方向

A2a把显示过渡加长到6帧/100ms并丢弃过渡中的候选。在同一场景中将
候选采纳间隔从约5帧拉到10帧，完整周期归一化波动升到22.60%，增加
active空历史和过期，已移除运行选项。原源码/完整历史保留在
`experiment/nr-temporal-ab-20261025`（7e7edf9），不作为玩家修复合流。

较早r3带同步验证和冷捕获，约24fps，200ms TTL经常到期；它不是正常
60fps症状的证据。r5的打印预算结束早于动作，只保留为静止对照。
真正动作窗为r6/r7/r8自动候选就绪后的记录；r8启动截图仍在菜单，
加载部分排除，动作视频已确认进入实际教堂场景。

## 有界诊断

`quality_scenario.py`仅对已准入会话发送短动作，记录实际输入日程。
`quality_window.py`可在play.py确认场景前启动观察器，等真实候选与正valid
GPU样本、短预热及实际Present条件后立即开始动作，避免人工延迟耗完
短日志预算。启动确认必须实际观察游戏场景，不能仅凭HDR路由标记。

```bash
python3 tools/nr_dev/quality_window.py SESSION run-stop-long \
  --label stop-window --min-fps 55 --close
```

`--min-fps`需要私有 `BB_MEMORY_PROFILE=1`。`--close`只请求正常关闭；
真实GPU/Helper/FD退休仍由原监督器和result/audit判定。不再包含X11视频采集功能，避免把不可靠录像引入主工作流程。

`BB_NR_HISTORY_SAMPLE_INTERVAL=1`启用独立诊断specialization：active/target
live交集、old-only/new-only、最大年龄与当前active Advance的首拒。
默认specialization关闭，无新图像/dispatch/GPU等待，统计双slot约多1.8MiB。
`target_current`指示target是否推进到本源帧；stale target不当当前替代。
`source_dead`只表示来源已失效，不能反推前帧原因；`reject_age`仅统计
本次推进新跨过TTL。`cadence_report.py`只读保留日志并保留这些语义。

## 恢复点与来源

旧M10源码 `m10/nr-quality@86b9689`、标签
`recovery/m10-nr-quality-20261025`、已运行Native
`out/recovery/m10-e5a659d`均保留。正常负载对照Native保留在
`out/recovery/normal-7751bed`及`recovery/nr-normal-control-7751bed`。
使用硬链接存档并在重建前解除可变输出链接，没有复制完整环境。
旧M10分支/对应Native需配套恢复；旧私人play-quality脚本不是本分支入口。

DSH B通过Herdr只读复核r3/r6，没有改本树或运行GPU。冻结Sky参考
`ca1c0287fc4b4bbd1cbe304aaa93dc7dd34a417d`、M7
`56f3ef61fc42f89578b7932fadc45e655a9c6d5d`保留来源身份；
本批未迁入未验证的colour range、fingerprint/context缓存或通用框架。
