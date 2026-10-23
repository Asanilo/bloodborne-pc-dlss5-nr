# M4 + M5：统一主线与启动入口

M3 基线 `464f4b6`，M4 `ba85152` 与 M5 `a1839a8` 已保留完整历史合入本地 `nrbridge`。
合流提交：`ca8b891`（M4）、`f300267`（M5，父提交包含两条线）。
恢复引用 `recovery/pre-m4-m5-integration-464f4b6` 保留合流前主线。
本轮使用原 NR 工作树和一份可变 out，未创建新 worktree、AppImage 或完整运行环境副本。

## 实际可选能力

| 模式 | 配置范围 | FG |
|---|---|---|
| 稳定 NR B → DLSS SR | 1080p Quality / Old，NR execution 720p | Off、DLSS x2、HUDless FSR x2 |
| DLSS MFG | 同一 1080p Quality / Old 固定输入 | 2X–6X；3X–6X 使用 GPU DMA-BUF + Consumer Queue |
| M5 NR→SR | DLAA、Quality、Balanced、Performance、Ultra Performance；1080p/1440p及菜单支持的宽高比 | Off |
| 严格 NR-only | Upscaler Off；1080p/1440p实测，NR execution 独立选择 | Off |
| SR-only | NR 开关关闭；原有 SR 和符合固定输入约定的 FG 路径 | 按后端原准入条件 |

新尺寸、NR-only 与 MFG 的组合未准入。启动器和 Runner 都明确拒绝，不能静默关闭 NR
或修改玩家输出分辨率。MFG 的 3X–6X 不接受 Submit-ready 或 CPU transport；x2保留这些回退。
倍率、后端及 NR execution 尺寸更换要求正常退出并建立新会话；M5 的 SR 档位/输出/宽高比
可运行时修改并 reset 历史。F8 保留 NR 效果回退，游戏内 FG 开关保留。

Smooth Motion 继续因 M3 DeviceLost/未证明正常退休而禁用，本轮没有启用或修改它。
NR Preset A/B、旧模型和 Helper 的独立封存配置继续保留，不由 SR 档位或 MFG 倍率替换。
新配置默认 FG Off、DLSS 倍率 2X，M5 尺寸开关默认关闭；没有切换正式玩家安装或存档。

## 统一入口

完整启动器是 `launcher/bbport_launcher.py`。源构建可以显式指定已有构建和用户自己的封存输入：

```bash
export BB_NRBRIDGE_ACTIVE=1
export NRBRIDGE_APP="$PWD"
export NRBRIDGE_NATIVE_BUILD="$PWD/out"
export NRBRIDGE_HOME=/absolute/private-runtime
export NRBRIDGE_RUNTIME_INPUTS="$NRBRIDGE_HOME/localinputs/unified-B.json"
python3 launcher/bbport_launcher.py
```

在同一个 NRBridge 设置组选择 FG 后端/倍率、NR 开关和 M5 尺寸开关；图形页选择 SR、输出
及宽高比。M5 + Upscaler Off 是严格 NR-only，DLAA 仍调用同分辨率 SR Feature。
3X–6X 需要高级设置的 `consumer-queue-v1` 和 `gpu-dmabuf-v1`；不兼容设置保持可编辑，
启动时给出明确原因。模型、Wine、运行目录和已正常退休的 prefix 由用户选择。

命令行使用同一个受管 Runner 的公开入口，以下示例不包含开发机路径：

```bash
# NR-only / 1440p / 固定 NR720执行域
bash tools/nr_dev/play-experiment.sh "$NRBRIDGE_RUNTIME_INPUTS" \
  --fg-backend off --nr-sr-preset native --nr-output 2560x1440 --nr-execution 1280x720

# 改为 NR→SR Quality：把 native 改成 quality；其他档位同理。

# 1080p Quality / NR B + DLSS MFG 6X
bash tools/nr_dev/play-experiment.sh "$NRBRIDGE_RUNTIME_INPUTS" \
  --fg-backend dlssg --dlssg-multiplier 6 \
  --dlssg-pipeline consumer-queue-v1 --dlssg-transport gpu-dmabuf-v1 \
  --dlssg-model /absolute/user-models/nvngx_dlssg.dll \
  --dlssg-runtime "$PWD/out" --dlssg-prefix-session retired-fg-prefix

# 原 x2 Submit-ready：multiplier=2，pipeline=submit-ready-v1。
# FSR回退：--fg-backend fsr31；完全关闭FG：--fg-backend off。
```

MFG 命令不附加 M5 尺寸参数。存档选择/复制/正常退休同步、GPU 独占锁、HOLD、actual
last-use、CONSUMED、FD/model 退休仍由同一个 Runner 管理。不要绕过它直接启动 Helper。

## 构建与接口处理

- Native：`bash scripts/dev-build.sh --test`，复用已准备依赖和共享编译缓存。
- NR Helper：[独立编译说明](../nrbridge/README.md#build)，M5需要v3r2能力标记；
  主线回归复用 M5 已验证的 NR Helper。未指定 M5 时仍使用原 wire 2 固定720p协议。
- FG Helper：合流后 NR transport 头变化，因此需要按合流闭包重新执行
  `python3 tools/fg_dev/build_wine_probe.py --compiler /absolute/llvm-mingw/bin/x86_64-w64-mingw32-g++`。
  使用已有 SDK、Vulkan/JSON 头和 Wine CUDA 兼容库；这些依赖不随源码提交。
- 唯一文本冲突是 `launcher/nrbridge_entry.py` 参数转发，已同时保留
  `nr_sr_preset/nr_output/nr_execution` 和 `dlssg_multiplier`。
- M4 的 batch-wide disable-interpolation buffer、独立输出图像、真实 batch fence、
  consumer timeline 和全部输出 ACK 原样保留；没有将第二索引的未写 sentinel 当成功。
- 对统一的 launcher → entry → play/Runner 新增接口回归，覆盖 2X–6X、M5各档位/
  NR-only、默认值以及不兼容组合拒绝。FG Helper 源码/产物身份检查未放宽。

## 合流回归

所有场景使用同一 `nrbridge` Native 构建（源码 `f300267`），合流后重新构建的 FG Helper，
以及 NR 310.9.16 Preset B。使用独立玩家设置与私有存档，不借用或修改原玩家存档。

| 本轮实际场景 | 结果 |
|---|---|
| 1080p Quality / Old，NR B + MFG 6X / Consumer Queue / GPU | 5秒窗口162 real + 810 generated，162/162 real Present选中NR缓存；顺序错误0；设备总占用4935 MiB |
| 原 DLSS x2 / Submit-ready / GPU，NR B + SR | 5秒200 real + 201 generated，200/200选中NR；顺序错误0；设备总占用4604 MiB |
| HUDless FSR x2，NR B + SR | 5秒283 real + 281 generated，283/283选中NR；顺序错误0；设备总占用4345 MiB |
| 2560×1440 严格 NR-only，FG Off | 实际render=2560×1440、model=1280×720；启动采样28次Present全部记录No-SR，19次选中NR缓存 |
| 同进程切1440p Balanced NR→SR | render=1506×848，output=2560×1440；历史reset，采样463/474选中NR，约58.3 real Present/s |
| 再切16:10 / 1920×1200 Quality | render=1280×800，output=1920×1200；再次reset，481/485选中NR，约59.3 real Present/s |

动态切换窗口包含重建初期的原画回退，随后恢复NR；NR执行域保持1280×720。
M5三段设备总占用采样约4.2–4.4 GiB（初次创建前另有3405 MiB样本）。
这些是短时同场景回归，设备占用包含桌面/驱动，Present数量不等于物理扫描显示或输入延迟，
也不作为严谨性能横评。四场均查看实际可操作游戏/HUD；画质仍为 `NOT_ACCEPTED`。

6X另做一次有界转镜的16帧实际读回：保留逐帧Present/source/index映射，一组完整的
5 generated + real图像均不同，场景位移按生成索引递进。长期运行时stdout轮转可丢失
早期映射，因此分析器支持显式 `--mapping-log` 读取保留的Native日志，并记录日志SHA256；
原有16帧映射断言不变。确认PNG与raw解码逐字节一致后，仅回收本轮重复raw 132,710,400 B，
保留PNG、读回元数据和日志，不制作游戏/运行时归档。植被错位与其他时域伪影不在本轮修复。

四场均 `PASS_NORMAL_TEARDOWN`，`native_lastuse_proof=true`，残留进程为空、
NR prefix lease为`RETURNED`；NR terminal finalized/captured/obsolete分别为：
6X `1543/1533/10`、x2 `588/587/1`、FSR `1229/1228/1`、M5 `962/934/28`。
共享图像/导出FD归零，NR模型释放，Native/Helper正常reap；DLSS两场还验证独立FG输出ACK、
stop-ready/native-released/stream-retired闭环。会话guard最终为retired，无HOLD被清除。
仅私有续玩存档正常同步；正式安装current/previous指向与原玩家存档不变。

增量Native构建与内置检查通过；聚焦CPU回归158项通过（FG 54、launcher 61、
统一入口4、play 13、退休16、协议7、daily retirement 3），协议身份检查通过。
另以实际GTK控件确认同一窗口含M5开关/NR execution及2X–6X倍率，默认FG Off/x2；
未启动额外GPU场景。独立线的3X–5X与其余M5矩阵沿用原验收，不冒充本轮重跑：
[M4记录](DLSS_MFG_M4.zh-CN.md)、[M5记录](M5_NR_DIMENSIONS.zh-CN.md)。

本机隔离入口为被Git忽略的 `out/play-unified.sh`，打开完整启动器，使用本轮私有设置/存档、
统一Native与Helper；初值NR B / 1080p Quality / Old / FG Off。公开可重建入口仍是上文
`launcher/bbport_launcher.py` 和 `tools/nr_dev/play-experiment.sh`，不依赖该私人脚本。

## 空间与交付边界

本轮主工作树 `du -sb` 从20,482,092,124 B到约21,091,337,338 B（净增约581 MiB），
其中out从20,407,120,795 B到约21,016,200,614 B；共享ccache另增76,737,793 B（约73 MiB）。
统计包含增量产物、四场诊断、私有存档与截图；不包含重复依赖/Wine树/AppImage。
原M4/M5工作目录、分支、独立实验和M3恢复引用均保留，未覆盖其他Agent工作。

本轮不推送公开二进制，不切换玩家安装；NRBridge既有GPL兼容性/分发问题仍需在公开发布前解决。
不将新尺寸+MFG、Smooth Motion、jitter校正或新memory model标记为通过。
